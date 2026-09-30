#include "disk_collector.h"

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cwchar>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace orion::platform::detail {
namespace {

[[nodiscard]] std::string utf8FromWide(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

[[nodiscard]] std::string storageType(const HANDLE volume)
{
    STORAGE_PROPERTY_QUERY deviceQuery {};
    deviceQuery.PropertyId = StorageDeviceProperty;
    deviceQuery.QueryType = PropertyStandardQuery;
    std::array<std::byte, 1024> descriptorBuffer {};
    DWORD returned = 0;
    STORAGE_BUS_TYPE busType = BusTypeUnknown;
    if (DeviceIoControl(
            volume,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &deviceQuery,
            sizeof(deviceQuery),
            descriptorBuffer.data(),
            static_cast<DWORD>(descriptorBuffer.size()),
            &returned,
            nullptr)
        && returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(
            descriptorBuffer.data());
        busType = descriptor->BusType;
    }
    if (busType == BusTypeNvme) {
        return "NVMe SSD";
    }

    STORAGE_PROPERTY_QUERY seekQuery {};
    seekQuery.PropertyId = StorageDeviceSeekPenaltyProperty;
    seekQuery.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR seekPenalty {};
    if (DeviceIoControl(
            volume,
            IOCTL_STORAGE_QUERY_PROPERTY,
            &seekQuery,
            sizeof(seekQuery),
            &seekPenalty,
            sizeof(seekPenalty),
            &returned,
            nullptr)
        && returned >= sizeof(seekPenalty)) {
        return seekPenalty.IncursSeekPenalty != FALSE ? "HDD" : "SSD";
    }
    if (busType == BusTypeUsb) {
        return "USB";
    }
    return "Unknown";
}

struct IoCounters {
    std::optional<std::uint64_t> readBytes;
    std::optional<std::uint64_t> writtenBytes;
    std::optional<std::uint64_t> readTime100ns;
    std::optional<std::uint64_t> writeTime100ns;
    std::optional<std::uint64_t> readCount;
    std::optional<std::uint64_t> writeCount;
};

[[nodiscard]] IoCounters readIoCounters(const HANDLE volume)
{
    DISK_PERFORMANCE performance {};
    DWORD returned = 0;
    if (!DeviceIoControl(
            volume,
            IOCTL_DISK_PERFORMANCE,
            nullptr,
            0,
            &performance,
            sizeof(performance),
            &returned,
            nullptr)
        || returned < sizeof(performance)) {
        return {};
    }
    if (performance.BytesRead.QuadPart < 0 || performance.BytesWritten.QuadPart < 0
        || performance.ReadTime.QuadPart < 0 || performance.WriteTime.QuadPart < 0) {
        return {};
    }
    return {
        static_cast<std::uint64_t>(performance.BytesRead.QuadPart),
        static_cast<std::uint64_t>(performance.BytesWritten.QuadPart),
        static_cast<std::uint64_t>(performance.ReadTime.QuadPart),
        static_cast<std::uint64_t>(performance.WriteTime.QuadPart),
        static_cast<std::uint64_t>(performance.ReadCount),
        static_cast<std::uint64_t>(performance.WriteCount),
    };
}

} // namespace

std::vector<DiskCounterSample> readDiskCounterSamples()
{
    const DWORD required = GetLogicalDriveStringsW(0, nullptr);
    if (required == 0) {
        return {};
    }
    std::vector<wchar_t> drives(required + 1, L'\0');
    if (GetLogicalDriveStringsW(required, drives.data()) == 0) {
        return {};
    }

    std::vector<DiskCounterSample> result;
    for (const wchar_t* root = drives.data(); *root != L'\0'; root += std::wcslen(root) + 1) {
        const UINT driveType = GetDriveTypeW(root);
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) {
            continue;
        }
        ULARGE_INTEGER available {};
        ULARGE_INTEGER total {};
        ULARGE_INTEGER free {};
        if (!GetDiskFreeSpaceExW(root, &available, &total, &free) || total.QuadPart == 0) {
            continue;
        }

        wchar_t label[MAX_PATH + 1] {};
        wchar_t fileSystem[MAX_PATH + 1] {};
        GetVolumeInformationW(
            root,
            label,
            static_cast<DWORD>(std::size(label)),
            nullptr,
            nullptr,
            nullptr,
            fileSystem,
            static_cast<DWORD>(std::size(fileSystem)));

        std::wstring devicePath = L"\\\\.\\";
        devicePath.append(root, root + 2);
        const HANDLE volume = CreateFileW(
            devicePath.c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        std::string type = "Unknown";
        std::optional<std::uint64_t> readBytes;
        std::optional<std::uint64_t> writtenBytes;
        IoCounters io;
        if (volume != INVALID_HANDLE_VALUE) {
            type = storageType(volume);
            io = readIoCounters(volume);
            readBytes = io.readBytes;
            writtenBytes = io.writtenBytes;
            CloseHandle(volume);
        }

        orion::core::DiskVolume disk;
        disk.mountPoint = utf8FromWide(root);
        disk.name = utf8FromWide(label);
        if (disk.name.empty()) {
            disk.name = disk.mountPoint;
        }
        disk.fileSystem = utf8FromWide(fileSystem);
        disk.storageType = std::move(type);
        disk.totalBytes = total.QuadPart;
        disk.freeBytes = free.QuadPart;
        disk.usedBytes = disk.totalBytes - std::min(disk.freeBytes, disk.totalBytes);
        disk.usedPercent = 100.0 * static_cast<double>(disk.usedBytes)
            / static_cast<double>(disk.totalBytes);
        result.push_back({
            std::move(disk), readBytes, writtenBytes,
            io.readTime100ns, io.writeTime100ns, io.readCount, io.writeCount});
    }
    std::ranges::sort(result, {}, [](const auto& sample) {
        return sample.volume.mountPoint;
    });
    return result;
}

} // namespace orion::platform::detail
