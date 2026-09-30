#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <optional>

namespace orion::diagnostics {

enum class DiagnosticStatus {
    Pass,
    Warning,
    Fail,
    Unknown,
    Skipped,
};

inline constexpr auto kStatusPass = "pass";
inline constexpr auto kStatusWarning = "warning";
inline constexpr auto kStatusFail = "fail";
inline constexpr auto kStatusUnknown = "unknown";
inline constexpr auto kStatusSkipped = "skipped";

inline constexpr auto kQualityValid = "valid";
inline constexpr auto kQualityStale = "stale";
inline constexpr auto kQualityEstimated = "estimated";
inline constexpr auto kQualityUnsupported = "unsupported";
inline constexpr auto kQualityPermissionDenied = "permission_denied";
inline constexpr auto kQualityCollectorError = "collector_error";

struct FindingOptions {
    QString id;
    QString domain {QStringLiteral("general")};
    std::optional<QString> status;
    QString confidence {QStringLiteral("medium")};
    QString urgency {QStringLiteral("monitor")};
    QString impact {QStringLiteral("unknown")};
    int priority {0};
    QString groupKey;
    QString rootCause;
    QString affectedDevice;
    QJsonArray evidence;
    QJsonArray counterEvidence;
    QStringList actions;
    QStringList verificationSteps;
    QStringList sourceChecks;
};

[[nodiscard]] QJsonObject makeEvidence(
    const QString& label,
    QJsonValue value = QJsonValue::Undefined,
    const QString& source = {},
    const QString& quality = QStringLiteral("valid"),
    const QString& observedAt = {});

[[nodiscard]] QJsonObject makeFinding(
    const QString& severity,
    const QString& title,
    const QString& detail,
    const FindingOptions& options = {});

[[nodiscard]] QJsonArray mergeSameGroup(const QJsonArray& findings);
[[nodiscard]] QJsonArray selectVerdictFindings(
    const QJsonArray& findings,
    int maximumItems = 3);
[[nodiscard]] QJsonArray buildActionPlan(
    const QJsonArray& findings,
    int maximumSteps = 6);
[[nodiscard]] QJsonObject buildCoverageSummary(const QJsonArray& findings);
[[nodiscard]] QJsonObject buildRiskAssessment(
    const QJsonArray& findings,
    const QJsonObject& coverage = {},
    const QJsonObject& stressTest = {},
    const QJsonObject& runtime = {});

} // namespace orion::diagnostics
