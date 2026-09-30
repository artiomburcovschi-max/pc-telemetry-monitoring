#pragma once

#include "orion/core/telemetry_data.h"

#include <chrono>
#include <memory>
#include <string_view>

namespace orion::platform {

class SystemBackend {
public:
    virtual ~SystemBackend() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual orion::core::TelemetryData sample(
        std::chrono::milliseconds interval) = 0;
};

[[nodiscard]] std::unique_ptr<SystemBackend> makeSystemBackend();

} // namespace orion::platform

