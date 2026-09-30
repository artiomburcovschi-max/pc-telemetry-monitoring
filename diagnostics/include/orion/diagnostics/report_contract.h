#pragma once

#include <QJsonObject>
#include <QString>

namespace orion::diagnostics {

inline constexpr int kReportSchemaVersion = 3;

[[nodiscard]] QJsonObject buildReport(const QJsonObject& snapshot);
[[nodiscard]] bool isReportV3(const QJsonObject& report) noexcept;
[[nodiscard]] QString reportToText(const QJsonObject& report);

} // namespace orion::diagnostics
