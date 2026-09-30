#include "orion/diagnostics/incident_data.h"
#include "orion/diagnostics/runtime_diagnostics.h"
#include "orion/diagnostics/diagnostic_engine.h"
#include "orion/diagnostics/report_contract.h"
#include <QTimeZone>
#include <iostream>
#include <cmath>

namespace {
using namespace orion::diagnostics;
using namespace orion::core;
int failures = 0;
constexpr qint64 origin = 100000;
const auto utc = QDateTime::fromString("2026-09-23T12:00:00.000Z", Qt::ISODateWithMs);
void check(bool ok, const char* why) { if (!ok) { ++failures; std::cerr << why << '\n'; } }
QJsonObject envelope(int second, const QJsonObject& values, const QJsonObject& ages = {}) {
    QJsonObject fields;
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto age = ages.value(it.key()).toDouble();
        fields.insert(it.key(), QJsonObject{{"value", it.value()}, {"source", "fixture"}, {"quality", "valid"},
            {"age_at_capture_ms", age}, {"observed_at", utc.addMSecs(second * 1000 - age).toString(Qt::ISODateWithMs)}});
    }
    return {{"captured_monotonic_ms", origin + second * 1000},
        {"observed_at", utc.addSecs(second).toString(Qt::ISODateWithMs)}, {"fields", fields}};
}
QJsonObject row(int second, QJsonObject values, QJsonObject ages = {}, double appMb = -1, int appAge = 0) {
    IncidentSampleBuilder builder;
    QJsonObject app;
    if (appMb >= 0) app = {{"exe_path", "fixture.exe"}, {"ram_mb", appMb}, {"process_data_quality", "valid"},
        {"process_observed_monotonic_ms", origin + second * 1000 - appAge}};
    return builder.build(envelope(second, values, ages), origin + second * 1000, origin, origin, {}, app);
}
QJsonObject baseValues() { return {{"cpu_freq_mhz", 4000}, {"cpu_usage_percent", 30}, {"cpu_temp_c", 50},
    {"ram_available_percent", 40}, {"ram_used_percent", 60}, {"ram_total_mb", 16000}, {"pages_input_per_sec", 0}}; }
