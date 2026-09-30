#pragma once

#include <QJsonArray>
#include <QJsonObject>

namespace orion::diagnostics {

// Accepts the language-neutral snapshot used by
// tests/fixtures/diagnostic_golden_cases.json.
[[nodiscard]] QJsonArray analyzeSnapshot(const QJsonObject& snapshot);

} // namespace orion::diagnostics
