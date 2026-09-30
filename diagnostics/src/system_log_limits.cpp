#include "orion/diagnostics/system_log_limits.h"
#include "orion/diagnostics/system_diagnostics_collector.h"
#include <QDateTime>
#include <QByteArrayView>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTimeZone>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif

namespace orion::diagnostics {
LogBufferAdmission reserveLogBuffer(quint64 bytes, quint64 maximum, quint64& remaining)
{
    if (!bytes || bytes > maximum) return LogBufferAdmission::Oversized;
    if (bytes > remaining) return LogBufferAdmission::Exhausted;
    remaining -= bytes;
    return LogBufferAdmission::Accepted;
}

QString boundedLogText(QStringView value, qsizetype maximum, bool& shortened, bool firstLine)
{
    if (maximum <= 0) { shortened |= !value.isEmpty(); return {}; }
    if (firstLine) {
        while (!value.isEmpty() && (value.front() == u'\r' || value.front() == u'\n')) value = value.sliced(1);
        qsizetype end = 0;
        while (end < value.size() && value[end] != u'\r' && value[end] != u'\n') ++end;
        value = value.first(end);
    }
    const bool cut = value.size() > maximum;
    qsizetype size = cut ? maximum - 1 : value.size();
    if (cut && size > 0 && value[size - 1].isHighSurrogate() && value[size].isLowSurrogate()) --size;
    QString result = value.first(size).toString().simplified();
    if (cut) { result += QChar(0x2026); shortened = true; }
    return result;
}

bool LogReadStats::limited() const
{
    return skipped || oversized || shortened || unformatted || byteLimitReached || recordLimitReached;
}
QJsonObject LogReadStats::toJson() const
{
    return {{"examined", examined}, {"skipped", skipped}, {"oversized", oversized},
        {"shortened", shortened}, {"unformatted", unformatted}, {"byte_limit_reached", byteLimitReached},
        {"record_limit_reached", recordLimitReached}, {"reserved_bytes", qint64(kLogQueryBytes - remainingBytes)}};
}
QString LogReadStats::note() const
{
    if (!limited()) return {};
    return QStringLiteral("Журнал прочитан не полностью: просмотрено %1; пропущено %2; "
        "слишком больших фрагментов %3; сокращено записей %4; без полного сообщения %5%6%7. "
        "Отсутствие записи в этой выборке не подтверждает отсутствие события.")
        .arg(examined).arg(skipped).arg(oversized).arg(shortened).arg(unformatted)
        .arg(byteLimitReached ? QStringLiteral("; достигнут лимит объёма") : QString{})
        .arg(recordLimitReached ? QStringLiteral("; достигнут лимит записей") : QString{});
}
QJsonObject systemLogLimits()
{
    return {{"xml_bytes_per_event", kEventXmlBytes}, {"formatted_message_bytes", kEventMessageBytes},
        {"bytes_per_channel_or_stdout", kLogQueryBytes}, {"stderr_bytes", kLogStderrBytes},
        {"line_utf16_units", kLogLineUnits}, {"max_records_per_channel", 500}};
}

QJsonObject parseJournalctlOutput(const QByteArray& output, int requestedLimit, bool outputTruncated,
    bool discardIncompleteTail)
{
    const int limit = std::clamp(requestedLimit, 1, 500);
    LogReadStats stats;
    const auto size = std::min<qsizetype>(output.size(), kLogQueryBytes);
    const QByteArrayView boundedOutput(output.constData(), size);
    stats.remainingBytes -= size;
    stats.byteLimitReached = outputTruncated || output.size() > size;
    QJsonArray errors;
    qsizetype position = 0;
    while (position < size && stats.examined < limit) {
        const auto newline = boundedOutput.indexOf('\n', position);
        const auto end = newline < 0 || newline >= size ? size : newline;
        const auto rawSize = end - position;
        if (rawSize == 0) { position = end + 1; continue; }
        ++stats.examined;
        if (rawSize > kEventXmlBytes || (end == size && (stats.byteLimitReached || discardIncompleteTail))) {
            ++stats.skipped;
            if (rawSize > kEventXmlBytes) ++stats.oversized;
            position = end + 1; continue;
        }
        const auto document = QJsonDocument::fromJson(output.mid(position, rawSize));
        position = end + 1;
        if (!document.isObject()) { ++stats.skipped; continue; }
        const auto record = document.object();
        const auto microseconds = record.value("__REALTIME_TIMESTAMP").toString().toLongLong();
        const auto time = microseconds > 0 ? QDateTime::fromMSecsSinceEpoch(microseconds / 1000, QTimeZone::UTC) : QDateTime{};
        bool shortened = false;
        const auto identifier = boundedLogText(record.value("SYSLOG_IDENTIFIER")
            .toString(record.value("_COMM").toString("?")), 256, shortened);
        const auto hostname = boundedLogText(record.value("_HOSTNAME").toString(), 256, shortened);
        QString message;
        if (record.value("MESSAGE").isString())
            message = boundedLogText(record.value("MESSAGE").toString(), kLogLineUnits, shortened, true);
        if (message.isEmpty() || !time.isValid()) ++stats.unformatted;
        if (message.isEmpty()) message = QStringLiteral("[сообщение недоступно]");
        const auto line = boundedLogText(QStringLiteral("%1 %2 %3: %4")
            .arg(time.isValid() ? time.toString(Qt::ISODateWithMs) : QStringLiteral("н/д"), hostname, identifier, message),
            kLogLineUnits, shortened);
        stats.shortened += shortened ? 1 : 0;
        errors.append(line);
    }
    // Reaching -n is not proof that no older events exist; disclose even at an exact boundary.
    stats.recordLimitReached = stats.examined >= limit;
    return {{"errors", errors}, {"note", stats.limited() ? QJsonValue(stats.note())
        : errors.isEmpty() ? QJsonValue(QStringLiteral("ошибок с момента загрузки не найдено")) : QJsonValue(QJsonValue::Null)},
        {"data_quality", stats.limited() ? "estimated" : "valid"}, {"source", "journalctl-json"},
        {"since_boot", true}, {"limit", limit}, {"collection_limited", stats.limited()},
        {"collection_limits", systemLogLimits()}, {"read_stats", stats.toJson()},
        {"groups", summarizeSystemErrorEntries(errors)}};
}

BoundedLogProcessResult runBoundedLogProcess(const QString& program, const QStringList& arguments,
    int timeoutMs, qint64 outputLimit, qint64 errorLimit)
{
    BoundedLogProcessResult result;
    timeoutMs = std::clamp(timeoutMs, 1, 6000);
    outputLimit = std::clamp<qint64>(outputLimit, 0, kLogQueryBytes);
    errorLimit = std::clamp<qint64>(errorLimit, 0, kLogStderrBytes);
    QElapsedTimer clock; clock.start();
    QProcess process;
#ifdef _WIN32
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    process.setProcessChannelMode(QProcess::SeparateChannels);
    const auto drain = [&](QProcess::ProcessChannel channel, QByteArray& retained, qint64 limit) {
        process.setReadChannel(channel);
        while (process.bytesAvailable() > 0 && !result.truncated && !result.timedOut) {
            if (clock.elapsed() >= timeoutMs) { result.timedOut = true; process.kill(); break; }
            const auto room = limit - retained.size();
            const auto chunk = process.read(std::min<qint64>(4096, room + 1));
            if (chunk.isEmpty()) break;
            retained.append(chunk.constData(), std::min<qint64>(chunk.size(), room));
            if (chunk.size() > room) { result.truncated = true; process.kill(); }
        }
    };
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &process,
        [&] { drain(QProcess::StandardOutput, result.output, outputLimit); }, Qt::DirectConnection);
    QObject::connect(&process, &QProcess::readyReadStandardError, &process,
        [&] { drain(QProcess::StandardError, result.standardError, errorLimit); }, Qt::DirectConnection);
    process.start(program, arguments);
    result.started = process.waitForStarted(std::min(timeoutMs, 2000));
    if (!result.started) { result.error = process.errorString(); return result; }
    if (process.state() != QProcess::NotRunning
        && !process.waitForFinished(std::max(1, timeoutMs - int(clock.elapsed())))) {
        result.timedOut = !result.truncated;
        process.kill(); process.waitForFinished(1000);
    }
    drain(QProcess::StandardOutput, result.output, outputLimit);
    drain(QProcess::StandardError, result.standardError, errorLimit);
    result.exitCode = process.exitCode();
    result.crashed = process.exitStatus() != QProcess::NormalExit;
    return result;
}

