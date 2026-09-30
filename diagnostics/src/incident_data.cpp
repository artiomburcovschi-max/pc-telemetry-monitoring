#include "orion/diagnostics/incident_data.h"
#include "orion/diagnostics/app_monitor_data.h"
#include "orion/diagnostics/telemetry_json.h"
#include "orion/core/memory_pressure.h"
#include "orion/core/thresholds.h"
#include <QJsonArray>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <limits>

namespace orion::diagnostics {
namespace {
struct Field { const char* key; const char* source; double divisor = 1; };
constexpr Field fields[] {
    {"cpu_usage_percent", "cpu_usage_percent"}, {"cpu_freq_mhz", "cpu_freq_mhz"}, {"cpu_temp_c", "cpu_temp_c"},
    {"ram_used_percent", "ram_used_percent"}, {"ram_available_percent", "ram_available_percent"},
    {"ram_total_mb", "ram_total_bytes", 1048576}, {"swap_used_percent", "swap_used_percent"},
    {"commit_used_percent", "commit_used_percent"}, {"pagefile_used_percent", "pagefile_used_percent"},
    {"pages_input_per_sec", "pages_input_per_second"}, {"page_reads_per_sec", "page_reads_per_second"},
    {"pages_per_sec", "pages_per_second"}, {"disk_busy_percent", "disk_busy_percent"},
    {"disk_read_latency_ms", "disk_read_latency_ms"}, {"disk_write_latency_ms", "disk_write_latency_ms"},
    {"system_context_switches_per_sec", "system_context_switches_per_second"},
    {"gpu_usage_percent", "gpu_usage_percent"}, {"gpu_temp_c", "gpu_temp_c"},
    {"gpu_vram_used_percent", "gpu_vram_used_percent"},
    {"net_download_mbps", "net_download_bytes_per_second", 1048576},
    {"net_upload_mbps", "net_upload_bytes_per_second", 1048576},
};
std::optional<double> numeric(const QJsonValue& v) {
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= 0
        ? std::optional<double>(v.toDouble()) : std::nullopt;
}
std::optional<qint64> tick(const QJsonValue& v) {
    const auto n = numeric(v);
    if (!n || std::trunc(*n) != *n || *n >= static_cast<double>(std::numeric_limits<qint64>::max())) return {};
    return static_cast<qint64>(*n);
}
QJsonObject withAge(QJsonObject metric, const QDateTime& utc) {
    const auto stamp = QDateTime::fromString(metric.value("observed_at").toString(), Qt::ISODateWithMs);
    metric.insert("age_at_capture_ms", stamp.isValid() && utc.isValid()
        ? QJsonValue(stamp.msecsTo(utc)) : QJsonValue(QJsonValue::Null));
    return metric;
}
QJsonObject counterMetric(const orion::core::Metric<std::uint64_t>& metric, const QDateTime& utc) {
    QJsonObject result{{"source", QString::fromStdString(metric.source)},
        {"quality", QString::fromLatin1(orion::core::toString(metric.quality).data())},
        {"reason", QString::fromStdString(metric.reason)}};
    if (metric.value) result.insert("value", static_cast<double>(*metric.value));
    if (metric.observedAt) result.insert("observed_at", QDateTime::fromMSecsSinceEpoch(
        std::chrono::duration_cast<std::chrono::milliseconds>(metric.observedAt->time_since_epoch()).count(),
        QTimeZone::UTC).toString(Qt::ISODateWithMs));
    return withAge(result, utc);
}
}

QJsonObject incidentSystemEnvelope(const orion::core::TelemetryData& data,
    const orion::core::NetworkCounterSample& network, qint64 monotonicMs, const QDateTime& utc)
{
    const auto metrics = telemetryToJson(data).value("metrics").toObject();
    QJsonObject selected;
    for (const auto& field : fields) {
        auto metric = metrics.value(QLatin1String(field.source)).toObject();
        if (const auto n = numeric(metric.value("value"))) metric.insert("value", *n / field.divisor);
        selected.insert(QLatin1String(field.key), withAge(metric, utc));
    }
    // Reuse the all-volumes-known disk aggregation (not a fabricated partial sum).
    const auto diskFields = appSystemEnvelope(data, monotonicMs, utc).value("system_fields").toObject();
    selected.insert("disk_read_mbps", diskFields.value("system_disk_read_mbps"));
    selected.insert("disk_write_mbps", diskFields.value("system_disk_write_mbps"));
    selected.insert("net_errors_delta", counterMetric(network.intervalErrors, utc));
    selected.insert("net_drops_delta", counterMetric(network.intervalDrops, utc));
    return {{"captured_monotonic_ms", monotonicMs}, {"observed_at", utc.toString(Qt::ISODateWithMs)},
        {"fields", selected}, {"network_scope", QString::fromStdString(data.netCounterScope)}};
}

QJsonObject IncidentSampleBuilder::build(const QJsonObject& envelope, qint64 now, qint64 origin,
    qint64 notBefore, const QJsonObject& ping, const QJsonObject& app)
{
    const auto captured = tick(envelope.value("captured_monotonic_ms"));
    if (!captured || origin < 0 || *captured < origin || *captured < notBefore || *captured > now
        || now - *captured > kIncidentFreshnessMs || *captured <= lastCapture_) return {};
    lastCapture_ = *captured;
    QJsonObject row{{"measurement_contract", kIncidentMeasurementContract},
        {"monotonic", (*captured - origin) / 1000.0}, {"observed_at", envelope.value("observed_at")},
        {"received_monotonic", (now - origin) / 1000.0}, {"source_age_limit_ms", kIncidentFreshnessMs},
        {"coincidence_limit_ms", kIncidentCoincidenceMs}, {"network_scope", envelope.value("network_scope")}};
    auto originals = envelope.value("fields").toObject();
    auto pingMetric = ping;
    const auto pingTick = tick(ping.value("observed_monotonic_ms"));
    pingMetric.insert("age_at_capture_ms", pingTick ? QJsonValue(*captured - *pingTick) : QJsonValue(QJsonValue::Null));
    originals.insert("net_ping_ms", pingMetric);
    QJsonObject metadata;
    int usable = 0, stale = 0, repeats = 0;
    for (auto it = originals.begin(); it != originals.end(); ++it) {
        auto metric = it.value().toObject();
        const auto value = numeric(metric.value("value"));
        const auto age = numeric(metric.value("age_at_capture_ms"));
        const auto quality = metric.value("quality").toString();
        const bool sourceKnown = quality == "valid" || quality == "estimated";
        const double observed = age ? *captured - *age : -1;
        const bool fresh = age && observed >= std::max(origin, notBefore) && observed <= *captured
            && now - observed <= kIncidentFreshnessMs;
        const bool range = value && (!it.key().endsWith("_percent") || *value <= 100)
            && (it.key() != "cpu_freq_mhz" || *value > 0);
        bool accepted = fresh && sourceKnown && range;
        QString reason = !value || !range ? "missing_or_invalid_value" : !sourceKnown ? "source_unusable"
            : !fresh ? "stale_or_pre_pause" : "accepted";
        if (accepted && (it.key() == "net_errors_delta" || it.key() == "net_drops_delta")) {
            const auto stamp = QDateTime::fromString(metric.value("observed_at").toString(), Qt::ISODateWithMs);
            const auto identity = it.key() + ':' + row.value("network_scope").toString() + ':' + metric.value("source").toString();
            const auto previous = intervalEnds_.constFind(identity);
            if (!stamp.isValid() || (previous != intervalEnds_.cend() && stamp.toMSecsSinceEpoch() <= *previous)) {
                accepted = false; reason = "repeated_or_unidentified_interval"; ++repeats;
            } else intervalEnds_.insert(identity, stamp.toMSecsSinceEpoch());
        }
        if (accepted) ++usable;
        else if (value && (!fresh || quality == "stale")) ++stale;
        metric.remove("value");
        metric.insert("effective_quality", accepted ? quality : reason);
        metric.insert("age_ms", age ? QJsonValue(now - observed) : QJsonValue(QJsonValue::Null));
        metric.insert("monotonic", fresh ? QJsonValue((observed - origin) / 1000.0) : QJsonValue(QJsonValue::Null));
        metric.insert("accepted", accepted);
        metadata.insert(it.key(), metric);
        row.insert(it.key(), accepted ? QJsonValue(*value) : QJsonValue(QJsonValue::Null));
    }
    row.insert("metric_fields", metadata);
    row.insert("usable_metric_count", usable); row.insert("stale_metric_count", stale);
    row.insert("repeated_interval_count", repeats);
    row.insert("metric_data_quality", usable == 0 ? "unknown" : usable == originals.size() ? "valid" : "partial");
    const auto paging = orion::core::assessWindowsPaging(numeric(row.value("ram_available_percent")),
        numeric(row.value("commit_used_percent")), numeric(row.value("pages_input_per_sec")),
        numeric(row.value("page_reads_per_sec")), numeric(row.value("pages_per_sec")));
    row.insert("paging_activity", QString::fromStdString(paging.pagingActivity));
    row.insert("hard_fault_activity", QString::fromStdString(paging.hardFaultActivity));
    row.insert("paging_interpretation", QString::fromStdString(paging.interpretation));
    row.insert("paging_rate_quality", paging.hardFaultActivity == "unknown" ? "unknown" : "estimated");
    row.insert("paging_sampling_mode", "endpoint_rates");
    row.insert("page_in_mbps", QJsonValue::Null); row.insert("page_out_mbps", QJsonValue::Null);
    const auto appTick = tick(app.value("process_observed_monotonic_ms"));
    const bool appFresh = appTick && *appTick >= std::max(origin, notBefore) && *appTick <= *captured
        && now - *appTick <= kIncidentFreshnessMs;
    row.insert("app_context_quality", app.isEmpty() ? "not_running" : appFresh ? "fresh" : "stale_or_untimestamped");
    if (appFresh) {
        QJsonObject context{{"exe_path", app.value("exe_path")}, {"monotonic", (*appTick - origin) / 1000.0},
            {"observed_at", app.value("process_observed_at")}, {"age_ms", now - *appTick},
            {"source", "identified process-tree snapshot"}, {"process_data_quality", app.value("process_data_quality")}};
        for (const auto& key : {"cpu_percent", "ram_mb", "private_mb", "read_mbps", "write_mbps", "process_count"}) {
            const auto value = numeric(app.value(QLatin1String(key)));
            context.insert(QLatin1String(key), value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null));
        }
        const auto total = numeric(row.value("ram_total_mb")), ram = numeric(context.value("ram_mb"));
        context.insert("ram_share_percent", total && *total > 0 && ram && *ram <= *total
            ? QJsonValue(*ram / *total * 100.0) : QJsonValue(QJsonValue::Null));
        row.insert("app", context);
    }
    return row;
}

