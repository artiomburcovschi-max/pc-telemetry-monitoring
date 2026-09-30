#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <functional>

namespace orion::app {

struct HardwareInventoryResult {
    QJsonArray rows;
    bool partial {false};
};

// Null/non-positive/sentinel link rates remain unavailable; values are bits/s.
[[nodiscard]] QJsonValue hardwareLinkMbps(quint64 bitsPerSecond);
[[nodiscard]] HardwareInventoryResult collectHardwareGpus(const QJsonArray& fallback);
[[nodiscard]] HardwareInventoryResult collectHardwareAdapters(const QJsonArray& seed);
[[nodiscard]] QJsonObject collectHardwareDisks(
    QJsonArray physical, QJsonArray volumes, const std::function<bool()>& cancelled);

// Group by explicit native disk numbers, never by array position or model name.
// Unmapped volumes stay in a separate bucket; spanned volumes retain every disk.
[[nodiscard]] QJsonObject groupHardwareDisks(
    const QJsonArray& physical, const QJsonArray& volumes);

} // namespace orion::app
