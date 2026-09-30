#include "incident_worker.h"
#include "orion/diagnostics/runtime_diagnostics.h"
#include "orion/diagnostics/report_contract.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QTimer>
#include <cmath>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace orion::app;
using namespace orion::diagnostics;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << message << '\n'; }
}
QJsonObject log(QJsonArray entries = {}) { return {{"errors", entries}, {"data_quality", "valid"}}; }
IncidentOptions options(double post = 0.1) {
    return {"fixture", "2026-09-22T12:00:00.000Z", 100, 60, post, std::nullopt};
}
QJsonObject capture(IncidentWorker& worker, const IncidentOptions& opts,
                    std::function<void(QEventLoop&)> arrange = {}) {
    QEventLoop loop;
    QJsonObject result;
    QObject::connect(&worker, &IncidentWorker::captureReady, &loop,
        [&](const QJsonObject& value) { result = value; });
    QObject::connect(&worker, &QThread::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(2500, &loop, [&] { worker.cancel(); loop.quit(); });
    worker.capture(opts);
    if (arrange) arrange(loop);
    loop.exec();
    worker.cancel();
    check(worker.wait(1500), "Fixture worker did not finish within its bound.");
    check(!result.isEmpty(), "Missing incident completion.");
    return result;
}
void windows() {
    const QJsonObject one{{"monotonic", 100}, {"net_errors_delta", 2}, {"cpu_usage_percent", 20}};
    QJsonArray input{QJsonObject{{"monotonic", 101}, {"net_errors_delta", 3}}, one, one,
        QJsonObject{{"monotonic", 99}, {"cpu_usage_percent", 10}},
        QJsonObject{{"monotonic", 200}, {"cpu_usage_percent", 99}},
        QJsonObject{{"monotonic", 39}}, QJsonObject{{"monotonic", "100"}},
        QJsonObject{{"monotonic", -1}}, QJsonObject{}, false};
    const auto normalized = normalizeIncidentSamples(input, 100, 60, 15);
    const auto summary = summarizeIncidentSamples(input, 100);
    check(normalized.parametersValid && normalized.samples.size() == 3
        && normalized.samples.first().toObject().value("monotonic").toInt() == 99
        && normalized.duplicateCount == 1 && normalized.invalidTimestampCount == 4
        && normalized.outsideWindowCount == 2, "Window filtering/order/deduplication failed.");
    check(summary.value("focus_net_error_count").toInt() == 5
        && summary.value("net_errors_known_sample_count").toInt() == 2
        && summary.value("net_errors_data_quality") == "partial"
        && summary.value("focus").toObject().value("cpu_usage_percent").toObject().value("max").toInt() == 20,
        "Excluded/duplicate observations affected focus statistics or counters.");
    input.append(QJsonObject{{"monotonic", 100}, {"cpu_usage_percent", 95}});
    const auto conflict = normalizeIncidentSamples(input, 100, 60, 15);
    check(conflict.samples.size() == 2 && conflict.conflictingTimestampCount == 1
        && conflict.duplicateCount == 2, "Conflicting timestamp group was not rejected in full.");
    QJsonArray full;
    for (int i = 40; i <= 115; ++i) full.append(QJsonObject{{"monotonic", i}});
    const auto complete = summarizeIncidentSamples(full, 100);
    check(complete.value("data_quality") == "valid" && complete.value("data_quality_scope") == "temporal_window_only"
        && complete.value("leading_gap_seconds").toDouble(-1) == 0
        && complete.value("trailing_gap_seconds").toDouble(-1) == 0
        && complete.value("focus_net_error_count").isNull()
        && complete.value("focus_net_drop_count").isNull()
        && complete.value("net_errors_data_quality") == "unknown", "Temporal coverage invented known network metrics.");
    full.append(full[10]);
    check(summarizeIncidentSamples(full, 100).value("sample_count").toInt() == 76,
        "Exact duplicate inflated coverage.");
    full.append(QJsonObject{{"monotonic", 50}, {"cpu_usage_percent", 1}});
    check(summarizeIncidentSamples(full, 100).value("data_quality") != "valid",
        "Conflicting timestamp was hidden in valid coverage.");
    QJsonArray dense;
    for (int i = 0; i < 100; ++i) dense.append(QJsonObject{{"monotonic", 99 + i * 0.02}});
    const auto edge = summarizeIncidentSamples(dense, 100);
    check(edge.value("data_quality") != "valid" && edge.value("leading_gap_seconds").toDouble() == 59
        && edge.value("trailing_gap_seconds").toDouble() > 14, "Dense central samples hid missing window edges.");
    const auto shortWindow = summarizeIncidentSamples(QJsonArray{one,
        QJsonObject{{"monotonic", 99}}, QJsonObject{{"monotonic", 101}}}, 100, 0, 0);
    check(shortWindow.value("sample_count").toInt() == 1 && shortWindow.value("focus_sample_count").toInt() == 1
        && shortWindow.value("data_quality") == "valid", "Zero-duration window leaked outside focus samples.");
    const auto zero = summarizeIncidentSamples(QJsonArray{QJsonObject{{"monotonic", 100},
        {"net_errors_delta", 0}, {"net_drops_delta", -1}}}, 100, 0, 0);
    check(zero.value("focus_net_error_count").isDouble() && zero.value("focus_net_error_count").toDouble(-1) == 0
        && zero.value("net_errors_data_quality") == "valid" && zero.value("focus_net_drop_count").isNull(),
        "Real zero or invalid negative network counter mishandled.");
    const auto huge = summarizeIncidentSamples(QJsonArray{QJsonObject{{"monotonic", 100},
        {"net_errors_delta", 1e307}}}, 100, 0, 0);
    check(huge.value("focus_net_error_count").toDouble() == 1e307, "Counter rounding overflowed a finite sum.");
    for (const auto params : {std::array<double, 3>{-1, 60, 15}, {100, -1, 15}, {100, 60, 301},
            {std::numeric_limits<double>::infinity(), 60, 15},
            {100, std::numeric_limits<double>::quiet_NaN(), 15}, {1e300, 60, 15}}) {
        const auto invalid = summarizeIncidentSamples(full, params[0], params[1], params[2]);
        check(!invalid.value("window_valid").toBool() && invalid.value("sample_count").toInt(-1) == 0,
            "Invalid incident window accepted.");
    }
    check(summarizeIncidentSamples({}, 100).value("leading_gap_seconds").isNull(),
        "Empty incident fabricated an observed edge gap.");
    const auto text = reportToText(QJsonObject{{"incident", QJsonObject{{"summary", complete},
        {"status", "complete"}, {"timing_contract", "absolute_marker_steady_v1"},
        {"log_pre_seconds", 120}, {"post_seconds", 15}}}});
    check(text.contains(QStringLiteral("ошибки н/д, потери н/д"))
        && text.contains(QStringLiteral("Покрытие относится только к окну времени"))
        && text.contains(QStringLiteral("Окно журнала: 120 сек до / 15 сек после")),
        "Incident text export lost unknown counters/window interpretation.");
}
void workers() {
    int calls = 0;
    IncidentWorker slow(nullptr, [&] { if (++calls == 1) QThread::msleep(250); return log(); });
    auto report = capture(slow, options(0.2));
    check(calls == 2 && report.value("status") == "complete"
        && report.value("post_wait_seconds").toDouble(99) < 0.15
        && report.value("capture_elapsed_seconds").toDouble() >= 0.2,
        "Slow baseline restarted the entire post-marker interval.");
    IncidentWorker normal(nullptr, [] { return log(); });
    report = capture(normal, options(0.15));
    check(report.value("capture_elapsed_seconds").toDouble() >= 0.149
        && report.value("timing_contract") == "absolute_marker_steady_v1"
        && report.value("marker_clock_source") == "capture_entry_steady", "Absolute deadline fired early.");
    auto old = options(1);
    old.markerSteady = Clock::now() - 2s;
    report = capture(normal, old);
    check(report.value("post_wait_seconds").toDouble(99) < 0.15
        && report.value("capture_elapsed_seconds").toDouble() >= 2
        && report.value("marker_clock_source") == "caller_steady", "Explicit marker clock ignored.");
    calls = 0;
    IncidentWorker cancelBefore(nullptr, [&] { ++calls; cancelBefore.cancel(); return log(); });
    report = capture(cancelBefore, options(1));
    check(calls == 1 && report.value("status") == "cancelled" && report.value("log_after").isNull(),
        "Cancellation during baseline still started after collector.");
    calls = 0;
    IncidentWorker cancelWait(nullptr, [&] { ++calls; return log(); });
    report = capture(cancelWait, options(1), [&](QEventLoop& loop) {
        QTimer::singleShot(30, &loop, [&] { cancelWait.cancel(); });
        auto ignored = options(0); ignored.incidentId = "must-not-replace"; cancelWait.capture(ignored);
    });
    check(calls == 1 && report.value("status") == "cancelled" && report.value("incident_id") == "fixture"
        && report.value("capture_elapsed_seconds").toDouble(99) < 0.8, "Wait cancellation or busy capture guard failed.");
    auto restarted = options(0); restarted.incidentId = "restarted";
    report = capture(cancelWait, restarted);
    check(calls == 3 && report.value("status") == "complete" && report.value("incident_id") == "restarted",
        "Restart retained cancellation/request identity.");
    calls = 0;
    IncidentWorker cancelAfter(nullptr, [&] {
        if (++calls == 2) cancelAfter.cancel();
        return log(calls == 2 ? QJsonArray{"2026-09-22T12:00:00Z | fixture event"} : QJsonArray{});
    });
    report = capture(cancelAfter, options(0));
    check(calls == 2 && report.value("status") == "cancelled" && report.value("log_after").isNull()
        && report.value("new_system_errors").toArray().isEmpty(), "Cancelled after query leaked into findings.");
    calls = 0;
    IncidentWorker invalid(nullptr, [&] { ++calls; return log(); });
    for (int i = 0; i < 4; ++i) {
        auto bad = options();
        if (i == 0) bad.postSeconds = -1;
        if (i == 1) bad.markerTimestamp = "not a date";
        if (i == 2) bad.markerSteady = Clock::now() + 10s;
        if (i == 3) bad.markerMonotonic = std::numeric_limits<double>::quiet_NaN();
        check(capture(invalid, bad).value("status") == "invalid", "Invalid capture was not rejected.");
    }
    check(calls == 0, "Invalid request performed system collection.");
    calls = 0;
    IncidentWorker logs(nullptr, [&] {
        QJsonArray entries{"2026-09-22T11:58:30Z | baseline -90", "2026-09-22T11:57:59Z | outside -121"};
        if (++calls == 2) {
            entries.append("2026-09-22T12:00:01Z | included +1");
            entries.append("2026-09-22T12:00:15Z | boundary +15");
            entries.append("2026-09-22T12:00:20Z | outside +20");
            entries.append("unknown timestamp fixture");
        }
        return log(entries);
    });
    auto logOptions = options(15); logOptions.markerSteady = Clock::now() - 16s;
    report = capture(logs, logOptions);
    check(report.value("new_system_errors").toArray().size() == 2
        && report.value("recent_system_errors").toArray().size() == 3
        && report.value("unfiltered_new_system_errors").toArray().size() == 4
        && report.value("new_system_error_correlation").toObject().value("unparseable_count").toInt() == 1,
        "Timestamp filtering lost 120-second context or correlated out-of-window/unknown events.");
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    windows(); workers();
    if (failures) return EXIT_FAILURE;
    std::cout << "Incident timing, cancellation, window and network contracts passed.\n";
    return EXIT_SUCCESS;
}
