#include "orion/core/process_tree_counters.h"
#include "orion/diagnostics/app_monitor_data.h"
#include "orion/diagnostics/runtime_diagnostics.h"
#include "orion/diagnostics/diagnostic_engine.h"
#include <QJsonArray>
#include <iostream>
#include <limits>

int main()
{
    using namespace orion::core;
    using namespace orion::diagnostics;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { passed = false; std::cerr << message << '\n'; }
    };
    ProcessInfo root;
    root.pid = 11; root.creationIdentity = 100;
    root.readBytes = 1000000; root.writeBytes = 2000000; root.pageFaultCount = 5000;
    ProcessTreeCounters counters;
    auto result = counters.update({root}, 1, 0);
    check(!result.readBytesPerSecond && !result.observedReadBytes, "Initial lifetime bytes leaked into monitoring.");
    *root.readBytes += 20; *root.writeBytes += 40; *root.pageFaultCount += 10;
    result = counters.update({root}, 1, 2);
    check(result.readBytesPerSecond == 10 && result.writeBytesPerSecond == 20 && result.faultsPerSecond == 5
        && result.observedReadBytes == 20, "Adjacent counter deltas wrong.");
    auto child = root; child.pid = 22; child.creationIdentity = 200; child.readBytes = 900000000;
    *root.readBytes += 10;
    result = counters.update({root, child}, 2, 3);
    check(!result.readBytesPerSecond && result.observedReadBytes == 30, "New child created lifetime spike.");
    *child.readBytes += 10; *root.readBytes += 10;
    result = counters.update({root, child}, 2, 4);
    check(result.readBytesPerSecond == 20 && result.observedReadBytes == 50, "Stable tree failed to resume.");
    *root.readBytes += 10;
    result = counters.update({root}, 1, 5);
    check(!result.readBytesPerSecond && result.observedReadBytes == 60, "Exited child erased accumulated bytes or fabricated rate.");
    *root.readBytes += 1000000;
    result = counters.update({root}, 1, 6, true);
    check(!result.readBytesPerSecond && result.observedReadBytes == 60, "Pause activity counted.");
    *root.readBytes += 7;
    result = counters.update({root}, 1, 7);
    check(result.readBytesPerSecond == 7 && result.observedReadBytes == 67, "Resume baseline broken.");
    const auto beforeGap = *root.readBytes;
    root.readBytes.reset();
    result = counters.update({root}, 1, 8);
    check(!result.readBytesPerSecond && result.writeBytesPerSecond == 0, "Unknown read broke known write or became zero.");
    root.readBytes = beforeGap + 1000000;
    result = counters.update({root}, 1, 9);
    check(!result.readBytesPerSecond && result.observedReadBytes == 67, "Gap recovery included unobserved bytes.");
    root.readBytes = 1;
    result = counters.update({root}, 1, 10);
    check(!result.readBytesPerSecond, "Counter decrease became a zero rate.");
    root.creationIdentity = 300; root.readBytes = 99999999;
    result = counters.update({root}, 1, 11);
    check(!result.readBytesPerSecond && result.observedReadBytes == 67, "PID reuse included unrelated bytes.");
    result = counters.update({root}, 1, 11);
    check(!result.readBytesPerSecond, "Duplicate time accepted.");
    root.creationIdentity = 0;
    result = counters.update({root}, 1, 12);
    check(!result.readBytesPerSecond, "Unverified identity yielded a rate.");
    ProcessTreeCounters precise;
    root.creationIdentity = 1; root.readBytes = (1ULL << 54);
    (void)precise.update({root}, 1, 0);
    ++*root.readBytes;
    result = precise.update({root}, 1, 1);
    check(result.observedReadBytes == 1 && result.readBytesPerSecond == 1, "64-bit delta lost precision above 2^53.");
    ProcessTreeCounters overflow;
    root.readBytes = 0; child.readBytes = 0;
    (void)overflow.update({root, child}, 2, 0);
    root.readBytes = std::numeric_limits<std::uint64_t>::max(); child.readBytes = 1;
    result = overflow.update({root, child}, 2, 1);
    check(!result.observedReadBytes && !result.readBytesPerSecond, "Overflow fabricated a small rate/total.");

    TelemetryData data;
    const auto utc = QDateTime::fromString("2026-09-22T12:00:00.000Z", Qt::ISODateWithMs);
    const auto epoch = std::chrono::system_clock::time_point(std::chrono::milliseconds(utc.toMSecsSinceEpoch()));
    data.ramAvailablePercent = Metric<double>::valid(50, "fixture-memory", epoch);
    data.ramUsagePercent = Metric<double>::valid(50, "fixture-memory", epoch);
    data.ramTotalBytes = Metric<std::uint64_t>::valid(16ULL * 1024 * 1024 * 1024, "fixture-memory", epoch);
    data.commitUsedPercent = Metric<double>::valid(30, "fixture-commit", epoch);
    data.gpuTemperatureC = Metric<double>::valid(99, "fixture-gpu", epoch - std::chrono::seconds(10));
    const auto envelope = appSystemEnvelope(data, 10000, utc);
    const auto fresh = freshAppSystemSample(envelope, 11000);
    check(fresh.value("system_available_percent").toDouble() == 50 && fresh.value("gpu_temperature_c").isNull()
        && fresh.value("system_stale_fields").toInt() == 1, "Old metric hidden behind fresh envelope.");
    check(fresh.value("system_fields").toObject().value("system_available_percent").toObject().value("source") == "fixture-memory",
        "Metric provenance lost.");
    check(freshAppSystemSample(envelope, 13000).value("system_available_percent").isDouble()
        && freshAppSystemSample(envelope, 13001).value("system_available_percent").isNull(), "Freshness boundary incorrect.");
    const auto stale = freshAppSystemSample(envelope, 20000);
    check(stale.value("system_ram_percent").isNull() && stale.value("paging_activity") == "unknown",
        "Stale data created healthy paging/system measurements.");
    check(freshAppSystemSample(envelope, 9000).value("system_ram_percent").isNull(), "Future producer clock accepted.");
    check(freshAppSystemSample(QJsonObject{{"system_ram_percent", 90}}, 20000).value("system_ram_percent").isNull(),
        "Untimestamped sample accepted.");
    check(freshAppSystemSample(envelope, 11000, 10001).value("system_ram_percent").isNull(), "Pre-boundary cache used after observation.");
    const auto republished = appSystemEnvelope(data, 11000, utc.addMSecs(1000));
    check(freshAppSystemSample(republished, 11500, 10500).value("system_ram_percent").isNull(),
        "Republishing an old metric defeated the after/resume boundary.");
    data.ramUsagePercent.quality = DataQuality::Stale;
    check(freshAppSystemSample(appSystemEnvelope(data, 10000, utc), 10000).value("system_ram_percent").isNull(),
        "Retained stale quality accepted.");
    data.ramUsagePercent = Metric<double>::valid(40, "after-memory", epoch + std::chrono::seconds(1));
    data.ramAvailablePercent = Metric<double>::valid(60, "after-memory", epoch + std::chrono::seconds(1));
    const auto after = freshAppSystemSample(appSystemEnvelope(data, 11000, utc.addMSecs(1000)), 11500, 10500);
    const auto comparison = appMemoryComparison(fresh, after);
    check(comparison.value("data_quality") == "valid" && comparison.value("system_ram_percent_delta").toDouble() == -10,
        "Fresh memory comparison failed.");
    check(appMemoryComparison(fresh, stale).value("data_quality") == "unknown"
        && appMemoryComparison(fresh, fresh).value("data_quality") == "unknown", "Invalid endpoints presented as a comparison.");
    auto row = stale;
    row.insert("elapsed", 1); row.insert("io_total_scope", "observed_intervals_lower_bound");
    const auto summary = summarizeAppMonitorSamples(QJsonArray{row});
    check(summary.value("system_ram_peak_percent").isNull() && summary.value("system_stale_sample_count").toInt() == 1,
        "Summary turned stale data into a peak.");
    const QJsonObject report{{"measurement_contract", "identified_adjacent_intervals_v1"}, {"summary", summary},
        {"runtime_memory", appMemoryComparison(fresh, stale)}, {"io_total_scope", "observed_intervals_lower_bound"}};
    const auto text = appMonitorReportToText(report);
    check(text.contains(QStringLiteral("нижняя граница")) && text.contains(QStringLiteral("Системные снимки до/после: unknown")),
        "Readable report omitted measurement limitations.");
    const auto findings = analyzeSnapshot(QJsonObject{{"app_monitor", report}});
    bool coverage = false;
    for (const auto& value : findings) coverage |= value.toObject().value("id") == "coverage.app_monitor.system_freshness";
    check(coverage, "Freshness gaps absent from verdict coverage.");
    const auto hasFinding = [](const QJsonArray& found, const char* id) {
        for (const auto& value : found) if (value.toObject().value("id") == QLatin1String(id)
            || value.toObject().value("related_findings").toArray().contains(QLatin1String(id))) return true;
        return false;
    };
    QJsonObject diskMoment{{"elapsed", 0}, {"cpu_percent", 10}, {"ram_mb", 100}, {"ram_share_percent", 20},
        {"system_disk_busy_percent", 99}, {"read_mbps", 0}, {"page_faults_per_sec", 0}, {"ui_hung", false},
        {"system_available_percent", 1}, {"system_commit_used_percent", 99}};
    QJsonObject appMoment{{"elapsed", 10}, {"cpu_percent", 10}, {"ram_mb", 100}, {"ram_share_percent", 20},
        {"system_disk_busy_percent", 0}, {"read_mbps", 90}, {"page_faults_per_sec", 9000}, {"ui_hung", true},
        {"system_available_percent", 50}, {"system_commit_used_percent", 30}};
    const auto analyzeRows = [](const QJsonArray& rows) {
        return analyzeSnapshot(QJsonObject{{"app_monitor", QJsonObject{{"measurement_contract", "identified_adjacent_intervals_v1"},
            {"samples", rows}, {"summary", summarizeAppMonitorSamples(rows)}}}});
    };
    const auto separate = analyzeRows(QJsonArray{diskMoment, appMoment});
    check(!hasFinding(separate, "app.io.pressure") && !hasFinding(separate, "app.memory.fault_pressure")
        && !hasFinding(separate, "app.ui.hung_memory_pressure") && hasFinding(separate, "app.ui.hung"),
        "Unrelated session maxima were described as simultaneous.");
    appMoment.insert("system_disk_busy_percent", 99);
    appMoment.insert("system_available_percent", 1);
    appMoment.insert("memory_pressure", "confirmed");
    const auto together = analyzeRows(QJsonArray{diskMoment, appMoment});
    check(hasFinding(together, "app.io.pressure") && hasFinding(together, "app.memory.fault_pressure")
        && hasFinding(together, "app.ui.hung_memory_pressure") && hasFinding(together, "app.memory.pressure_contributor"),
        "Coincident valid observations were ignored.");
    QJsonArray tinyBurst;
    for (int i = 0; i < 2; ++i) tinyBurst.append(QJsonObject{{"elapsed", i}, {"cpu_percent", 97}});
    check(!hasFinding(analyzeRows(tinyBurst), "app.cpu.sustained"),
        "Two-sample CPU burst was reported as a sustained finding.");
    QJsonArray shortWindow;
    for (int i = 0; i < 5; ++i) shortWindow.append(QJsonObject{{"elapsed", i}, {"cpu_percent", 90}});
    const auto shortFindings = analyzeRows(shortWindow);
    const auto findingById = [](const QJsonArray& found, const char* id) -> QJsonObject {
        for (const auto& value : found) if (value.toObject().value("id") == QLatin1String(id)) return value.toObject();
        return {};
    };
    const auto shortCpu = findingById(shortFindings, "app.cpu.sustained");
    check(!shortCpu.isEmpty() && shortCpu.value("confidence") == "medium",
        "Short known window did not cap app.cpu.sustained at medium confidence.");
    QJsonArray longWindow;
    for (int i = 0; i < 40; ++i) longWindow.append(QJsonObject{{"elapsed", i}, {"cpu_percent", 92}});
    const auto longCpu = findingById(analyzeRows(longWindow), "app.cpu.sustained");
    check(!longCpu.isEmpty() && longCpu.value("confidence") == "high"
        && longCpu.value("evidence").toArray().size() == 4,
        "Long known sustained-CPU window lost high confidence or evidence.");
    QJsonArray burstyWindow;
    for (int i = 0; i < 5; ++i) burstyWindow.append(QJsonObject{{"elapsed", i}, {"cpu_percent", 95}});
    for (int i = 5; i < 10; ++i) burstyWindow.append(QJsonObject{{"elapsed", i}, {"cpu_percent", 10}});
    check(!hasFinding(analyzeRows(burstyWindow), "app.cpu.sustained"),
        "A short high-CPU burst inside a mostly-idle window was treated as sustained.");

    std::cout << "App monitor counter/provenance contracts " << (passed ? "passed" : "failed") << ".\n";
    return passed ? 0 : 1;
}
