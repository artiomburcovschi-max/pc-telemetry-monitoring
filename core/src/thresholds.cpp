#include "orion/core/thresholds.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>

namespace orion::core {
namespace {

struct TemperatureProfile {
    std::string_view name;
    double warning;
    double critical;
};

constexpr std::array kTemperatureProfiles {
    TemperatureProfile {"cpu", 75.0, 85.0},
    TemperatureProfile {"gpu", 75.0, 85.0},
    TemperatureProfile {"gpu_hotspot", 90.0, 105.0},
    TemperatureProfile {"hdd", 45.0, 55.0},
    TemperatureProfile {"ssd", 55.0, 70.0},
    TemperatureProfile {"nvme", 70.0, 85.0},
    TemperatureProfile {
        "generic",
        kGenericTemperatureCriticalC * kWarningRatio,
        kGenericTemperatureCriticalC},
};

[[nodiscard]] bool validNumber(const std::optional<double> value) noexcept
{
    return value.has_value() && std::isfinite(*value);
}

[[nodiscard]] std::string lowercase(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] int severityRank(const StatusLevel level) noexcept
{
    switch (level) {
    case StatusLevel::Unknown:
        return -1;
    case StatusLevel::Ok:
        return 0;
    case StatusLevel::Warning:
        return 1;
    case StatusLevel::Critical:
        return 2;
    }
    return -1;
}

} // namespace

StatusLevel levelForPercent(
    const std::optional<double> value,
    const double critical) noexcept
{
    if (!validNumber(value) || !std::isfinite(critical)) {
        return StatusLevel::Unknown;
    }
    if (*value >= critical) {
        return StatusLevel::Critical;
    }
    if (*value >= critical * kWarningRatio) {
        return StatusLevel::Warning;
    }
    return StatusLevel::Ok;
}

std::pair<double, double> temperatureLimits(const std::string_view sensorKind) noexcept
{
    const auto normalized = lowercase(sensorKind.empty() ? "generic" : sensorKind);
    for (const auto& profile : kTemperatureProfiles) {
        if (profile.name == normalized) {
            return {profile.warning, profile.critical};
        }
    }
    const auto& fallback = kTemperatureProfiles.back();
    return {fallback.warning, fallback.critical};
}

StatusLevel levelForTemperature(
    const std::optional<double> value,
    const std::string_view sensorKind) noexcept
{
    if (!validNumber(value)) {
        return StatusLevel::Unknown;
    }
    const auto [warning, critical] = temperatureLimits(sensorKind);
    if (*value >= critical) {
        return StatusLevel::Critical;
    }
    if (*value >= warning) {
        return StatusLevel::Warning;
    }
    return StatusLevel::Ok;
}

StatusLevel worse(const StatusLevel left, const StatusLevel right) noexcept
{
    if (left == StatusLevel::Unknown) {
        return right;
    }
    if (right == StatusLevel::Unknown) {
        return left;
    }
    return severityRank(left) >= severityRank(right) ? left : right;
}

} // namespace orion::core

