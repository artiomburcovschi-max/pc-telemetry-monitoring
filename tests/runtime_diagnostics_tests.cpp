#include "orion/diagnostics/runtime_diagnostics.h"
#include "orion/diagnostics/diagnostic_engine.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

bool closeEnough(const double left, const double right, const double tolerance = 0.01)
{
    return std::abs(left - right) <= tolerance;
}

} // namespace

int main()
{
    using namespace orion::diagnostics;
    if (appMonitorSampleInterval(60) != 1.0
        || appMonitorSampleInterval(30 * 60) != 1.0
        || appMonitorSampleInterval(31 * 60) != 2.0
        || appMonitorSampleInterval(120 * 60) != 2.0
        || appMonitorSampleInterval(121 * 60) != 5.0) {
        std::cerr << "Duration-aware app-monitor interval contract failed.\n";
        return EXIT_FAILURE;
    }

    QJsonArray appSamples;
    for (int index = 0; index < 8; ++index) {
        appSamples.append(QJsonObject {
            {QStringLiteral("elapsed"), index * 10.0},
            {QStringLiteral("sample_interval"), 1.0},
            {QStringLiteral("cpu_percent"), 50.0 + index * 10.0},
            {QStringLiteral("ram_mb"), 500.0 + index * 20.0},
            {QStringLiteral("private_mb"), 300.0 + index * 30.0},
            {QStringLiteral("handle_count"), 100 + index * 5},
            {QStringLiteral("process_count"), 2},
            {QStringLiteral("thread_count"), 12},
            {QStringLiteral("read_total_mb"), index * 2.0},
            {QStringLiteral("write_total_mb"), index * 3.0},
            {QStringLiteral("system_disk_busy_percent"), 10.0 + index},
            {QStringLiteral("system_disk_read_latency_ms"), 1.0 + index},
            {QStringLiteral("system_disk_write_latency_ms"), 2.0 + index},
            {QStringLiteral("context_switches_per_sec"), 100.0 + index},
            {QStringLiteral("system_context_switches_per_sec"), 1000.0 + index * 10.0},
            {QStringLiteral("ui_hung"), index == 5 || index == 6},
            {QStringLiteral("paging_activity"), QStringLiteral("idle")},
        });
    }
    const auto appSummary = summarizeAppMonitorSamples(appSamples);
    if (appSummary.value(QStringLiteral("sample_count")).toInt() != 8
        || !closeEnough(appSummary.value(QStringLiteral("cpu_peak_percent")).toDouble(), 120.0)
        || appSummary.value(QStringLiteral("private_growth_mb")).toDouble() < 200.0
        || appSummary.value(QStringLiteral("handle_growth")).toDouble() != 35.0
        || appSummary.value(QStringLiteral("ui_hung_fraction")).toDouble() != 0.25
        || appSummary.value(QStringLiteral("system_disk_read_latency_peak_ms")).toDouble() != 8.0
        || appSummary.value(QStringLiteral("system_context_switches_peak_per_sec")).toDouble() != 1070.0) {
        std::cerr << "App-monitor trend summary contract failed.\n";
        return EXIT_FAILURE;
    }

    const auto makeTrend = [](const QJsonArray& data, bool sameTime = false) {
        QJsonArray rows;
        for (qsizetype i = 0; i < data.size(); ++i)
            rows.append(QJsonObject{{"elapsed", sameTime ? 0 : i}, {"ram_mb", data[i]},
                {"private_mb", data[i]}, {"handle_count", data[i]}});
        return summarizeAppMonitorSamples(rows);
    };
    const auto constant = makeTrend({0, 0, 0});
    const auto nonlinear = makeTrend({1, 3, 2, 5});
    const auto missingTrend = makeTrend({1, QJsonValue::Null, 3, 2, 5});
    const auto single = makeTrend({42});
    const auto absent = makeTrend({QJsonValue::Null, QJsonValue::Null});
    const auto equalTimes = makeTrend({5, 5, 5}, true);
    for (const auto& key : {"ram_growth_r2", "private_growth_r2", "handle_growth_r2"}) {
        if (constant.value(QLatin1String(key)).toDouble(-1) != 1.0
            || !closeEnough(nonlinear.value(QLatin1String(key)).toDouble(), 0.6914, 0.00001)
            || !single.value(QLatin1String(key)).isNull() || !absent.value(QLatin1String(key)).isNull()
            || !equalTimes.value(QLatin1String(key)).isNull()) {
            std::cerr << "Constant/absent/degenerate trend R2 contract failed.\n"; return EXIT_FAILURE;
        }
    }
    if (constant.value("ram_growth_mb_per_min").toDouble(-1) != 0
        || constant.value("ram_growth_positive_fraction").toDouble(-1) != 0
        || nonlinear.value("ram_growth_positive_fraction").toDouble() != 0.667
        || nonlinear.value("ram_growth_step_count").toInt() != 3
        || missingTrend.value("ram_growth_positive_fraction").toDouble() != 0.667
        || missingTrend.value("ram_growth_fraction_scope") != "adjacent_known_values"
        || !single.value("ram_growth_positive_fraction").isNull()
        || !absent.value("ram_growth_positive_fraction").isNull()
        || appSummary.value("ram_growth_positive_fraction").toDouble() != 1.0) {
        std::cerr << "Known-value growth fraction invented steps or lost source semantics.\n"; return EXIT_FAILURE;
    }
    struct ExitCase { qint64 code; const char* name; const char* category; };
    const ExitCase exitCases[] {
        {0xC0000005LL, "ACCESS_VIOLATION", "memory_access"},
        {0xC0000017LL, "NO_MEMORY", "out_of_memory"},
        {0xC0000094LL, "INTEGER_DIVIDE_BY_ZERO", "application_fault"},
        {0xC0000096LL, "PRIVILEGED_INSTRUCTION", "application_fault"},
        {0xC00000FDLL, "STACK_OVERFLOW", "stack_overflow"},
        {0xC0000374LL, "HEAP_CORRUPTION", "heap_corruption"},
        {0xC0000409LL, "STACK_BUFFER_OVERRUN / FAIL_FAST", "fail_fast"},
        {0xC0000420LL, "ASSERTION_FAILURE", "application_fault"},
        {0xE06D7363LL, "MICROSOFT_CPP_EXCEPTION", "application_exception"},
        {0x40000015LL, "FATAL_APP_EXIT", "application_fault"},
    };
    for (const auto& expected : exitCases) {
        const auto decoded = decodeWindowsExitCode(expected.code);
        if (decoded.value("unsigned").toInteger() != expected.code
            || decoded.value("name") != QLatin1String(expected.name)
            || decoded.value("category") != QLatin1String(expected.category)) {
            std::cerr << "Windows exit-code mapping differs from the reference.\n"; return EXIT_FAILURE;
        }
    }
    const auto signedExit = decodeWindowsExitCode(qint64(-1073741819));
    if (signedExit.value("hex") != "0xC0000005" || signedExit.value("name") != "ACCESS_VIOLATION"
        || signedExit.value("code").toInteger() != -1073741819
        || !decodeWindowsExitCode(37).value("name").isNull()
        || decodeWindowsExitCode(0).value("hex") != "0x00000000") {
        std::cerr << "Signed/unknown/zero exit-code handling failed.\n"; return EXIT_FAILURE;
    }
    for (const QJsonValue& invalid : {QJsonValue(), QJsonValue("37"), QJsonValue(1.5),
            QJsonValue(4294967296.0), QJsonValue(-2147483649.0)}) {
        if (!decodeWindowsExitCode(invalid).value("hex").isNull()) {
            std::cerr << "Malformed exit code was truncated into a Windows status.\n"; return EXIT_FAILURE;
        }
    }

    const auto unknownSummary = summarizeAppMonitorSamples(QJsonArray{QJsonObject{{"elapsed", 1}, {"ui_hung", QJsonValue::Null}}});
    QJsonObject failedReport{{"summary", unknownSummary}, {"launch_error", "fixture error"},
        {"termination", QJsonObject{{"errors", QJsonArray{"fixture denied"}}, {"survivor_pids", QJsonArray{123}}}},
        {"action_plan", QJsonArray{QJsonObject{{"action", "fixture action"}}}}};
    const auto failedText = appMonitorReportToText(failedReport);
    if (!unknownSummary.value("ui_hung_fraction").isNull() || !failedText.contains("fixture error")
        || !failedText.contains("fixture denied") || !failedText.contains("fixture action")
        || !failedText.contains(QStringLiteral("Окно без отклика: н/д"))) {
        std::cerr << "Report lifecycle/error/unknown-value contract failed.\n"; return EXIT_FAILURE;
    }

    QJsonObject richReport{{"summary", appSummary}, {"verdict", QStringLiteral("есть подозрительные признаки")},
        {"exit_code", qint64(3221225477)},
        {"system_errors_checked_after", false},
        {"new_system_errors", QJsonArray{QJsonObject{{"time", "event-time"}, {"provider", "event-provider"},
            {"id", 0}, {"text", "event-message"}}}},
        {"findings", QJsonArray{QJsonObject{{"title", "finding-title"}, {"detail", "finding-detail"},
            {"severity", "warn"}, {"confidence", "high"}, {"evidence", QJsonArray{QJsonObject{
                {"metric", "evidence-metric"}, {"value", 0}, {"source", "evidence-source"}}}}}}},
        {"termination", QJsonObject{{"close_messages_sent", 2}, {"killed_pids", QJsonArray{456}}}},
        {"coverage", QJsonObject{{"message", "fixture-coverage"}}}};
    richReport.insert("verdict_findings", richReport.value("findings"));
    richReport.insert("exit_details", decodeWindowsExitCode(richReport.value("exit_code")));
    const auto richText = appMonitorReportToText(richReport);
    for (const auto& expected : {QStringLiteral("finding-detail"), QStringLiteral("evidence-metric: 0"),
        QStringLiteral("evidence-source"), QStringLiteral("event-time · event-provider · ID 0 · event-message"),
        QStringLiteral("0xc0000005 (3221225477)"), QStringLiteral("ACCESS_VIOLATION"),
        QStringLiteral("не доказывает причину"), QStringLiteral("Доля переходов с ростом Working set: 100.0%"),
        QStringLiteral("WM_CLOSE: 2"), QStringLiteral("killed_pids: 456"),
        QStringLiteral("ПИКОВЫЕ МОМЕНТЫ"), QStringLiteral("CPU (%): 120.0; +70.0 сек"), QStringLiteral("fixture-coverage"),
        QStringLiteral("Private memory start/end/peak: 300.0 / 510.0 / 510.0 МБ")}) {
        if (!richText.contains(expected)) { std::cerr << "Missing detailed report field: " << expected.toStdString() << '\n'; return EXIT_FAILURE; }
    }
    if (!unknownSummary.value("paging_active_fraction").isNull()
        || !appMonitorVerdictText(richReport).contains("finding-detail")
        || !appMonitorVerdictText(failedReport).contains(QStringLiteral("Недостаточно"))) {
        std::cerr << "Unknown paging/reason contract failed.\n"; return EXIT_FAILURE;
    }
    const auto topSummary = summarizeAppMonitorSamples(QJsonArray{
        QJsonObject{{"elapsed", 0}, {"top_processes", QJsonArray{QJsonObject{{"pid", 321}}}}},
        QJsonObject{{"elapsed", 1}, {"top_processes", QJsonArray{}}}});
    if (topSummary.value("top_processes_last").toArray().first().toObject().value("pid").toInt() != 321) {
        std::cerr << "Final empty sample erased last process evidence.\n"; return EXIT_FAILURE;
    }

    QJsonArray incidentSamples;
    for (int second = 40; second <= 115; ++second) {
        const bool focus = second >= 90 && second <= 105;
        incidentSamples.append(QJsonObject {
            {QStringLiteral("monotonic"), second},
            {QStringLiteral("observed_at"), QStringLiteral("2026-08-30T12:00:%1").arg(second)},
            {QStringLiteral("cpu_usage_percent"), focus ? 90.0 : 20.0},
            {QStringLiteral("cpu_freq_mhz"), focus ? 3000.0 : 4000.0},
            {QStringLiteral("cpu_temp_c"), focus ? 95.0 : 55.0},
            {QStringLiteral("ram_used_percent"), focus ? 96.0 : 60.0},
            {QStringLiteral("ram_available_percent"), focus ? 4.0 : 40.0},
            {QStringLiteral("paging_activity"), focus ? QStringLiteral("active") : QStringLiteral("idle")},
            {QStringLiteral("paging_rate_quality"), QStringLiteral("valid")},
            {QStringLiteral("net_errors_delta"), focus ? QJsonValue{2} : QJsonValue{QJsonValue::Null}},
            {QStringLiteral("net_drops_delta"), focus ? QJsonValue{3} : QJsonValue{QJsonValue::Null}},
        });
    }
    const auto incident = summarizeIncidentSamples(incidentSamples, 100.0, 60.0, 15.0);
    if (incident.value(QStringLiteral("data_quality")).toString() != QStringLiteral("valid")
        || incident.value(QStringLiteral("baseline_sample_count")).toInt() != 50
        || incident.value(QStringLiteral("focus_sample_count")).toInt() != 16
        || incident.value(QStringLiteral("focus_memory_pressure")).toString() != QStringLiteral("confirmed")
        || incident.value(QStringLiteral("focus_net_error_count")).toInt() != 32
        || incident.value(QStringLiteral("focus_net_drop_count")).toInt() != 48
        || incident.value(QStringLiteral("cpu_frequency_drop_percent")).toDouble() < 24.0) {
        std::cerr << "Incident baseline/focus/recovery contract failed.\n";
        return EXIT_FAILURE;
    }

    QJsonArray unorderedSamples;
    const double unorderedValues[] {95, 20, 55, 10, 80};
    for (int i = 0; i < 5; ++i) unorderedSamples.append(QJsonObject{
        {"monotonic", 96 + i}, {"cpu_usage_percent", unorderedValues[i]},
        {"app", QJsonObject{{"cpu_percent", unorderedValues[i]}}}});
    const auto unorderedSummary = summarizeIncidentSamples(unorderedSamples, 100);
    for (const auto& stat : {unorderedSummary.value("focus").toObject().value("cpu_usage_percent").toObject(),
            unorderedSummary.value("app").toObject().value("cpu_percent").toObject()}) {
        if (stat.value("min").toDouble(-1) != 10 || stat.value("max").toDouble(-1) != 95
            || stat.value("last").toDouble(-1) != 80 || stat.value("p95").toDouble(-1) != 95
            || stat.value("avg").toDouble(-1) != 52 || stat.value("above_90_fraction").toDouble(-1) != 0.2) {
            std::cerr << "Unsorted incident statistics lost extrema/last/fraction.\n"; return EXIT_FAILURE;
        }
    }
    const auto boundarySummary = summarizeIncidentSamples(QJsonArray{
        QJsonObject{{"monotonic", 89.9999999}, {"cpu_freq_mhz", 4000}},
        QJsonObject{{"monotonic", 90}, {"cpu_freq_mhz", 3000}},
        QJsonObject{{"monotonic", 105}, {"cpu_freq_mhz", 3500}},
        QJsonObject{{"monotonic", 105.0000001}, {"cpu_freq_mhz", 3900}}}, 100);
    if (boundarySummary.value("baseline_sample_count").toInt() != 1
        || boundarySummary.value("focus_sample_count").toInt() != 2
        || boundarySummary.value("recovery_sample_count").toInt() != 1) {
        std::cerr << "Incident boundary dropped or double-counted sub-microsecond sample.\n"; return EXIT_FAILURE;
    }
    const auto emptyStats = summarizeIncidentSamples({}, 100).value("focus").toObject()
        .value("cpu_usage_percent").toObject();
    if (!emptyStats.value("above_90_fraction").isNull() || !emptyStats.value("min").isNull()) {
        std::cerr << "Absent incident values became zero.\n"; return EXIT_FAILURE;
    }
    const auto spikeSummary = summarizeIncidentSamples(QJsonArray{
        QJsonObject{{"monotonic", 98}, {"net_ping_ms", 350}},
        QJsonObject{{"monotonic", 99}, {"net_ping_ms", 20}},
        QJsonObject{{"monotonic", 100}, {"net_ping_ms", 10}}}, 100);
    bool spikeFinding = false;
    for (const auto& finding : analyzeSnapshot(QJsonObject{{"incident", QJsonObject{{"summary", spikeSummary}}}}))
        spikeFinding |= finding.toObject().value("id") == "incident.network.degradation";
    if (!spikeFinding) {
        std::cerr << "Sorted-statistics regression hid incident network spike from diagnostics.\n"; return EXIT_FAILURE;
    }
    std::cout << "Runtime diagnostics contracts passed.\n";
    return EXIT_SUCCESS;
}
