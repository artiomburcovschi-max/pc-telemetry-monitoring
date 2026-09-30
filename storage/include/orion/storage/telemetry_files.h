#pragma once

#include <QJsonObject>
#include <QString>

namespace orion::storage {

// Preview only: the original file is never modified or rotated.
inline constexpr qint64 kLogPreviewBytes = 256 * 1024;
inline constexpr int kLogPreviewLines = 2000;
inline constexpr int kLogPreviewLineCharacters = 4096;

struct LogPreview {
    QString text;
    QString error;
    qint64 fileBytes {0};
    qint64 readBytes {0};
    qint64 omittedPrefixBytes {0};
    int shortenedLines {0};
    bool startsMidLine {false};
    bool changedDuringRead {false};
    bool invalidUtf8 {false};
    [[nodiscard]] QString description() const;
};

[[nodiscard]] LogPreview readLogPreview(const QString& path);
// An explicit suffix is preserved. An empty selection remains cancellation.
[[nodiscard]] QString jsonSnapshotPath(const QString& selection);
// Call only after the UI has confirmed replacement of the final resolved path.
[[nodiscard]] bool saveJsonSnapshot(const QString& path, const QJsonObject& snapshot,
    QString* error = nullptr);

} // namespace orion::storage
