#include "orion/storage/telemetry_files.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringDecoder>
#include <QStringList>

#include <algorithm>

namespace orion::storage {

QString LogPreview::description() const
{
    if (!error.isEmpty()) return error;
    QString result = QStringLiteral("Журнал приложения, не журнал Windows. Размер при открытии: %1 байт. Прочитано: %2 байт.")
        .arg(fileBytes).arg(readBytes);
    if (omittedPrefixBytes > 0 || shortenedLines > 0) {
        result += QStringLiteral(" Ограниченный просмотр последних записей: пропущено в начале %1 байт; укорочено строк: %2.")
            .arg(omittedPrefixBytes).arg(shortenedLines);
    }
    if (startsMidLine) result += QStringLiteral(" Первая строка показана с середины.");
    if (changedDuringRead) result += QStringLiteral(" Размер файла изменился при чтении; снимок может быть неполным.");
    if (invalidUtf8) result += QStringLiteral(" Неполные или неверные UTF-8 символы заменены на �.");
    result += QStringLiteral(" Файл на диске не изменён. Новые записи появятся при повторном открытии.");
    return result;
}

LogPreview readLogPreview(const QString& path)
{
    LogPreview result;
    if (path.isEmpty()) {
        result.error = QStringLiteral("Журнал приложения ещё не инициализирован.");
        return result;
    }
    if (!QFileInfo(path).isFile()) {
        result.error = QStringLiteral("Журнал отсутствует или путь не является обычным файлом.");
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Не удалось открыть журнал: %1").arg(file.errorString());
        return result;
    }
    result.fileBytes = file.size();
    const qint64 start = std::max(qint64(0), result.fileBytes - kLogPreviewBytes);
    // One extra byte identifies whether the bounded window begins at a line boundary.
    const qint64 probeStart = start > 0 ? start - 1 : 0;
    if (!file.seek(probeStart)) {
        result.error = QStringLiteral("Не удалось перейти к последним записям: %1").arg(file.errorString());
        return result;
    }
    auto bytes = file.read(result.fileBytes - probeStart);
    result.readBytes = bytes.size();
    if (file.error() != QFileDevice::NoError) {
        result.error = QStringLiteral("Не удалось прочитать журнал: %1").arg(file.errorString());
        return result;
    }
    result.changedDuringRead = file.size() != result.fileBytes
        || result.readBytes != result.fileBytes - probeStart;
    result.omittedPrefixBytes = start;
    if (start > 0 && !bytes.isEmpty()) {
        const bool boundary = bytes.front() == '\n';
        bytes.remove(0, 1);
        if (!boundary) {
            const auto newline = bytes.indexOf('\n');
            if (newline >= 0 && newline + 1 < bytes.size()) {
                bytes.remove(0, newline + 1);
                result.omittedPrefixBytes += newline + 1;
            } else {
                result.startsMidLine = true;
                // Never introduce a replacement character just by splitting a valid UTF-8 prefix.
                while (!bytes.isEmpty() && (static_cast<unsigned char>(bytes.front()) & 0xc0) == 0x80) {
                    bytes.remove(0, 1);
                    ++result.omittedPrefixBytes;
                }
            }
        }
    }
    int lines = 0;
    for (qsizetype index = bytes.size() - 1; index >= 0; --index) {
        if (bytes[index] == '\n' && index != bytes.size() - 1 && ++lines == kLogPreviewLines) {
            bytes.remove(0, index + 1);
            result.omittedPrefixBytes += index + 1;
            result.startsMidLine = false;
            break;
        }
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder(bytes);
    result.invalidUtf8 = decoder.hasError();
    auto textLines = decoded.split(QLatin1Char('\n'));
    for (auto& line : textLines) {
        if (line.size() <= kLogPreviewLineCharacters) continue;
        auto length = kLogPreviewLineCharacters;
        if (line[length - 1].isHighSurrogate()) --length;
        line.truncate(length);
        line += QStringLiteral(" … [строка укорочена в просмотре]");
        ++result.shortenedLines;
    }
    result.text = textLines.join(QLatin1Char('\n'));
    return result;
}

QString jsonSnapshotPath(const QString& selection)
{
    if (selection.isEmpty() || !QFileInfo(selection).suffix().isEmpty()) return selection;
    return selection + QStringLiteral(".json");
}

bool saveJsonSnapshot(const QString& path, const QJsonObject& snapshot, QString* error)
{
    if (error) error->clear();
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Не выбран файл для сохранения.");
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const auto bytes = QJsonDocument(snapshot).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        if (error) *error = QStringLiteral("Не удалось записать весь отчёт: %1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace orion::storage
