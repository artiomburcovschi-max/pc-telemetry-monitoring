#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace orion::diagnostics {

// Parser entry points are public so collector behavior can be tested with
// deterministic Python-compatible fixtures without touching real hardware.
[[nodiscard]] QJsonObject parseSmartctlDeviceJson(
    const QByteArray& json,
    const QString& requestedDevice);

[[nodiscard]] QJsonObject parseWindowsEventXml(
    const QString& xml,
    const QString& formattedMessage = {});

[[nodiscard]] QJsonArray summarizeSystemErrorEntries(
    const QJsonArray& entries,
    int maximumGroups = 20);

[[nodiscard]] QJsonObject diffSystemErrorReports(
    const QJsonObject& before,
    const QJsonObject& after);

[[nodiscard]] QDateTime parseSystemErrorTimestamp(const QString& entry);

[[nodiscard]] QJsonObject mergeSystemErrorReports(
    const QJsonObject& first,
    const QJsonObject& second);

[[nodiscard]] QJsonObject filterSystemErrorReportWindow(
    const QJsonObject& report,
    const QString& centerAt,
    double beforeSeconds = 120.0,
    double afterSeconds = 15.0);

[[nodiscard]] QJsonObject collectSmartReport();
[[nodiscard]] QJsonObject collectSystemErrorReport(int limit = 50);

} // namespace orion::diagnostics
