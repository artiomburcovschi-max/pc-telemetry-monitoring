#include "orion/diagnostics/system_log_limits.h"
#include "orion/diagnostics/system_diagnostics_collector.h"
#include "orion/diagnostics/diagnostic_engine.h"
#include "orion/diagnostics/report_contract.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <cstdio>
#include <iostream>
#include <limits>
using namespace orion::diagnostics;

namespace {
int failures = 0;
void check(bool ok, const char* message) { if (!ok) { ++failures; std::cerr << message << '\n'; } }
QByteArray record(const QString& message = "fixture event")
{
    return QJsonDocument(QJsonObject{{"__REALTIME_TIMESTAMP", "1790416800000000"},
        {"_HOSTNAME", "fixture-host"}, {"SYSLOG_IDENTIFIER", "fixture"}, {"MESSAGE", message}})
        .toJson(QJsonDocument::Compact) + '\n';
}
int child(const QString& mode)
{
    // This executable is its own finite output fixture. No external program or system log writes.
    if (mode == "timeout") {
        const auto bytes = record(); std::fwrite(bytes.constData(), 1, bytes.size(), stdout); std::fflush(stdout);
        QThread::msleep(3000); return 0;
    }
    FILE* channel = mode == "stderr" ? stderr : stdout;
    const int count = mode == "exact" ? 4096 : 1024 * 1024;
    const QByteArray block(1024, 'X');
    for (int n = 0; n < count / block.size(); ++n)
        if (std::fwrite(block.constData(), 1, block.size(), channel) != size_t(block.size())) break;
    std::fflush(channel); return 0;
}
void parserTests()
{
    quint64 remaining = kLogQueryBytes;
    check(reserveLogBuffer(0, kEventXmlBytes, remaining) == LogBufferAdmission::Oversized, "Zero allocation accepted");
    check(reserveLogBuffer(std::numeric_limits<quint64>::max(), kEventXmlBytes, remaining) == LogBufferAdmission::Oversized,
        "Overflow-sized allocation accepted");
    check(reserveLogBuffer(quint64(0xffffffffU) * 2, kEventMessageBytes, remaining) == LogBufferAdmission::Oversized,
        "WCHAR size multiplication overflowed");
    check(remaining == kLogQueryBytes, "Rejected size consumed budget");
    check(reserveLogBuffer(kEventMessageBytes + 2, kEventMessageBytes, remaining) == LogBufferAdmission::Oversized,
        "Formatted message allocation crossed its own cap");
    for (int n = 0; n < kLogQueryBytes / kEventXmlBytes; ++n)
        check(reserveLogBuffer(kEventXmlBytes, kEventXmlBytes, remaining) == LogBufferAdmission::Accepted, "Exact budget rejected");
    check(remaining == 0 && reserveLogBuffer(2, kEventXmlBytes, remaining) == LogBufferAdmission::Exhausted,
        "Channel aggregate budget not enforced");
    bool clipped = false;
    const auto emoji = QString::fromUtf8("🙂");
    const auto shortened = boundedLogText(QString(2046, 'x') + emoji + "after", 2048, clipped);
    check(clipped && shortened.size() <= 2048 && !shortened.contains(QChar::ReplacementCharacter)
        && !shortened[shortened.size() - 2].isHighSurrogate(), "Surrogate boundary broken");
    clipped = false;
    check(boundedLogText(u"first\r\nsecond", 2048, clipped, true) == "first" && !clipped, "First-line semantics changed");
    const QString xml = "<Event><System><Provider Name='fixture'/><EventID>1</EventID>"
        "<TimeCreated SystemTime='2026-09-26T10:00:00Z'/><Channel>System</Channel></System>"
        "<EventData><Data>A &amp; B</Data></EventData></Event>";
    check(parseWindowsEventXml(xml).value("message").toString() == "A & B", "Ordinary XML escapes rejected");
    const auto longEvent = parseWindowsEventXml(xml, QString(5000, 'm') + emoji);
    check(longEvent.value("shortened").toBool() && longEvent.value("line").toString().size() <= kLogLineUnits,
        "Windows event line escaped cap");
    check(parseWindowsEventXml(QString(kEventXmlBytes / 2 + 1, 'x')).isEmpty(), "Oversized XML accepted");
    check(parseWindowsEventXml("<!DOCTYPE Event [<!ENTITY e 'expansion'>]><Event>&e;</Event>").isEmpty(), "DTD accepted");
    check(parseWindowsEventXml("<Event><System>").isEmpty(), "Malformed XML accepted");

    auto good = parseJournalctlOutput(record("first\nsecond"), 50);
    check(good.value("data_quality") == "valid" && good.value("errors").toArray().size() == 1
        && !good.value("errors").toArray()[0].toString().contains("second"), "Multiline journal event inflated count");
    auto exact = parseJournalctlOutput(record(), 1);
    check(exact.value("collection_limited").toBool() && exact.value("data_quality") == "estimated", "Count boundary not disclosed");
    auto missing = parseJournalctlOutput("{bad json}\n", 50);
    check(missing.value("errors").toArray().isEmpty() && missing.value("data_quality") != "valid"
        && !missing.value("note").toString().contains(QStringLiteral("не найдено")), "Parse failure became clean health");
    auto huge = parseJournalctlOutput(record(QString(70000, 'x')) + record(), 50);
    check(huge.value("errors").toArray().size() == 1 && huge.value("read_stats").toObject().value("oversized").toInt() == 1,
        "Oversized journal record lost subsequent valid event");
    auto message = parseJournalctlOutput(record(QString(5000, 'm')), 50);
    check(message.value("data_quality") == "estimated" && message.value("errors").toArray()[0].toString().size() <= kLogLineUnits,
        "Journal message escaped line cap");
    auto boundary = parseJournalctlOutput(record(QString(kEventXmlBytes - record("").size() + 1, 'x')), 50);
    check(boundary.value("errors").toArray().size() == 1
        && boundary.value("read_stats").toObject().value("oversized").toInt() == 0, "Exact JSON record byte boundary rejected");
    QByteArray many;
    for (int i = 0; i < 600; ++i) many += record();
    auto clamped = parseJournalctlOutput(many, 10000);
    check(clamped.value("errors").toArray().size() == 500 && clamped.value("limit").toInt() == 500,
        "Maximum event count escaped clamp");
    const auto large = record() + QByteArray(kLogQueryBytes + 100, 'x');
    auto limited = parseJournalctlOutput(large, 500);
    check(limited.value("errors").toArray().size() == 1 && limited.value("read_stats").toObject().value("reserved_bytes").toInteger() == kLogQueryBytes
        && limited.value("collection_limited").toBool(), "Aggregate stdout bound lost complete prefix");
    auto noNewline = record(); noNewline.chop(1);
    check(parseJournalctlOutput(noNewline, 50).value("errors").toArray().size() == 1, "Normal final JSON without newline lost");
    check(parseJournalctlOutput(noNewline, 50, true).value("errors").toArray().isEmpty(), "Incomplete final JSON accepted at cutoff");
    auto unsupportedMessage = parseJournalctlOutput("{\"MESSAGE\":[65,66]}\n", 50);
    check(unsupportedMessage.value("data_quality") == "estimated", "Missing/unsupported fields became reliable");

    auto delta = diffSystemErrorReports(limited, limited);
    check(delta.value("collection_limited").toBool() && delta.value("data_quality") == "estimated"
        && delta.value("note").toString().contains(QStringLiteral("доступная часть")), "Diff lost incomplete provenance");
    auto merged = mergeSystemErrorReports(good, limited);
    auto filtered = filterSystemErrorReportWindow(merged, "2026-09-26T10:00:00Z", 100000, 100000);
    check(filtered.value("collection_limited").toBool() && filtered.value("note").toString().contains(QStringLiteral("не полностью")),
        "Window/merge lost partial-read warning");
    const QJsonObject snapshot{{"diagnostics", QJsonObject{{"log_errors", limited}}}};
    const auto report = buildReport(snapshot);
    check(reportToText(report).contains(limited.value("note").toString()), "TXT dropped note when entries exist");
    bool coverage = false;
    for (const auto& finding : analyzeSnapshot(snapshot))
        coverage |= finding.toObject().value("id") == "coverage.system_log";
    check(coverage, "Partial log did not disclose missing diagnostic coverage");
    auto contradictory = good; contradictory.insert("collection_limited", true);
    check(diffSystemErrorReports(contradictory, good).value("data_quality") == "estimated", "Limited flag allowed reliable diff");
}
void processTests()
{
    const auto exe = QCoreApplication::applicationFilePath();
    auto exact = runBoundedLogProcess(exe, {"--child", "exact"}, 2000, 4096, 1024);
    check(exact.started && !exact.timedOut && !exact.truncated && !exact.crashed && exact.output.size() == 4096,
        "Exact output limit falsely truncated");
    for (const auto* channel : {"stdout", "stderr"}) {
        QElapsedTimer clock; clock.start();
        const auto huge = runBoundedLogProcess(exe, {"--child", channel}, 2000, 4096, 1024);
        check(huge.started && huge.truncated && huge.output.size() <= 4096 && huge.standardError.size() <= 1024
            && clock.elapsed() < 2500, "Child output not promptly bounded/stopped");
    }
    const auto timeout = runBoundedLogProcess(exe, {"--child", "timeout"}, 300);
    check(timeout.started && timeout.timedOut && !timeout.truncated, "Timeout confused with byte cutoff");
    const auto partial = journalctlProcessReport(timeout, 50);
    check(partial.value("errors").toArray().size() == 1 && partial.value("data_quality") == "estimated"
        && !partial.value("read_stats").toObject().value("byte_limit_reached").toBool(), "Timeout lost prefix/misreported byte limit");
    auto failure = runBoundedLogProcess(exe + ".missing-fixture", {}, 200);
    check(!failure.started && journalctlProcessReport(failure, 50).value("data_quality") == "collector_error", "Missing child became clean log");
    BoundedLogProcessResult warning; warning.started = true; warning.exitCode = 0;
    warning.output = record(); warning.standardError = "fixture access warning";
    check(journalctlProcessReport(warning, 50).value("collection_limited").toBool(), "stderr warning silently discarded");
    auto noBytes = runBoundedLogProcess(exe, {"--child", "exact"}, 2000, 0, 0);
    check(noBytes.truncated && noBytes.output.isEmpty() && noBytes.standardError.isEmpty(), "Zero byte cap retained output");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == "--child") return child(app.arguments()[2]);
    parserTests(); processTests();
    std::cout << "System log limits failures: " << failures << '\n';
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
