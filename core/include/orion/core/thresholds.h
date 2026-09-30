#pragma once

#include "orion/core/status_level.h"

#include <optional>
#include <string_view>
#include <utility>

namespace orion::core {

inline constexpr double kCpuCriticalPercent = 95.0;
inline constexpr double kGpuCriticalPercent = 95.0;
inline constexpr double kRamCriticalPercent = 95.0;
inline constexpr double kGenericTemperatureCriticalC = 85.0;
inline constexpr double kWarningRatio = 0.8;

[[nodiscard]] StatusLevel levelForPercent(
    std::optional<double> value,
    double critical) noexcept;

[[nodiscard]] std::pair<double, double> temperatureLimits(
    std::string_view sensorKind = "generic") noexcept;

[[nodiscard]] StatusLevel levelForTemperature(
    std::optional<double> value,
    std::string_view sensorKind = "generic") noexcept;

[[nodiscard]] StatusLevel worse(StatusLevel left, StatusLevel right) noexcept;

} // namespace orion::core

