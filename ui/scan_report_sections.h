#pragma once

#include <QJsonObject>
#include <QString>

namespace orion::app
{

// Shared extended text used by both deep and full scans. The report remains
// structured JSON internally, while copied/saved text is intended for people.
[[nodiscard]] QString formatExtendedScanSections(const QJsonObject& report);

} // namespace orion::app
