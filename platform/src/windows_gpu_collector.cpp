#include "gpu_collector.h"

#include <windows.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace orion::platform::detail {
namespace {

using NvmlDevice = void*;
struct NvmlUtilization {
    unsigned int gpu;
    unsigned int memory;
};
struct NvmlMemory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

template<typename Function>
[[nodiscard]] Function loadFunction(const HMODULE module, const char* name)
{
    const auto procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

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

[[nodiscard]] HMODULE loadNvmlLibrary()
{
    if (const auto module = LoadLibraryW(L"nvml.dll"); module != nullptr) {
        return module;
    }
    for (const auto* variable : {L"ProgramW6432", L"ProgramFiles"}) {
        std::array<wchar_t, MAX_PATH> root {};
        const DWORD length = GetEnvironmentVariableW(
            variable, root.data(), static_cast<DWORD>(root.size()));
        if (length == 0 || length >= root.size()) {
            continue;
        }
        std::wstring path(root.data(), length);
        path += L"\\NVIDIA Corporation\\NVSMI\\nvml.dll";
        if (const auto module = LoadLibraryW(path.c_str()); module != nullptr) {
            return module;
        }
    }
    return nullptr;
}

class NvmlApi final {
public:
    NvmlApi()
        : module_(loadNvmlLibrary())
    {
        if (module_ == nullptr) {
            return;
        }
        init_ = loadFunction<Init>(module_, "nvmlInit_v2");
        shutdown_ = loadFunction<Shutdown>(module_, "nvmlShutdown");
        count_ = loadFunction<DeviceGetCount>(module_, "nvmlDeviceGetCount_v2");
        handle_ = loadFunction<DeviceGetHandle>(module_, "nvmlDeviceGetHandleByIndex_v2");
        name_ = loadFunction<DeviceGetName>(module_, "nvmlDeviceGetName");
        utilization_ = loadFunction<DeviceGetUtilization>(module_, "nvmlDeviceGetUtilizationRates");
        temperature_ = loadFunction<DeviceGetTemperature>(module_, "nvmlDeviceGetTemperature");
        fanSpeed_ = loadFunction<DeviceGetFanSpeed>(module_, "nvmlDeviceGetFanSpeed");
        memory_ = loadFunction<DeviceGetMemory>(module_, "nvmlDeviceGetMemoryInfo");
        ready_ = init_ != nullptr && shutdown_ != nullptr && count_ != nullptr
            && handle_ != nullptr && name_ != nullptr && utilization_ != nullptr
            && temperature_ != nullptr && memory_ != nullptr && init_() == 0;
    }

    ~NvmlApi()
    {
        if (ready_) {
            shutdown_();
        }
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    NvmlApi(const NvmlApi&) = delete;
    NvmlApi& operator=(const NvmlApi&) = delete;

    [[nodiscard]] std::optional<GpuSample> sample() const
    {
        if (!ready_) {
            return std::nullopt;
        }
        unsigned int count = 0;
        if (count_(&count) != 0 || count == 0) {
            return std::nullopt;
        }

        NvmlDevice selected = nullptr;
        NvmlMemory selectedMemory {};
        for (unsigned int index = 0; index < count; ++index) {
            NvmlDevice candidate = nullptr;
            NvmlMemory candidateMemory {};
            if (handle_(index, &candidate) == 0 && memory_(candidate, &candidateMemory) == 0
                && (selected == nullptr || candidateMemory.total > selectedMemory.total)) {
                selected = candidate;
                selectedMemory = candidateMemory;
            }
        }
        if (selected == nullptr) {
            return std::nullopt;
        }

        GpuSample result;
        result.source = "NVIDIA NVML";
        std::array<char, 256> nameBuffer {};
        if (name_(selected, nameBuffer.data(), static_cast<unsigned int>(nameBuffer.size())) == 0) {
            result.name = nameBuffer.data();
        }
        result.memoryTotalBytes = static_cast<std::uint64_t>(selectedMemory.total);
        result.memoryUsedBytes = static_cast<std::uint64_t>(selectedMemory.used);
        NvmlUtilization utilization {};
        if (utilization_(selected, &utilization) == 0) {
            result.usagePercent = std::clamp(static_cast<double>(utilization.gpu), 0.0, 100.0);
        }
        unsigned int temperature = 0;
        if (temperature_(selected, 0, &temperature) == 0) {
            result.temperatureC = static_cast<double>(temperature);
        }
        unsigned int fanSpeed = 0;
        if (fanSpeed_ != nullptr && fanSpeed_(selected, &fanSpeed) == 0) {
            result.fanPercent = std::clamp(static_cast<double>(fanSpeed), 0.0, 100.0);
        }
        return result;
    }

private:
    using Init = int (*)();
    using Shutdown = int (*)();
    using DeviceGetCount = int (*)(unsigned int*);
    using DeviceGetHandle = int (*)(unsigned int, NvmlDevice*);
    using DeviceGetName = int (*)(NvmlDevice, char*, unsigned int);
    using DeviceGetUtilization = int (*)(NvmlDevice, NvmlUtilization*);
    using DeviceGetTemperature = int (*)(NvmlDevice, unsigned int, unsigned int*);
    using DeviceGetFanSpeed = int (*)(NvmlDevice, unsigned int*);
    using DeviceGetMemory = int (*)(NvmlDevice, NvmlMemory*);

    HMODULE module_ {nullptr};
    Init init_ {nullptr};
    Shutdown shutdown_ {nullptr};
    DeviceGetCount count_ {nullptr};
    DeviceGetHandle handle_ {nullptr};
    DeviceGetName name_ {nullptr};
    DeviceGetUtilization utilization_ {nullptr};
    DeviceGetTemperature temperature_ {nullptr};
    DeviceGetFanSpeed fanSpeed_ {nullptr};
    DeviceGetMemory memory_ {nullptr};
    bool ready_ {false};
};

[[nodiscard]] GpuSample dxgiInventory()
{
    GpuSample result;
    result.source = "DXGI adapter inventory";
    result.reason = "Live utilization and temperature require a supported vendor driver API";
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void**>(&factory)))) {
        return result;
    }
    SIZE_T bestMemory = 0;
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 description {};
        if (adapter != nullptr && SUCCEEDED(adapter->GetDesc1(&description))
            && (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0
            && (result.name == "Unknown GPU" || description.DedicatedVideoMemory > bestMemory)) {
            result.name = utf8FromWide(description.Description);
            bestMemory = description.DedicatedVideoMemory;
            if (bestMemory > 0) {
                result.memoryTotalBytes = static_cast<std::uint64_t>(bestMemory);
            }
        }
        if (adapter != nullptr) {
            adapter->Release();
        }
    }
    factory->Release();
    return result;
}

} // namespace

GpuSample readGpuSample()
{
    static NvmlApi nvml;
    if (const auto sample = nvml.sample(); sample.has_value()) {
        return *sample;
    }
    static const GpuSample inventory = dxgiInventory();
    return inventory;
}

} // namespace orion::platform::detail