QJsonObject journalctlProcessReport(const BoundedLogProcessResult& result, int limit)
{
    const bool failed = !result.started || result.timedOut || result.crashed || result.exitCode != 0;
    auto report = parseJournalctlOutput(result.output, limit, result.truncated, failed);
    if (failed || result.truncated || !result.standardError.trimmed().isEmpty()) {
        bool shortened = false;
        QString reason = result.truncated ? QStringLiteral("достигнут предел вывода journalctl")
            : result.timedOut ? QStringLiteral("journalctl превысил тайм-аут")
            : !result.started ? QStringLiteral("journalctl не запущен")
            : failed ? QStringLiteral("journalctl завершился с ошибкой") : QStringLiteral("journalctl сообщил предупреждение");
        const auto detail = boundedLogText(QString::fromUtf8(result.standardError), 1024, shortened, true);
        if (!detail.isEmpty()) reason += QStringLiteral(": %1").arg(detail);
        QString note = QStringLiteral("Журнал прочитан не полностью: %1. Отсутствие записей не подтверждает отсутствие ошибок.").arg(reason);
        if (report.value("collection_limited").toBool()) note += " " + report.value("note").toString();
        report.insert("note", note);
        report.insert("data_quality", failed && !result.truncated && report.value("errors").toArray().isEmpty()
            ? "collector_error" : "estimated");
        report.insert("collection_limited", true);
    }
    report.insert("process", QJsonObject{{"started", result.started}, {"timed_out", result.timedOut},
        {"output_stopped", result.truncated}, {"exit_code", result.exitCode}, {"crashed", result.crashed},
        {"stdout_bytes_retained", result.output.size()}, {"stderr_bytes_retained", result.standardError.size()}});
    return report;
}
} // namespace orion::diagnostics
