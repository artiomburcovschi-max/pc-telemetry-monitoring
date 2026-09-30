#include "orion/core/process_tree_counters.h"
#include <cmath>
#include <limits>

namespace orion::core {
TreeCounterSample ProcessTreeCounters::update(const std::vector<ProcessInfo>& processes,
    std::size_t expectedAlive, double elapsed, bool rebaseline)
{
    TreeCounterSample result;
    if (!std::isfinite(elapsed) || elapsed < 0 || (previousElapsed_ && elapsed <= *previousElapsed_)) return result;
    const bool interval = previousElapsed_.has_value() && !rebaseline;
    const double seconds = interval ? elapsed - *previousElapsed_ : 0;
    const bool sameSize = !processes.empty() && processes.size() == expectedAlive
        && previous_.size() == processes.size();
    std::array<std::uint64_t, 3> deltas{};
    std::array<std::size_t, 3> known{};
    std::array<bool, 3> deltaOverflow{};
    std::unordered_map<std::uint32_t, ProcessInfo> next;
    for (const auto& process : processes) {
        const bool unique = next.emplace(process.pid, process).second;
        if (!unique) continue;
        const auto before = previous_.find(process.pid);
        if (!interval || !process.creationIdentity || before == previous_.end()
            || before->second.creationIdentity != process.creationIdentity) continue;
        const std::array current{process.readBytes, process.writeBytes, process.pageFaultCount};
        const std::array previous{before->second.readBytes, before->second.writeBytes, before->second.pageFaultCount};
        for (std::size_t i = 0; i < 3; ++i) {
            if (!current[i] || !previous[i] || *current[i] < *previous[i]) continue;
            const auto delta = *current[i] - *previous[i];
            if (delta > std::numeric_limits<std::uint64_t>::max() - deltas[i]) deltaOverflow[i] = true;
            else deltas[i] += delta;
            ++known[i];
        }
    }
    std::array<std::optional<double>, 3> rates;
    std::array<std::optional<std::uint64_t>, 3> totals;
    for (std::size_t i = 0; i < 3; ++i) {
        result.complete[i] = interval && sameSize && known[i] == processes.size() && !deltaOverflow[i];
        if (result.complete[i]) rates[i] = static_cast<double>(deltas[i]) / seconds;
        if (known[i]) {
            observed_[i] = true;
            if (deltaOverflow[i] || deltas[i] > std::numeric_limits<std::uint64_t>::max() - totals_[i]) overflow_[i] = true;
            else totals_[i] += deltas[i];
        }
        if (observed_[i] && !overflow_[i]) totals[i] = totals_[i];
    }
    result.readBytesPerSecond = rates[0]; result.writeBytesPerSecond = rates[1]; result.faultsPerSecond = rates[2];
    result.observedReadBytes = totals[0]; result.observedWriteBytes = totals[1]; result.observedFaults = totals[2];
    previous_ = std::move(next);
    previousElapsed_ = elapsed;
    return result;
}
}
