#include "sensor_collector.h"

#include <windows.h>
#include <wbemidl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace orion::platform::detail {
namespace {

[[nodiscard]] std::string utf8FromWide(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

[[nodiscard]] std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] bool containsAny(const std::string_view value,
                               const std::initializer_list<std::string_view> tokens)
{
    return std::ranges::any_of(tokens, [value](const auto token) {
        return value.find(token) != std::string_view::npos;
    });
}

[[nodiscard]] orion::core::SensorComponent classify(
    const std::string& name,
    const std::string& identifier)
{
    const auto text = lower(name + " " + identifier);
    if (containsAny(text, {"/cpu/", "intelcpu", "amdcpu", "cpu package", "core max", "tctl", "tdie"})) {
        return orion::core::SensorComponent::Cpu;
    }
    if (containsAny(text, {"/gpu", "gpu core", "gpu hotspot", "gpu memory", "nvidia", "amdgpu"})) {
        return orion::core::SensorComponent::Gpu;
    }
    if (containsAny(text, {"storage", "hdd", "ssd", "nvme"})) {
        return orion::core::SensorComponent::Storage;
    }
    if (containsAny(text, {"mainboard", "motherboard", "chipset", "pch", "acpi", "system"})) {
        return orion::core::SensorComponent::System;
    }
    return orion::core::SensorComponent::Other;
}

[[nodiscard]] std::string variantText(const VARIANT& value)
{
    return value.vt == VT_BSTR && value.bstrVal != nullptr
        ? utf8FromWide(value.bstrVal)
        : std::string {};
}

[[nodiscard]] std::optional<double> variantNumber(const VARIANT& value)
{
    double result = 0.0;
    switch (value.vt) {
    case VT_R4:
        result = value.fltVal;
        break;
    case VT_R8:
        result = value.dblVal;
        break;
    case VT_I2:
        result = value.iVal;
        break;
    case VT_UI2:
        result = value.uiVal;
        break;
    case VT_I4:
    case VT_INT:
        result = value.lVal;
        break;
    case VT_UI4:
    case VT_UINT:
        result = value.ulVal;
        break;
    case VT_I8:
        result = static_cast<double>(value.llVal);
        break;
    case VT_UI8:
        result = static_cast<double>(value.ullVal);
        break;
    default:
        return std::nullopt;
    }
    return std::isfinite(result) ? std::optional {result} : std::nullopt;
}

[[nodiscard]] std::string objectText(IWbemClassObject* object, const wchar_t* property)
{
    VARIANT value;
    VariantInit(&value);
    const auto result = SUCCEEDED(object->Get(property, 0, &value, nullptr, nullptr))
        ? variantText(value)
        : std::string {};
    VariantClear(&value);
    return result;
}

[[nodiscard]] std::optional<double> objectNumber(
    IWbemClassObject* object,
    const wchar_t* property)
{
    VARIANT value;
    VariantInit(&value);
    const auto result = SUCCEEDED(object->Get(property, 0, &value, nullptr, nullptr))
        ? variantNumber(value)
        : std::nullopt;
    VariantClear(&value);
    return result;
}

class ComSession final {
public:
    ComSession()
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
    {
    }

