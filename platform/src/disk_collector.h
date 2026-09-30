#pragma once

#include "orion/core/disk_volume.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace orion::platform::detail {

struct DiskCounterSample {
    orion::core::DiskVolume volume;
    std::optional<std::uint64_t> readBytes;
    std::optional<std::uint64_t> writtenBytes;
    std::optional<std::uint64_t> readTime100ns;
    std::optional<std::uint64_t> writeTime100ns;
    std::optional<std::uint64_t> readCount;
    std::optional<std::uint64_t> writeCount;
};

[[nodiscard]] std::vector<DiskCounterSample> readDiskCounterSamples();

[[nodiscard]] std::vector<orion::core::DiskVolume> calculateDiskVolumes(
    const std::vector<DiskCounterSample>& before,
    const std::vector<DiskCounterSample>& after,
    double elapsedSeconds);

} // namespace orion::platform::detail
