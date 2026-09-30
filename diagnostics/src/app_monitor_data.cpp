#include "orion/diagnostics/app_monitor_data.h"
#include "orion/diagnostics/telemetry_json.h"
#include "orion/core/memory_pressure.h"
#include <QJsonArray>
#include <cmath>

namespace orion::diagnostics {
namespace {
struct Mapping { const char* target; const char* source; double divisor = 1; };
constexpr Mapping fields[] = {
    {"system_cpu_percent", "cpu_usage_percent"}, {"system_ram_percent", "ram_used_percent"},
    {"system_available_percent", "ram_available_percent"}, {"system_ram_total_mb", "ram_total_bytes", 1048576},
    {"system_commit_used_percent", "commit_used_percent"}, {"system_commit_used_mb", "commit_used_bytes", 1048576},
    {"system_commit_limit_mb", "commit_limit_bytes", 1048576}, {"pagefile_used_percent", "pagefile_used_percent"},
    {"system_disk_busy_percent", "disk_busy_percent"}, {"system_disk_read_latency_ms", "disk_read_latency_ms"},
    {"system_disk_write_latency_ms", "disk_write_latency_ms"}, {"system_context_switches_per_sec", "system_context_switches_per_second"},
    {"pages_input_per_sec", "pages_input_per_second"}, {"page_reads_per_sec", "page_reads_per_second"},
    {"pages_per_sec", "pages_per_second"}, {"gpu_usage_percent", "gpu_usage_percent"},
    {"gpu_temperature_c", "gpu_temp_c"}, {"gpu_vram_used_mb", "gpu_vram_used_mb"},
    {"gpu_vram_used_percent", "gpu_vram_used_percent"},
    {"net_download_mbps", "net_download_bytes_per_second", 1048576},
    {"net_upload_mbps", "net_upload_bytes_per_second", 1048576},
    {"system_disk_read_mbps", "app_disk_read_mbps"}, {"system_disk_write_mbps", "app_disk_write_mbps"},
};
std::optional<double> number(const QJsonValue& value)
{
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= 0
        ? std::optional<double>(value.toDouble()) : std::nullopt;
}
}
QJsonObject appSystemEnvelope(const orion::core::TelemetryData& data, qint64 monotonicMs, const QDateTime& nowUtc)
{
    auto metrics = telemetryToJson(data).value("metrics").toObject();
    for (const auto& direction : {QStringLiteral("read"), QStringLiteral("write")}) {
        auto diskMetric = metrics.value("disk_volumes").toObject();
        const auto volumes = diskMetric.value("value").toArray();
        bool complete = !volumes.isEmpty();
        double sum = 0;
        for (const auto& volume : volumes) {
            const auto value = number(volume.toObject().value(direction + "_bytes_per_second"));
            if (value) sum += *value / 1048576.0; else complete = false;
        }
        diskMetric.insert("value", complete && std::isfinite(sum) ? QJsonValue(sum) : QJsonValue(QJsonValue::Null));
        metrics.insert("app_disk_" + direction + "_mbps", diskMetric);
    }
    QJsonObject metadata;
    for (const auto& field : fields) {
        auto metric = metrics.value(QLatin1String(field.source)).toObject();
        if (const auto value = number(metric.value("value"))) metric.insert("value", *value / field.divisor);
        const auto stamp = QDateTime::fromString(metric.value("observed_at").toString(), Qt::ISODateWithMs);
        metric.insert("age_at_capture_ms", stamp.isValid() ? QJsonValue(stamp.msecsTo(nowUtc)) : QJsonValue(QJsonValue::Null));
        metadata.insert(QLatin1String(field.target), metric);
    }
    return {{"system_captured_monotonic_ms", monotonicMs}, {"system_observed_at", nowUtc.toString(Qt::ISODateWithMs)},
        {"system_fields", metadata}, {"gpu_name", QString::fromStdString(data.gpuName)},
        {"system_source", QStringLiteral("native telemetry; timestamped metric cache")}};
}

QJsonObject freshAppSystemSample(const QJsonObject& envelope, qint64 nowMonotonicMs, qint64 notBeforeMonotonicMs)
{
    const auto stamp = envelope.value("system_captured_monotonic_ms");
    const qint64 captured = stamp.toInteger(-1);
    const bool timed = stamp.isDouble() && captured >= 0 && captured <= nowMonotonicMs;
    const bool boundary = timed && captured >= notBeforeMonotonicMs;
    const qint64 age = timed ? nowMonotonicMs - captured : -1;
    const bool fresh = boundary && age <= kAppSystemFreshnessMs;
    QJsonObject result{{"system_observed_at", envelope.value("system_observed_at")},
        {"system_captured_monotonic_ms", stamp}, {"system_source", envelope.value("system_source")},
        {"system_age_ms", timed ? QJsonValue(age) : QJsonValue(QJsonValue::Null)},
        {"system_freshness_limit_ms", kAppSystemFreshnessMs},
        {"system_sample_reason", !timed ? "missing_or_invalid_timestamp" : !boundary ? "no_post_boundary_sample"
            : !fresh ? "cache_expired" : "timestamped_cache"}, {"gpu_name", envelope.value("gpu_name")}};
    QJsonObject metadata;
    int usable = 0, stale = 0;
    const auto original = envelope.value("system_fields").toObject();
    for (const auto& field : fields) {
        const auto key = QLatin1String(field.target);
        auto metric = original.value(key).toObject();
        const auto ageAtCapture = metric.value("age_at_capture_ms");
        const double totalAge = ageAtCapture.toDouble(-1) + (timed ? age : 0);
        const auto quality = metric.value("quality").toString("unknown");
        const bool timeValid = fresh && ageAtCapture.isDouble() && ageAtCapture.toDouble() >= 0
            && (notBeforeMonotonicMs < 0 || ageAtCapture.toDouble() <= captured - notBeforeMonotonicMs)
            && totalAge >= 0 && totalAge <= kAppSystemFreshnessMs;
        const bool sourceUsable = quality == "valid" || quality == "estimated";
        const auto value = number(metric.value("value"));
        const bool accepted = timeValid && sourceUsable && value.has_value();
        if (accepted) ++usable;
        if (quality == "stale" || (value && sourceUsable && !timeValid)) ++stale;
        const QString effective = accepted ? quality : (value && (sourceUsable || quality == "stale"))
            ? QStringLiteral("stale") : quality;
        metric.insert("effective_quality", effective);
        metric.insert("age_ms", ageAtCapture.isDouble() && timed ? QJsonValue(totalAge) : QJsonValue(QJsonValue::Null));
        // Retained stale values are never exported as current measurements, even in metadata.
        metric.remove("value");
        metadata.insert(key, metric);
        result.insert(key, accepted ? QJsonValue(*value) : QJsonValue(QJsonValue::Null));
    }
    result.insert("system_fields", metadata);
    result.insert("system_usable_fields", usable);
    result.insert("system_stale_fields", stale);
    result.insert("system_data_quality", !fresh ? (timed ? "stale" : "unknown")
        : !usable ? "unknown" : usable < static_cast<int>(std::size(fields)) ? "partial" : "valid");
    const auto paging = orion::core::assessWindowsPaging(number(result.value("system_available_percent")),
        number(result.value("system_commit_used_percent")), number(result.value("pages_input_per_sec")),
        number(result.value("page_reads_per_sec")), number(result.value("pages_per_sec")));
    result.insert("paging_activity", QString::fromStdString(paging.pagingActivity));
    result.insert("hard_fault_activity", QString::fromStdString(paging.hardFaultActivity));
    result.insert("memory_pressure", !number(result.value("system_available_percent")) && !number(result.value("system_commit_used_percent"))
        ? QStringLiteral("unknown") : QString::fromStdString(paging.memoryPressure));
    result.insert("paging_interpretation", QString::fromStdString(paging.interpretation));
    result.insert("paging_sampling_mode", "endpoint_rates");
    result.insert("paging_rate_quality", result.value("pages_input_per_sec").isDouble()
        || result.value("page_reads_per_sec").isDouble() || result.value("pages_per_sec").isDouble() ? "estimated" : "unknown");
    result.insert("page_in_mbps", QJsonValue::Null); result.insert("page_out_mbps", QJsonValue::Null);
    return result;
}

QJsonObject appMemoryComparison(const QJsonObject& before, const QJsonObject& after)
{
    const bool comparable = before.value("system_available_percent").isDouble()
        && after.value("system_available_percent").isDouble()
        && before.value("system_captured_monotonic_ms").toInteger(-1) < after.value("system_captured_monotonic_ms").toInteger(-1);
    QJsonObject result{{"before", before}, {"after", after}, {"observation_kind", "timestamped_cache_endpoints"},
        {"data_quality", comparable ? "valid" : "unknown"}, {"memory_pressure", "unknown"},
        {"before_memory_pressure", before.value("memory_pressure")}, {"after_memory_pressure", after.value("memory_pressure")},
        {"note", QStringLiteral("Снимки до/после — свежий системный кэш с временем источника. Они не доказывают совпадение дефицита памяти с работой приложения.")}};
    for (const auto& key : {QStringLiteral("system_ram_percent"), QStringLiteral("system_available_percent"), QStringLiteral("system_commit_used_mb")}) {
        const auto a = number(before.value(key)), b = number(after.value(key));
        result.insert(key + "_delta", comparable && a && b ? QJsonValue(*b - *a) : QJsonValue(QJsonValue::Null));
    }
    return result;
}
}
