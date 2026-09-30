#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QStringView>
#include <QStringList>

namespace orion::diagnostics {
inline constexpr qint64 kEventXmlBytes = 64 * 1024;
inline constexpr qint64 kEventMessageBytes = 32 * 1024;
inline constexpr qint64 kLogQueryBytes = 2 * 1024 * 1024;
inline constexpr qint64 kLogStderrBytes = 16 * 1024;
inline constexpr qsizetype kLogLineUnits = 2048;

enum class LogBufferAdmission { Accepted, Oversized, Exhausted };
// Units are bytes; the Windows message API returns WCHAR counts, unlike EvtRender.
LogBufferAdmission reserveLogBuffer(quint64 bytes, quint64 maximum, quint64& remaining);
QString boundedLogText(QStringView value, qsizetype maximum, bool& shortened, bool firstLine = false);

struct LogReadStats {
    int examined {0};
    int skipped {0};
    int oversized {0};
    int shortened {0};
    int unformatted {0};
    bool byteLimitReached {false};
    bool recordLimitReached {false};
    quint64 remainingBytes {kLogQueryBytes};
    bool limited() const;
    QJsonObject toJson() const;
    QString note() const;
};
QJsonObject systemLogLimits();
QJsonObject parseJournalctlOutput(const QByteArray& output, int limit, bool outputTruncated = false,
    bool discardIncompleteTail = false);

struct BoundedLogProcessResult {
    bool started {false};
    bool timedOut {false};
    bool truncated {false};
    bool crashed {false};
    int exitCode {-1};
    QByteArray output;
    QByteArray standardError;
    QString error;
};
// No shell, no detached helper. Stdout/stderr drained during execution, not readAll at exit.
BoundedLogProcessResult runBoundedLogProcess(const QString& program, const QStringList& arguments,
    int timeoutMs = 6000, qint64 outputLimit = kLogQueryBytes, qint64 errorLimit = kLogStderrBytes);
QJsonObject journalctlProcessReport(const BoundedLogProcessResult& result, int limit);
} // namespace orion::diagnostics
