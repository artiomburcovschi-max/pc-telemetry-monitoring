#pragma once
#include "orion/core/telemetry_data.h"
#include <QDateTime>
#include <QJsonObject>

namespace orion::diagnostics {
inline constexpr qint64 kAppSystemFreshnessMs = 3000;
// Capture metric provenance in the producer thread, before delivery to the UI.
[[nodiscard]] QJsonObject appSystemEnvelope(const orion::core::TelemetryData& data,
    qint64 monotonicMs, const QDateTime& nowUtc);
// Re-evaluate age at consumption. Untimestamped, old or pre-boundary data is null.
[[nodiscard]] QJsonObject freshAppSystemSample(const QJsonObject& envelope,
    qint64 nowMonotonicMs, qint64 notBeforeMonotonicMs = -1);
[[nodiscard]] QJsonObject appMemoryComparison(const QJsonObject& before, const QJsonObject& after);
}
