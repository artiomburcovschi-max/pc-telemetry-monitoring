#include "orion/platform/autostart_collector.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace orion::platform {
namespace {

[[nodiscard]] std::string utf8FromWide(const std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

void readRegistryKey(
    std::vector<orion::core::AutostartEntry>& entries,
    HKEY hive,
    const wchar_t* subkey,
    const std::string& label,
    const orion::core::AutostartCategory category)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(hive, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return;
    }
    DWORD index = 0;
    while (true) {
        std::wstring name(32768, L'\0');
        std::vector<wchar_t> value(32768, L'\0');
        DWORD nameSize = static_cast<DWORD>(name.size());
        DWORD valueBytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        DWORD type = 0;
        const auto result = RegEnumValueW(
            key, index, name.data(), &nameSize, nullptr, &type,
            reinterpret_cast<BYTE*>(value.data()), &valueBytes);
        if (result == ERROR_NO_MORE_ITEMS) {
            break;
        }
        ++index;
        if (result != ERROR_SUCCESS
            || (type != REG_SZ && type != REG_EXPAND_SZ)) {
            continue;
        }
        name.resize(nameSize);
        std::size_t valueLength = valueBytes / sizeof(wchar_t);
        while (valueLength > 0 && value[valueLength - 1] == L'\0') {
            --valueLength;
        }
        entries.push_back({
            utf8FromWide(name),
            utf8FromWide(std::wstring_view(value.data(), valueLength)),
            "Registry: " + label,
            true,
            category,
        });
    }
    RegCloseKey(key);
}

[[nodiscard]] std::wstring environmentPath(const wchar_t* variable)
{
    const DWORD size = GetEnvironmentVariableW(variable, nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(variable, value.data(), size);
    if (written == 0) {
        return {};
    }
    value.resize(written);
    return value;
}

void readStartupFolder(
    std::vector<orion::core::AutostartEntry>& entries,
    const std::filesystem::path& directory,
    const std::string& source,
    const orion::core::AutostartCategory category)
{
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return;
    }
    for (const auto& item : std::filesystem::directory_iterator(
             directory, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (error || !item.is_regular_file(error)) {
            continue;
        }
        const auto filename = item.path().filename().wstring();
        std::wstring lowercase = filename;
        std::ranges::transform(lowercase, lowercase.begin(), ::towlower);
        if (lowercase == L"desktop.ini") {
            continue;
        }
        entries.push_back({
            utf8FromWide(filename),
            utf8FromWide(item.path().wstring()),
            source,
            std::nullopt,
            category,
        });
    }
}

class WindowsAutostartCollector final : public AutostartCollector {
public:
    [[nodiscard]] orion::core::AutostartSnapshot scan() override
    {
        orion::core::AutostartSnapshot snapshot;
        snapshot.source = "Windows Registry Run/RunOnce + Startup folders";
        constexpr auto run = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        constexpr auto runOnce = L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
        readRegistryKey(snapshot.entries, HKEY_CURRENT_USER, run,
            "HKCU\\...\\Run", orion::core::AutostartCategory::user);
        readRegistryKey(snapshot.entries, HKEY_CURRENT_USER, runOnce,
            "HKCU\\...\\RunOnce", orion::core::AutostartCategory::user);
        readRegistryKey(snapshot.entries, HKEY_LOCAL_MACHINE, run,
            "HKLM\\...\\Run", orion::core::AutostartCategory::system);
        readRegistryKey(snapshot.entries, HKEY_LOCAL_MACHINE, runOnce,
            "HKLM\\...\\RunOnce", orion::core::AutostartCategory::system);

        const auto appData = environmentPath(L"APPDATA");
        if (!appData.empty()) {
            readStartupFolder(snapshot.entries,
                std::filesystem::path(appData) / L"Microsoft/Windows/Start Menu/Programs/Startup",
                "Startup (user folder)", orion::core::AutostartCategory::user);
        }
        const auto programData = environmentPath(L"PROGRAMDATA");
        if (!programData.empty()) {
            readStartupFolder(snapshot.entries,
                std::filesystem::path(programData) / L"Microsoft/Windows/Start Menu/Programs/StartUp",
                "Startup (common folder)", orion::core::AutostartCategory::system);
        }
        std::ranges::sort(snapshot.entries, [](const auto& left, const auto& right) {
            if (left.category != right.category) {
                return left.category == orion::core::AutostartCategory::user;
            }
            return left.name < right.name;
        });
        return snapshot;
    }
};

} // namespace

std::unique_ptr<AutostartCollector> makeAutostartCollector()
{
    return std::make_unique<WindowsAutostartCollector>();
}

} // namespace orion::platform
