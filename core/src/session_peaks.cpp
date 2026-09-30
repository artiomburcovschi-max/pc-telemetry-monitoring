#include "orion/core/session_peaks.h"

#include <algorithm>

namespace orion::core {
namespace {

void updatePeak(std::optional<double>& peak, const Metric<double>& metric) noexcept
{
    if (!metric.usable()) {
        return;
    }
    peak = peak.has_value() ? std::max(*peak, *metric.value) : metric.value;
}

} // namespace

SessionPeaksTracker::SessionPeaksTracker()
    : SessionPeaksTracker(Clock::now())
{
}

SessionPeaksTracker::SessionPeaksTracker(const Clock::time_point startTime)
    : startTime_(startTime)
{
}

void SessionPeaksTracker::update(const TelemetryData& data) noexcept
{
    updatePeak(peaks_.cpu, data.cpuUsagePercent);
    updatePeak(peaks_.ram, data.ramUsagePercent);
    updatePeak(peaks_.gpu, data.gpuUsagePercent);
    updatePeak(peaks_.cpuTemperatureC, data.cpuTemperatureC);
    updatePeak(peaks_.gpuTemperatureC, data.gpuTemperatureC);
}

const SessionPeaks& SessionPeaksTracker::peaks() const noexcept
{
    return peaks_;
}

double SessionPeaksTracker::uptimeSeconds() const noexcept
{
    return uptimeSeconds(Clock::now());
}

double SessionPeaksTracker::uptimeSeconds(const Clock::time_point now) const noexcept
{
    return std::chrono::duration<double>(now - startTime_).count();
}

} // namespace orion::core