bool incidentMetricsCoincide(const QJsonObject& sample, const QStringList& keys, std::optional<double> extra)
{
    if (sample.value("measurement_contract") != kIncidentMeasurementContract || keys.isEmpty()) return false;
    double earliest = std::numeric_limits<double>::max(), latest = -1;
    const auto fields = sample.value("metric_fields").toObject();
    for (const auto& key : keys) {
        const auto field = fields.value(key).toObject();
        const auto time = numeric(field.value("monotonic"));
        if (!numeric(sample.value(key)) || !field.value("accepted").toBool() || !time) return false;
        earliest = std::min(earliest, *time); latest = std::max(latest, *time);
    }
    if (extra) { if (!std::isfinite(*extra) || *extra < 0) return false;
        earliest = std::min(earliest, *extra); latest = std::max(latest, *extra); }
    return (latest - earliest) * 1000.0 <= kIncidentCoincidenceMs + 0.00001;
}

QJsonObject incidentCoincidences(const QJsonArray& baseline, const QJsonArray& focus)
{
    double frequencySum = 0;
    int frequencyCount = 0;
    for (const auto& v : baseline) {
        const auto row = v.toObject();
        if (incidentMetricsCoincide(row, {"cpu_freq_mhz"})) {
            frequencySum += row.value("cpu_freq_mhz").toDouble(); ++frequencyCount;
        }
    }
    const double baselineFrequency = frequencyCount ? frequencySum / frequencyCount : 0;
    QJsonArray memory, app, thermal;
    int memoryCount = 0, appCount = 0, thermalCount = 0, criticalThermalCount = 0;
    bool possible = false, memoryKnown = false, unconfirmedHardFaults = false;
    int activePagingCount = 0;
    for (const auto& v : focus) {
        const auto row = v.toObject();
        const auto available = numeric(row.value("ram_available_percent"));
        const auto ram = numeric(row.value("ram_used_percent"));
        possible |= (available && *available < 10) || (ram && *ram >= 95);
        QString faultKey;
        for (const auto& [key, threshold] : {std::pair{"pages_input_per_sec", 100.0},
                std::pair{"page_reads_per_sec", 5.0}, std::pair{"pages_per_sec", 1000.0}}) {
            if (!incidentMetricsCoincide(row, {"ram_available_percent", QLatin1String(key)})) continue;
            memoryKnown = true;
            if (row.value(QLatin1String(key)).toDouble() >= threshold && faultKey.isEmpty()) faultKey = QLatin1String(key);
        }
        unconfirmedHardFaults |= !faultKey.isEmpty();
        if (available && *available < 10 && !faultKey.isEmpty()) {
            ++memoryCount; ++activePagingCount;
            QJsonObject point{{"monotonic", row.value("monotonic")}, {"observed_at", row.value("observed_at")},
                {"available_percent", *available}, {"counter", faultKey}, {"counter_value", row.value(faultKey)}};
            if (memory.size() < 40) memory.append(point);
            const auto context = row.value("app").toObject();
            const auto share = numeric(context.value("ram_share_percent"));
            const auto stamp = numeric(context.value("monotonic"));
            if (share && *share >= 15 && stamp && incidentMetricsCoincide(row,
                    {"ram_available_percent", "ram_total_mb", faultKey}, stamp)) {
                ++appCount;
                point.insert("app_share_percent", *share); point.insert("exe_path", context.value("exe_path"));
                point.insert("app_monotonic", *stamp);
                if (app.size() < 40) app.append(point);
            }
        }
        if (!std::isfinite(baselineFrequency) || baselineFrequency <= 0
            || !incidentMetricsCoincide(row, {"cpu_usage_percent", "cpu_temp_c", "cpu_freq_mhz"})) continue;
        const auto temperature = row.value("cpu_temp_c").toDouble();
        const auto level = orion::core::levelForTemperature(temperature, "cpu");
        const double drop = std::max(0.0, (baselineFrequency - row.value("cpu_freq_mhz").toDouble()) / baselineFrequency * 100);
        if (row.value("cpu_usage_percent").toDouble() < 60 || drop < 10
            || (level != orion::core::StatusLevel::Warning && level != orion::core::StatusLevel::Critical)) continue;
        ++thermalCount;
        if (level == orion::core::StatusLevel::Critical && drop >= 20) ++criticalThermalCount;
        if (thermal.size() < 40) thermal.append(QJsonObject{{"monotonic", row.value("monotonic")},
            {"observed_at", row.value("observed_at")}, {"cpu_percent", row.value("cpu_usage_percent")},
            {"temperature_c", temperature}, {"frequency_mhz", row.value("cpu_freq_mhz")},
            {"baseline_frequency_mhz", baselineFrequency}, {"drop_percent", drop}});
    }
    return {{"measurement_contract", kIncidentMeasurementContract}, {"coincidence_limit_ms", kIncidentCoincidenceMs},
        {"focus_memory_pressure", memoryCount ? "confirmed" : possible ? "possible" : memoryKnown ? "not_observed" : "unknown"},
        {"focus_paging_activity", activePagingCount ? "active" : unconfirmedHardFaults ? "unconfirmed" : memoryKnown ? "idle" : "unknown"},
        {"paging_data_quality", memoryKnown ? "estimated" : "unknown"},
        {"focus_paging_active_sample_count", activePagingCount},
        {"memory_pressure_sample_count", memoryCount}, {"memory_pressure_observations", memory},
        {"app_contributor_sample_count", appCount}, {"app_contributor_observations", app},
        {"cpu_thermal_sample_count", thermalCount}, {"cpu_thermal_critical_sample_count", criticalThermalCount},
        {"cpu_thermal_observations", thermal}};
}
}
