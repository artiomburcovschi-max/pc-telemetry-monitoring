#pragma once

#include "orion/core/telemetry_data.h"

#include <chrono>
#include <optional>

namespace orion::core {

struct SessionPeaks {
    std::optional<double> cpu;
    std::optional<double> ram;
    std::optional<double> gpu;
    std::optional<double> cpuTemperatureC;
    std::optional<double> gpuTemperatureC;
};

class SessionPeaksTracker final {
public:
    using Clock = std::chrono::steady_clock;

    SessionPeaksTracker();
    explicit SessionPeaksTracker(Clock::time_point startTime);

    void update(const TelemetryData& data) noexcept;

    [[nodiscard]] const SessionPeaks& peaks() const noexcept;
    [[nodiscard]] double uptimeSeconds() const noexcept;
    [[nodiscard]] double uptimeSeconds(Clock::time_point now) const noexcept;

private:
    Clock::time_point startTime_;
    SessionPeaks peaks_;
};

} // namespace orion::core
