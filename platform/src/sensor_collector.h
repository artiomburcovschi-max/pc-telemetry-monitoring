#pragma once

#include "gpu_collector.h"
#include "orion/core/sensor_reading.h"

#include <string>
#include <vector>

namespace orion::platform::detail {

struct SensorSnapshot {
    std::vector<orion::core::TemperatureSensor> temperatures;
    std::vector<orion::core::FanSensor> fans;
    std::string source;
    std::string reason;
};

[[nodiscard]] SensorSnapshot readSensorSnapshot(const GpuSample& gpu);

[[nodiscard]] inline const orion::core::TemperatureSensor* componentTemperature(
    const SensorSnapshot& snapshot,
    const orion::core::SensorComponent component) noexcept
{
    for (const auto& sensor : snapshot.temperatures) {
        if (sensor.component == component) {
            return &sensor;
        }
    }
    return nullptr;
}

} // namespace orion::platform::detail
