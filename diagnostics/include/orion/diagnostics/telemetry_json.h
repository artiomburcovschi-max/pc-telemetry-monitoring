#pragma once

#include "orion/core/telemetry_data.h"

#include <QJsonObject>

namespace orion::diagnostics {

inline constexpr int kTelemetrySchemaVersion = 1;

[[nodiscard]] QJsonObject telemetryToJson(const orion::core::TelemetryData& data);

} // namespace orion::diagnostics