QJsonArray findings(const QJsonObject& summary) {
    return analyzeSnapshot(QJsonObject{{"incident", QJsonObject{{"status", "complete"}, {"summary", summary},
        {"runtime_memory", QJsonObject{{"memory_pressure", "confirmed"}, {"data_quality", "valid"}}}}}});
}
QJsonObject find(const QJsonArray& values, const char* id) {
    for (const auto& value : values) if (value.toObject().value("id") == QLatin1String(id)) return value.toObject();
    return {};
}
void freshness() {
    TelemetryData data;
    const auto epoch = std::chrono::system_clock::time_point(std::chrono::milliseconds(utc.toMSecsSinceEpoch()));
    data.cpuUsagePercent = Metric<double>::valid(90, "current-cpu", epoch);
    data.cpuTemperatureC = Metric<double>::valid(99, "old-sensor", epoch - std::chrono::seconds(10));
    data.ramUsagePercent = Metric<double>::valid(0, "memory", epoch);
    const auto source = incidentSystemEnvelope(data, {}, origin + 10000, utc);
    IncidentSampleBuilder builder;
    const auto fresh = builder.build(source, origin + 11000, origin, origin);
    check(fresh.value("cpu_usage_percent").toDouble() == 90 && fresh.value("cpu_temp_c").isNull()
        && fresh.value("ram_used_percent").toDouble(-1) == 0 && fresh.value("monotonic").toDouble() == 10,
        "Producer provenance, real zero or producer-relative placement lost.");
    const auto meta = fresh.value("metric_fields").toObject().value("cpu_temp_c").toObject();
    check(meta.value("source") == "old-sensor" && !meta.contains("value") && !meta.value("accepted").toBool(),
        "Rejected metric lost provenance or retained a misleading value.");
    check(builder.build(source, origin + 11001, origin, origin).isEmpty(), "Replayed frame created another observation.");
    IncidentSampleBuilder future, delayed, boundary, paused;
    check(future.build(source, origin + 9999, origin, origin).isEmpty(), "Future frame accepted.");
    check(delayed.build(source, origin + 13001, origin, origin).isEmpty(), "Old queued frame retimestamped as fresh.");
    check(boundary.build(source, origin + 13000, origin, origin).value("cpu_usage_percent").isDouble(),
        "Freshness boundary rejected exactly 3000 ms.");
    check(paused.build(source, origin + 11000, origin, origin + 10001).isEmpty(), "Pre-pause frame returned after resume.");
    IncidentSampleBuilder age;
    const auto aged = age.build(envelope(100, {{"cpu_temp_c", 99}}, {{"cpu_temp_c", 2000}}), origin + 101001, origin, origin);
    check(aged.value("cpu_temp_c").isNull(), "Queue delay and source age were not added.");
    IncidentSampleBuilder prePause;
    const auto before = prePause.build(envelope(100, {{"cpu_temp_c", 99}}, {{"cpu_temp_c", 1000}}),
        origin + 100000, origin, origin + 99500);
    check(before.value("cpu_temp_c").isNull(), "Fresh envelope hid pre-pause metric.");
    const auto invalid = row(100, {{"cpu_usage_percent", 101}, {"cpu_freq_mhz", 0}, {"cpu_temp_c", "99"},
        {"ram_used_percent", false}, {"net_errors_delta", -1}});
    check(invalid.value("usable_metric_count").toInt() == 0, "Invalid values/percent/frequency were accepted.");
    auto badQuality = envelope(100, {{"cpu_temp_c", 99}});
    auto badFields = badQuality.value("fields").toObject();
    auto badMetric = badFields.value("cpu_temp_c").toObject();
    badMetric.insert("quality", "stale"); badFields.insert("cpu_temp_c", badMetric); badQuality.insert("fields", badFields);
    IncidentSampleBuilder staleQuality;
    check(staleQuality.build(badQuality, origin + 100000, origin, origin).value("cpu_temp_c").isNull(),
        "Fresh timestamp rehabilitated a stale source quality.");
    check(row(100, baseValues(), {{"cpu_temp_c", -1}}).value("cpu_temp_c").isNull(), "Future source timestamp accepted.");
    IncidentSampleBuilder network;
    const auto one = network.build(envelope(100, {{"net_errors_delta", 3}, {"net_drops_delta", 0}}), origin + 100000, origin, origin);
    const auto two = network.build(envelope(101, {{"net_errors_delta", 3}, {"net_drops_delta", 0}},
        {{"net_errors_delta", 1000}, {"net_drops_delta", 1000}}), origin + 101000, origin, origin);
    const auto sum = summarizeIncidentSamples({one, two}, 100);
    check(sum.value("focus_net_error_count").toInt() == 3 && sum.value("focus_net_drop_count").toDouble(-1) == 0
        && sum.value("repeated_interval_count").toInt() == 2 && sum.value("net_errors_data_quality") == "partial",
        "Repeated network interval counted twice or known zero lost.");
    IncidentSampleBuilder pingBuilder;
    const auto ping = pingBuilder.build(envelope(100, baseValues()), origin + 100000, origin, origin, QJsonObject{
        {"value", 999}, {"quality", "valid"}, {"source", "fixture ping"}, {"observed_monotonic_ms", origin + 95000}});
    check(ping.value("net_ping_ms").isNull(), "Old ping attributed to a new incident frame.");
    check(row(100, baseValues(), {}, 8000, 4000).value("app").isUndefined(), "Stale process snapshot refreshed by UI receipt.");
    check(row(100, baseValues(), {}, 8000, -1).value("app").isUndefined(), "Future app snapshot joined an earlier system frame.");
    auto outside = row(90, {{"cpu_freq_mhz", 2000}}, {{"cpu_freq_mhz", 100}});
    check(summarizeIncidentSamples({outside}, 100).value("focus").toObject().value("cpu_freq_mhz").toObject()
        .value("count").toInt() == 0, "Pre-focus source metric leaked across focus boundary.");
}
void coincidences() {
    auto a = baseValues(), b = baseValues();
    a.insert("cpu_usage_percent", 100); a.insert("cpu_freq_mhz", 2500); a.insert("cpu_temp_c", 50);
    a.insert("ram_available_percent", 5);
    b.insert("cpu_usage_percent", 100); b.insert("cpu_temp_c", 95);
    b.insert("ram_available_percent", 14); b.insert("pages_input_per_sec", 200);
    const auto separate = summarizeIncidentSamples({row(80, baseValues()), row(96, a), row(97, b)}, 100);
    const auto separateFindings = findings(separate);
    check(separate.value("cpu_thermal_sample_count").toInt() == 0
        && separate.value("memory_pressure_sample_count").toInt() == 0
        && find(separateFindings, "incident.cpu.thermal_throttling").isEmpty()
        && find(separateFindings, "incident.memory.pressure").isEmpty(), "Unrelated peaks became thermal/memory coincidence.");
    auto matched = baseValues();
    matched.insert("cpu_usage_percent", 100); matched.insert("cpu_temp_c", 95); matched.insert("cpu_freq_mhz", 2900);
    matched.insert("ram_available_percent", 5); matched.insert("pages_input_per_sec", 200);
    const auto positive = summarizeIncidentSamples({row(80, baseValues()), row(100, matched, {}, 4000)}, 100);
    const auto positiveFindings = findings(positive);
    const auto thermal = find(positiveFindings, "incident.cpu.thermal_throttling");
    const auto app = find(positiveFindings, "incident.memory.app_contributor");
    const auto text = reportToText(QJsonObject{{"incident", QJsonObject{{"summary", positive}}}});
    check(text.contains(QStringLiteral("Свежие совпадения: RAM 1; CPU 1; приложение 1"))
        && text.contains(QStringLiteral("Причина не доказана")) && text.contains("2900.0"),
        "Text report hid source coincidence or its uncertainty.");
    check(positive.value("cpu_thermal_sample_count").toInt() == 1 && positive.value("app_contributor_sample_count").toInt() == 1
        && !thermal.isEmpty() && !app.isEmpty() && thermal.value("confidence") == "medium"
        && thermal.value("severity") == "critical" && thermal.value("detail").toString().contains(QStringLiteral("не проверен"))
        && !thermal.value("evidence").toArray().isEmpty(), "Matched fresh observation failed to produce qualified evidence.");
    const auto skew = summarizeIncidentSamples({row(80, baseValues()), row(100, matched,
        {{"cpu_temp_c", 2000}, {"pages_input_per_sec", 2000}}, 4000)}, 100);
    check(skew.value("cpu_thermal_sample_count").toInt() == 0 && skew.value("memory_pressure_sample_count").toInt() == 0,
        "Fresh-but-asynchronous sources were treated as simultaneous.");
    const auto edge = summarizeIncidentSamples({row(80, baseValues()), row(100, matched,
        {{"cpu_temp_c", 1000}, {"pages_input_per_sec", 1000}}, 4000, 1000)}, 100);
    check(edge.value("cpu_thermal_sample_count").toInt() == 1 && edge.value("app_contributor_sample_count").toInt() == 1,
        "One-second coincidence boundary changed.");
    const auto appElsewhere = summarizeIncidentSamples({row(100, matched, {}, 800), row(101, baseValues(), {}, 8000)}, 100);
    check(appElsewhere.value("memory_pressure_sample_count").toInt() == 1
        && appElsewhere.value("app_contributor_sample_count").toInt() == 0
        && find(findings(appElsewhere), "incident.memory.app_contributor").isEmpty(), "Unrelated application RAM peak was blamed.");
    const auto staleApp = summarizeIncidentSamples({row(100, matched, {}, 8000, 4000)}, 100);
    check(staleApp.value("stale_app_sample_count").toInt() == 1
        && find(findings(staleApp), "incident.memory.app_contributor").isEmpty(), "Stale app context became contributing evidence.");
    QJsonObject legacy{{"monotonic", 99}, {"cpu_usage_percent", 100}, {"cpu_freq_mhz", 1000}, {"cpu_temp_c", 99}};
    const auto mixed = summarizeIncidentSamples({row(80, baseValues()), row(100, baseValues()), legacy}, 100);
    check(mixed.value("focus").toObject().value("cpu_temp_c").toObject().value("max").toInt() == 50,
        "Legacy unprovenanced metrics contaminated a new-contract window.");
    const auto staleCpu = summarizeIncidentSamples({row(98, {{"cpu_usage_percent", 100}}, {{"cpu_usage_percent", 4000}}),
        row(99, {{"cpu_usage_percent", 20}}), row(100, {{"cpu_usage_percent", 20}})}, 100);
    check(find(findings(staleCpu), "incident.cpu.saturation").isEmpty(), "Stale high CPU polluted the high-sample fraction.");
    const auto freshCpu = summarizeIncidentSamples({row(96, {{"cpu_usage_percent", 95}}), row(97, {{"cpu_usage_percent", 90}}),
        row(98, {{"cpu_usage_percent", 10}}), row(99, {{"cpu_usage_percent", 10}}), row(100, {{"cpu_usage_percent", 10}})}, 100);
    check(!find(findings(freshCpu), "incident.cpu.saturation").isEmpty(), "Fresh CPU fraction missed low-average sustained samples.");
}
}
int main() {
    freshness(); coincidences();
    if (failures) return 1;
    std::cout << "Incident producer freshness and same-observation contracts passed.\n";
    return 0;
}
