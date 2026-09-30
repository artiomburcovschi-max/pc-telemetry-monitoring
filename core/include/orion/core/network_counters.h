#pragma once

#include "orion/core/telemetry_data.h"

namespace orion::core
{

struct NetworkCounterSample
{
    Metric<std::uint64_t> receivedBytes;
    Metric<std::uint64_t> sentBytes;
    Metric<std::uint64_t> errors;
    Metric<std::uint64_t> drops;
    Metric<std::uint64_t> intervalErrors;
    Metric<std::uint64_t> intervalDrops;
};

// Deltas cover consecutive published samples, not the backend's short rate probe.
// Keep integer precision and rebaseline after a gap, pause, reset or source change.
class NetworkCounterTracker
{
public:
    void reset()
    {
        previousErrors_ = {};
        previousDrops_ = {};
    }

    [[nodiscard]] NetworkCounterSample update(const TelemetryData& data)
    {
        if (scope_ != data.netCounterScope)
            reset();
        scope_ = data.netCounterScope;
        return { data.netTotalReceivedBytes, data.netTotalSentBytes, data.totalErrors, data.totalDrops,
            delta(data.totalErrors, previousErrors_), delta(data.totalDrops, previousDrops_) };
    }

private:
    static Metric<std::uint64_t> delta(const Metric<std::uint64_t>& current, Metric<std::uint64_t>& previous)
    {
        auto result = Metric<std::uint64_t>::unavailable(
            DataQuality::Unavailable, current.source, "baseline_required");
        if (!current.usable() || current.quality == DataQuality::Stale)
        {
            previous = {};
            return Metric<std::uint64_t>::unavailable(current.quality, current.source,
                current.reason.empty() ? "counter_unavailable" : current.reason);
        }
        if (previous.usable() && current.source == previous.source && current.observedAt
            && previous.observedAt && *current.observedAt > *previous.observedAt)
        {
            if (*current.value >= *previous.value)
            {
                result = Metric<std::uint64_t>::valid(
                    *current.value - *previous.value, current.source, *current.observedAt);
                if (current.quality == DataQuality::Estimated || previous.quality == DataQuality::Estimated)
                    result.quality = DataQuality::Estimated;
            }
            else
            {
                result.reason = "counter_reset";
            }
        }
        previous = current;
        return result;
    }

    Metric<std::uint64_t> previousErrors_;
    Metric<std::uint64_t> previousDrops_;
    std::string scope_;
};

} // namespace orion::core