    ~ComSession()
    {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool usable() const noexcept
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_;
};

[[nodiscard]] IWbemServices* connectNamespace(
    IWbemLocator* locator,
    const wchar_t* namespaceName)
{
    BSTR name = SysAllocString(namespaceName);
    IWbemServices* services = nullptr;
    const HRESULT result = locator->ConnectServer(
        name, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    SysFreeString(name);
    if (FAILED(result) || services == nullptr) {
        return nullptr;
    }
    if (FAILED(CoSetProxyBlanket(
            services,
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            nullptr,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE))) {
        services->Release();
        return nullptr;
    }
    return services;
}

[[nodiscard]] IEnumWbemClassObject* executeQuery(
    IWbemServices* services,
    const wchar_t* queryText)
{
    BSTR language = SysAllocString(L"WQL");
    BSTR query = SysAllocString(queryText);
    IEnumWbemClassObject* enumerator = nullptr;
    const HRESULT result = services->ExecQuery(
        language,
        query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        &enumerator);
    SysFreeString(query);
    SysFreeString(language);
    return SUCCEEDED(result) ? enumerator : nullptr;
}

void appendTemperature(
    SensorSnapshot& snapshot,
    orion::core::TemperatureSensor sensor)
{
    if (sensor.valueC < -50.0 || sensor.valueC > 160.0) {
        return;
    }
    const auto duplicate = std::ranges::any_of(snapshot.temperatures, [&sensor](const auto& current) {
        return current.component == sensor.component
            && lower(current.label) == lower(sensor.label);
    });
    if (!duplicate) {
        snapshot.temperatures.push_back(std::move(sensor));
    }
}

void appendFan(SensorSnapshot& snapshot, orion::core::FanSensor sensor)
{
    if (!sensor.rpm.has_value() && !sensor.percent.has_value()) {
        return;
    }
    const auto duplicate = std::ranges::any_of(snapshot.fans, [&sensor](const auto& current) {
        return current.component == sensor.component
            && lower(current.label) == lower(sensor.label);
    });
    if (!duplicate) {
        snapshot.fans.push_back(std::move(sensor));
    }
}

bool queryHardwareMonitor(
    IWbemLocator* locator,
    const wchar_t* namespaceName,
    const std::string& source,
    SensorSnapshot& snapshot)
{
    IWbemServices* services = connectNamespace(locator, namespaceName);
    if (services == nullptr) {
        return false;
    }
    IEnumWbemClassObject* enumerator = executeQuery(
        services, L"SELECT Name, Identifier, SensorType, Value FROM Sensor");
    if (enumerator == nullptr) {
        services->Release();
        return false;
    }

    bool found = false;
    for (;;) {
        IWbemClassObject* object = nullptr;
        ULONG returned = 0;
        if (enumerator->Next(500, 1, &object, &returned) != WBEM_S_NO_ERROR
            || returned == 0 || object == nullptr) {
            break;
        }
        const auto name = objectText(object, L"Name");
        const auto identifier = objectText(object, L"Identifier");
        const auto type = lower(objectText(object, L"SensorType"));
        const auto value = objectNumber(object, L"Value");
        const auto component = classify(name, identifier);
        if (value.has_value() && type == "temperature") {
            appendTemperature(snapshot, {
                component,
                name.empty() ? "Temperature" : name,
                *value,
                std::nullopt,
                std::nullopt,
                source,
                identifier,
            });
            found = true;
        } else if (value.has_value() && type == "fan" && *value >= 0.0) {
            appendFan(snapshot, {
                component,
                name.empty() ? "Fan" : name,
                *value,
                std::nullopt,
                source,
                identifier,
            });
            found = true;
        } else if (value.has_value() && type == "control" && *value >= 0.0
                   && *value <= 100.0
                   && containsAny(lower(name + " " + identifier), {"fan", "cooler"})) {
            appendFan(snapshot, {
                component,
                name.empty() ? "Fan control" : name,
                std::nullopt,
                *value,
                source,
                identifier,
            });
            found = true;
        }
        object->Release();
    }
    enumerator->Release();
    services->Release();
    return found;
}

bool queryAcpiZones(IWbemLocator* locator, SensorSnapshot& snapshot)
{
    IWbemServices* services = connectNamespace(locator, L"ROOT\\WMI");
    if (services == nullptr) {
        return false;
    }
    IEnumWbemClassObject* enumerator = executeQuery(
        services, L"SELECT InstanceName, CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
    if (enumerator == nullptr) {
        services->Release();
        return false;
    }
    bool found = false;
    for (;;) {
        IWbemClassObject* object = nullptr;
        ULONG returned = 0;
        if (enumerator->Next(500, 1, &object, &returned) != WBEM_S_NO_ERROR
            || returned == 0 || object == nullptr) {
            break;
        }
        const auto raw = objectNumber(object, L"CurrentTemperature");
        const auto identifier = objectText(object, L"InstanceName");
        if (raw.has_value() && *raw > 0.0) {
            const double celsius = *raw / 10.0 - 273.15;
            if (celsius >= -50.0 && celsius <= 160.0) {
                appendTemperature(snapshot, {
                    orion::core::SensorComponent::System,
                    identifier.empty() ? "ACPI thermal zone" : identifier,
                    celsius,
                    std::nullopt,
                    std::nullopt,
                    "Windows ACPI thermal zone",
                    identifier,
                });
                found = true;
            }
        }
        object->Release();
    }
    enumerator->Release();
    services->Release();
    return found;
}

} // namespace

SensorSnapshot readSensorSnapshot(const GpuSample& gpu)
{
    SensorSnapshot snapshot;
    if (gpu.temperatureC.has_value()) {
        appendTemperature(snapshot, {
            orion::core::SensorComponent::Gpu,
            "GPU Core",
            *gpu.temperatureC,
            std::nullopt,
            std::nullopt,
            gpu.source,
            "gpu:primary:temperature",
        });
    }
    if (gpu.fanPercent.has_value()) {
        appendFan(snapshot, {
            orion::core::SensorComponent::Gpu,
            "GPU Fan",
            std::nullopt,
            *gpu.fanPercent,
            gpu.source,
            "gpu:primary:fan",
        });
    }

    ComSession com;
    if (!com.usable()) {
        snapshot.source = gpu.source;
        snapshot.reason = "COM is unavailable; motherboard sensors require a running hardware monitor provider";
        return snapshot;
    }
    IWbemLocator* locator = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_WbemLocator,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator,
            reinterpret_cast<void**>(&locator)))
        || locator == nullptr) {
        snapshot.source = gpu.source;
        snapshot.reason = "Windows WMI is unavailable";
        return snapshot;
    }

    const bool libre = queryHardwareMonitor(
        locator, L"ROOT\\LibreHardwareMonitor", "LibreHardwareMonitor WMI", snapshot);
    const bool open = queryHardwareMonitor(
        locator, L"ROOT\\OpenHardwareMonitor", "OpenHardwareMonitor WMI", snapshot);
    const bool acpi = queryAcpiZones(locator, snapshot);
    locator->Release();

    snapshot.source = libre ? "LibreHardwareMonitor WMI"
        : open ? "OpenHardwareMonitor WMI"
        : acpi ? "Windows ACPI thermal zone"
               : gpu.source;
    if (!libre && !open) {
        snapshot.reason =
            "CPU and motherboard sensors are not exposed by standard Windows APIs; "
            "run LibreHardwareMonitor or OpenHardwareMonitor to publish them through WMI";
    }
    return snapshot;
}

} // namespace orion::platform::detail
