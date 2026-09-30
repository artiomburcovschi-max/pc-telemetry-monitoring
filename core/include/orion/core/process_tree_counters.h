#pragma once
#include "orion/core/process_info.h"
#include <array>
#include <unordered_map>

namespace orion::core {
struct TreeCounterSample {
    // Rates require complete adjacent observations of the same identified tree.
    std::optional<double> readBytesPerSecond, writeBytesPerSecond, faultsPerSecond;
    // Sums of measured per-process deltas only, not lifetime OS counters.
    std::optional<std::uint64_t> observedReadBytes, observedWriteBytes, observedFaults;
    std::array<bool, 3> complete{};
};
class ProcessTreeCounters {
public:
    [[nodiscard]] TreeCounterSample update(const std::vector<ProcessInfo>& processes,
        std::size_t expectedAlive, double elapsed, bool rebaseline = false);
private:
    std::unordered_map<std::uint32_t, ProcessInfo> previous_;
    std::array<std::uint64_t, 3> totals_{};
    std::array<bool, 3> observed_{}, overflow_{};
    std::optional<double> previousElapsed_;
};
}
