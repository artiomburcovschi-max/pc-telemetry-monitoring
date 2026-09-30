#pragma once
#include "orion/core/network_counters.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <optional>

namespace orion::diagnostics {
inline constexpr auto kIncidentMeasurementContract = "fresh_incident_observations_v1";
inline constexpr qint64 kIncidentFreshnessMs = 3000;
inline constexpr qint64 kIncidentCoincidenceMs = 1000;
[[nodiscard]] QJsonObject incidentSystemEnvelope(const orion::core::TelemetryData& data,
    const orion::core::NetworkCounterSample& network, qint64 monotonicMs, const QDateTime& utc);
// UI-owned; no OS calls. Capture timestamps share QElapsedTimer's reference clock.
class IncidentSampleBuilder {
public:
    [[nodiscard]] QJsonObject build(const QJsonObject& envelope, qint64 nowMs, qint64 sessionOriginMs,
        qint64 notBeforeMs, const QJsonObject& ping = {}, const QJsonObject& app = {});
private:
    qint64 lastCapture_ {-1};
    QMap<QString, qint64> intervalEnds_;
};
// Only accepted metric timestamps can participate in same-observation evidence.
[[nodiscard]] bool incidentMetricsCoincide(const QJsonObject& sample, const QStringList& keys,
    std::optional<double> additionalTimestamp = std::nullopt);
[[nodiscard]] QJsonObject incidentCoincidences(const QJsonArray& baseline, const QJsonArray& focus);
}
