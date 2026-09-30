#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace orion::core {

struct DiskVolume {
    std::string name;
    std::string mountPoint;
    std::string fileSystem;
    std::string storageType;
    std::uint64_t totalBytes {0};
    std::uint64_t usedBytes {0};
    std::uint64_t freeBytes {0};
    double usedPercent {0.0};
    std::optional<double> readBytesPerSecond;
    std::optional<double> writeBytesPerSecond;
    std::optional<std::uint64_t> totalReadBytes;
    std::optional<std::uint64_t> totalWrittenBytes;
    std::optional<double> busyPercent;
    std::optional<double> readLatencyMs;
    std::optional<double> writeLatencyMs;

    [[nodiscard]] bool operator==(const DiskVolume&) const = default;
};

} // namespace orion::core
