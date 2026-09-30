#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace orion::diagnostics {

[[nodiscard]] double appMonitorSampleInterval(int durationSeconds) noexcept;
[[nodiscard]] QJsonObject decodeWindowsExitCode(const QJsonValue& exitCode);
[[nodiscard]] QJsonObject summarizeAppMonitorSamples(const QJsonArray& samples);
struct IncidentWindowSamples {
    QJsonArray samples;
    int invalidTimestampCount {0};
    int outsideWindowCount {0};
    int duplicateCount {0};
    int conflictingTimestampCount {0};
    bool parametersValid {false};
};
[[nodiscard]] bool validIncidentWindow(double marker, double pre, double post) noexcept;
[[nodiscard]] IncidentWindowSamples normalizeIncidentSamples(
    const QJsonArray& samples, double marker, double pre, double post);
[[nodiscard]] QJsonObject summarizeIncidentSamples(
    const QJsonArray& samples,
    double markerMonotonic,
    double preSeconds = 60.0,
    double postSeconds = 15.0);
[[nodiscard]] QString appMonitorReportToText(const QJsonObject& report);
[[nodiscard]] QJsonArray appMonitorReasons(const QJsonObject& report);
[[nodiscard]] QString appMonitorVerdictText(const QJsonObject& report);
[[nodiscard]] QString appMonitorOutcomeText(const QJsonObject& report);

} // namespace orion::diagnostics
