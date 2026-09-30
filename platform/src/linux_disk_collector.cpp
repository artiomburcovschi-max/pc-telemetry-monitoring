#include "disk_collector.h"

#include <sys/statvfs.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orion::platform::detail {
namespace {

[[nodiscard]] std::string decodeMountField(std::string value)
{
    const std::pair<std::string_view, char> escapes[] {
        {"\\040", ' '}, {"\\011", '\t'}, {"\\012", '\n'}, {"\\134", '\\'},
    };
    for (const auto& [encoded, decoded] : escapes) {
        std::size_t position = 0;
        while ((position = value.find(encoded, position)) != std::string::npos) {
            value.replace(position, encoded.size(), 1, decoded);
            ++position;
        }
    }
    return value;
}

[[nodiscard]] std::string blockNameForDevice(const std::string& device)
{
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(device, error);
    return (error ? std::filesystem::path(device) : resolved).filename().string();
}

[[nodiscard]] std::string physicalBlockName(const std::string& blockName)
{
    std::error_code error;
    const auto sysPath = std::filesystem::weakly_canonical(
        std::filesystem::path("/sys/class/block") / blockName, error);
    if (!error && std::filesystem::exists(
            std::filesystem::path("/sys/class/block") / blockName / "partition")) {
        return sysPath.parent_path().filename().string();
    }
    return blockName;
}

[[nodiscard]] std::optional<std::uint64_t> integerFromFile(
    const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::uint64_t value = 0;
    return input >> value ? std::optional {value} : std::nullopt;
}

[[nodiscard]] std::pair<std::optional<std::uint64_t>, std::optional<std::uint64_t>>
readIoCounters(const std::string& blockName)
{
    std::ifstream input(std::filesystem::path("/sys/class/block") / blockName / "stat");
    std::vector<std::uint64_t> fields;
    std::uint64_t field = 0;
    while (input >> field) {
        fields.push_back(field);
    }
    if (fields.size() < 7) {
        return {};
    }
    const auto sectorSize = integerFromFile(
        std::filesystem::path("/sys/class/block") / blockName / "queue/logical_block_size")
                                .value_or(512);
    return {fields[2] * sectorSize, fields[6] * sectorSize};
}

[[nodiscard]] std::string storageType(const std::string& blockName)
{
    const auto physicalName = physicalBlockName(blockName);
    const auto rotational = integerFromFile(
        std::filesystem::path("/sys/class/block") / physicalName / "queue/rotational");
    if (!rotational.has_value()) {
        return "Unknown";
    }
    if (*rotational != 0) {
        return "HDD";
    }
    return physicalName.starts_with("nvme") ? "NVMe SSD" : "SSD";
}

} // namespace

std::vector<DiskCounterSample> readDiskCounterSamples()
{
    std::ifstream mounts("/proc/mounts");
    std::vector<DiskCounterSample> result;
    std::set<std::string> seenMounts;
    std::string device;
    std::string mountPoint;
    std::string fileSystem;
    std::string options;
    int dump = 0;
    int pass = 0;
    while (mounts >> device >> mountPoint >> fileSystem >> options >> dump >> pass) {
        device = decodeMountField(device);
        mountPoint = decodeMountField(mountPoint);
        if (!device.starts_with("/dev/") || !seenMounts.insert(mountPoint).second) {
            continue;
        }
        struct statvfs usage {};
        if (statvfs(mountPoint.c_str(), &usage) != 0 || usage.f_blocks == 0) {
            continue;
        }
        const auto blockSize = static_cast<std::uint64_t>(
            usage.f_frsize != 0 ? usage.f_frsize : usage.f_bsize);
        const auto totalBytes = static_cast<std::uint64_t>(usage.f_blocks) * blockSize;
        const auto freeBytes = static_cast<std::uint64_t>(usage.f_bavail) * blockSize;
        const auto usedBytes = totalBytes - std::min(freeBytes, totalBytes);
        const auto blockName = blockNameForDevice(device);
        auto [readBytes, writtenBytes] = readIoCounters(blockName);

        orion::core::DiskVolume disk;
        disk.name = mountPoint;
        disk.mountPoint = mountPoint;
        disk.fileSystem = fileSystem;
        disk.storageType = storageType(blockName);
        disk.totalBytes = totalBytes;
        disk.usedBytes = usedBytes;
        disk.freeBytes = freeBytes;
        disk.usedPercent = 100.0 * static_cast<double>(usedBytes)
            / static_cast<double>(totalBytes);
        result.push_back({std::move(disk), readBytes, writtenBytes});
    }
    std::ranges::sort(result, {}, [](const auto& sample) {
        return sample.volume.mountPoint;
    });
    return result;
}

} // namespace orion::platform::detail
