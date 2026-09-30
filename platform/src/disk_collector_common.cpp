#include "disk_collector.h"

#include <algorithm>
#include <utility>

namespace orion::platform::detail {

std::vector<orion::core::DiskVolume> calculateDiskVolumes(
    const std::vector<DiskCounterSample>& before,
    const std::vector<DiskCounterSample>& after,
    const double elapsedSeconds)
{
    std::vector<orion::core::DiskVolume> result;
    result.reserve(after.size());
    for (const auto& current : after) {
        auto volume = current.volume;
        volume.totalReadBytes = current.readBytes;
        volume.totalWrittenBytes = current.writtenBytes;
        const auto previous = std::ranges::find_if(before, [&](const auto& candidate) {
            return candidate.volume.mountPoint == current.volume.mountPoint;
        });
        if (previous != before.end() && elapsedSeconds > 0.0) {
            if (current.readBytes.has_value() && previous->readBytes.has_value()
                && *current.readBytes >= *previous->readBytes) {
                volume.readBytesPerSecond = static_cast<double>(
                    *current.readBytes - *previous->readBytes)
                    / elapsedSeconds;
            }
            if (current.writtenBytes.has_value() && previous->writtenBytes.has_value()
                && *current.writtenBytes >= *previous->writtenBytes) {
                volume.writeBytesPerSecond = static_cast<double>(
                    *current.writtenBytes - *previous->writtenBytes)
                    / elapsedSeconds;
            }
            const auto readTimeDelta = current.readTime100ns.has_value()
                    && previous->readTime100ns.has_value()
                    && *current.readTime100ns >= *previous->readTime100ns
                ? std::optional {*current.readTime100ns - *previous->readTime100ns}
                : std::nullopt;
            const auto writeTimeDelta = current.writeTime100ns.has_value()
                    && previous->writeTime100ns.has_value()
                    && *current.writeTime100ns >= *previous->writeTime100ns
                ? std::optional {*current.writeTime100ns - *previous->writeTime100ns}
                : std::nullopt;
            if (readTimeDelta.has_value() && writeTimeDelta.has_value()) {
                volume.busyPercent = std::clamp(
                    100.0 * static_cast<double>(*readTimeDelta + *writeTimeDelta)
                        / (elapsedSeconds * 10'000'000.0),
                    0.0,
                    100.0);
            }
            if (readTimeDelta.has_value() && current.readCount.has_value()
                && previous->readCount.has_value()
                && *current.readCount > *previous->readCount) {
                volume.readLatencyMs = static_cast<double>(*readTimeDelta)
                    / static_cast<double>(*current.readCount - *previous->readCount)
                    / 10'000.0;
            }
            if (writeTimeDelta.has_value() && current.writeCount.has_value()
                && previous->writeCount.has_value()
                && *current.writeCount > *previous->writeCount) {
                volume.writeLatencyMs = static_cast<double>(*writeTimeDelta)
                    / static_cast<double>(*current.writeCount - *previous->writeCount)
                    / 10'000.0;
            }
        }
        result.push_back(std::move(volume));
    }
    return result;
}

} // namespace orion::platform::detail
