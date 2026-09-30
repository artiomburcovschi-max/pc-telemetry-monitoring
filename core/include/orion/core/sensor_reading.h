#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace orion::core {

enum class SensorComponent {
    Cpu,
    Gpu,
    Storage,
    System,
    Other,
};

[[nodiscard]] constexpr std::string_view toString(const SensorComponent component) noexcept
{
    switch (component) {
    case SensorComponent::Cpu:
        return "cpu";
    case SensorComponent::Gpu:
        return "gpu";
    case SensorComponent::Storage:
        return "storage";
    case SensorComponent::System:
        return "system";
    case SensorComponent::Other:
        return "other";
    }
    return "other";
}

struct TemperatureSensor {
    SensorComponent component {SensorComponent::Other};
    std::string label;
    double valueC {0.0};
    std::optional<double> highC;
    std::optional<double> criticalC;
    std::string source;
    std::string identifier;
};

struct FanSensor {
    SensorComponent component {SensorComponent::Other};
    std::string label;
    std::optional<double> rpm;
    std::optional<double> percent;
    std::string source;
    std::string identifier;
};

} // namespace orion::core
