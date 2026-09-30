#include "orion/storage/telemetry_files.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << message << '\n'; }
}
void put(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "Fixture open failed");
    check(file.write(bytes) == bytes.size(), "Fixture write failed");
}
QByteArray get(const QString& path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Result open failed");
    return file.readAll();
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using namespace orion::storage;
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const auto path = temporary.filePath(QStringLiteral("снимок журнала.json"));
    check(jsonSnapshotPath({}).isEmpty(), "Cancelled path acquired an extension");
    check(jsonSnapshotPath(temporary.filePath("name")) == temporary.filePath("name.json"), "Missing suffix not supplied");
    check(jsonSnapshotPath("name.JSON") == "name.JSON", "Uppercase suffix was doubled");
    check(jsonSnapshotPath("name.custom") == "name.custom", "Explicit suffix changed");
    QJsonObject payload{{"text", QStringLiteral("Русский текст 🚀")}, {"unknown", QJsonValue::Null},
        {"zero", 0}, {"report", QJsonObject{{"source", "fixture-only"}, {"errors", QJsonArray{"test"}}}}};
    QString error = "old error";
    check(saveJsonSnapshot(path, payload, &error) && error.isEmpty(), "UTF-8 atomic save failed");
    check(QJsonDocument::fromJson(get(path)).object() == payload, "Snapshot roundtrip changed values");
    payload.insert("generation", 2);
    check(saveJsonSnapshot(path, payload, &error), "Confirmed replacement failed");
    check(QJsonDocument::fromJson(get(path)).object() == payload, "Replacement was not complete JSON");
    check(!saveJsonSnapshot({}, payload, &error) && !error.isEmpty(), "Empty path save succeeded");
    check(!saveJsonSnapshot(temporary.filePath("missing/report.json"), payload, &error) && !error.isEmpty(),
        "Missing directory failure not reported");
    check(!saveJsonSnapshot(temporary.path(), payload, &error) && !error.isEmpty(), "Directory target save succeeded");
    check(QDir(temporary.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size() == 1,
        "Atomic save left temporary files");
    check(QJsonDocument::fromJson(get(path)).object() == payload, "Failed save damaged previous report");

    const auto log = temporary.filePath(QStringLiteral("журнал.log"));
    check(!readLogPreview({}).error.isEmpty(), "Uninitialized journal not reported");
    check(!readLogPreview(log).error.isEmpty(), "Missing journal not reported");
    check(!readLogPreview(temporary.path()).error.isEmpty(), "Directory journal accepted");
    put(log, {});
    auto preview = readLogPreview(log);
    check(preview.error.isEmpty() && preview.text.isEmpty() && preview.fileBytes == 0, "Empty journal failed");
    const QByteArray small = QStringLiteral("ТЕСТ — <b>не HTML</b>\r\n🚀\n").toUtf8();
    put(log, small);
    preview = readLogPreview(log);
    check(preview.text == QString::fromUtf8(small) && !preview.invalidUtf8
        && preview.omittedPrefixBytes == 0 && preview.readBytes == small.size(), "Small UTF-8/CRLF log changed");

    QByteArray many;
    for (int index = 1; index <= kLogPreviewLines; ++index) many += QByteArray::number(index) + '\n';
    put(log, many);
    preview = readLogPreview(log);
    check(preview.text.startsWith("1\n") && preview.omittedPrefixBytes == 0, "Exact line limit lost a line");
    many += "2001\n";
    put(log, many);
    preview = readLogPreview(log);
    check(preview.text.startsWith("2\n") && preview.text.endsWith("2001\n")
        && preview.omittedPrefixBytes == 2, "Last-line limit did not keep exactly newest 2000 lines");
    many.chop(1);
    put(log, many);
    check(readLogPreview(log).text.startsWith("2\n"), "Unterminated last line altered count");

    QByteArray large(4 * 1024 * 1024, 'x');
    large += "\nTEST-FIXTURE-END\n";
    put(log, large);
    preview = readLogPreview(log);
    check(preview.text == "TEST-FIXTURE-END\n" && preview.readBytes <= kLogPreviewBytes + 1
        && preview.omittedPrefixBytes == 4 * 1024 * 1024 + 1
        && preview.description().contains(QStringLiteral("Ограниченный")), "Large log was not bounded/labeled");
    check(get(log) == large, "Preview changed original file");

    const auto exact = QByteArray(kLogPreviewBytes - 5, 'x') + "\nend\n";
    put(log, QByteArray("prefix\n") + exact);
    preview = readLogPreview(log);
    check(preview.omittedPrefixBytes == 7 && !preview.startsMidLine && preview.shortenedLines == 1
        && preview.text.endsWith("\nend\n"), "Window exactly at newline lost the first full line");

    QByteArray unicode;
    const auto emoji = QStringLiteral("🚀").toUtf8();
    for (int i = 0; i < 100000; ++i) unicode += emoji;
    unicode += "x"; // bounded start lands inside a multibyte character
    put(log, unicode);
    preview = readLogPreview(log);
    check(preview.startsMidLine && !preview.invalidUtf8 && preview.shortenedLines == 1
        && preview.text.size() < kLogPreviewLineCharacters + 100
        && !preview.text.contains(QChar::ReplacementCharacter), "Huge single UTF-8 line not bounded or split safely");
    put(log, QByteArray("invalid\xff\nunfinished\xe2\x82"));
    preview = readLogPreview(log);
    check(preview.invalidUtf8 && preview.text.contains(QChar::ReplacementCharacter), "Invalid/incomplete UTF-8 not disclosed");
    if (failures) return 1;
    std::cout << "Telemetry atomic files and bounded log preview contracts passed.\n";
    return 0;
}
