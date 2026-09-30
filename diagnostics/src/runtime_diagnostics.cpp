#include "orion/diagnostics/runtime_diagnostics.h"
#include "orion/diagnostics/incident_data.h"

#include <QDateTime>
#include <QJsonValue>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>

namespace orion::diagnostics {
namespace {

[[nodiscard]] std::optional<double> finiteNumber(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double result = value.toDouble();
    return std::isfinite(result) ? std::optional<double> {result} : std::nullopt;
}

[[nodiscard]] double rounded(const double value, const int digits = 3)
{
    const double scale = std::pow(10.0, digits);
    return std::round(value * scale) / scale;
}

[[nodiscard]] QVector<double> values(const QJsonArray& rows, const QString& key)
{
    QVector<double> result;
    result.reserve(rows.size());
    for (const auto& value : rows) {
        const auto number = finiteNumber(value.toObject().value(key));
        if (number.has_value()) {
            result.append(*number);
        }
    }
    return result;
}

[[nodiscard]] QJsonObject statistics(const QJsonArray& rows, const QString& key)
{
    auto data = values(rows, key);
    if (data.isEmpty()) {
        return {
            {QStringLiteral("count"), 0},
            {QStringLiteral("min"), QJsonValue::Null},
            {QStringLiteral("max"), QJsonValue::Null},
            {QStringLiteral("avg"), QJsonValue::Null},
            {QStringLiteral("p95"), QJsonValue::Null},
            {QStringLiteral("last"), QJsonValue::Null},
            {QStringLiteral("above_90_fraction"), QJsonValue::Null},
        };
    }
    const double last = data.back();
    const auto above90 = std::count_if(data.cbegin(), data.cend(), [](double value) { return value >= 90.0; });
    const double average = std::accumulate(data.cbegin(), data.cend(), 0.0)
        / static_cast<double>(data.size());
    std::ranges::sort(data);
    const qsizetype p95Index = std::clamp<qsizetype>(
        static_cast<qsizetype>(std::ceil(static_cast<double>(data.size()) * 0.95)) - 1,
        0,
        data.size() - 1);
    return {
        {QStringLiteral("count"), data.size()},
        {QStringLiteral("min"), rounded(data.front())},
        {QStringLiteral("max"), rounded(data.back())},
        {QStringLiteral("avg"), rounded(average)},
        {QStringLiteral("p95"), rounded(data[p95Index])},
        {QStringLiteral("last"), rounded(last)},
        {QStringLiteral("above_90_fraction"), rounded(static_cast<double>(above90) / data.size())},
    };
}

[[nodiscard]] QJsonValue optionalValue(const std::optional<double>& value, const int digits = 3)
{
    return value.has_value() ? QJsonValue {rounded(*value, digits)}
                             : QJsonValue {QJsonValue::Null};
}

[[nodiscard]] std::optional<double> average(const QVector<double>& data)
{
    if (data.isEmpty()) return std::nullopt;
    return std::accumulate(data.cbegin(), data.cend(), 0.0)
        / static_cast<double>(data.size());
}

[[nodiscard]] std::optional<double> maximum(const QVector<double>& data)
{
    if (data.isEmpty()) return std::nullopt;
    return *std::max_element(data.cbegin(), data.cend());
}

[[nodiscard]] std::optional<double> minimum(const QVector<double>& data)
{
    if (data.isEmpty()) return std::nullopt;
    return *std::min_element(data.cbegin(), data.cend());
}

[[nodiscard]] std::optional<double> median(QVector<double> data)
{
    if (data.isEmpty()) return std::nullopt;
    std::ranges::sort(data);
    const qsizetype middle = data.size() / 2;
    return data.size() % 2 != 0
        ? data[middle]
        : (data[middle - 1] + data[middle]) / 2.0;
}

struct Trend {
    std::optional<double> start;
    std::optional<double> end;
    std::optional<double> peak;
    std::optional<double> growth;
    std::optional<double> slopePerMinute;
    std::optional<double> r2;
    std::optional<double> positiveFraction;
    qsizetype stepCount {0};
    std::optional<double> firstQuarterAverage;
    std::optional<double> lastQuarterAverage;
    std::optional<double> quarterGrowth;
    std::optional<double> recoveryFromPeak;
};

[[nodiscard]] Trend trend(const QJsonArray& samples, const QString& key)
{
    QVector<QPair<double, double>> points;
    for (const auto& value : samples) {
        const auto row = value.toObject();
        const auto x = finiteNumber(row.value(QStringLiteral("elapsed")));
        const auto y = finiteNumber(row.value(key));
        if (x.has_value() && y.has_value()) points.append({*x, *y});
    }
    if (points.isEmpty()) return {};
    Trend result;
    result.start = points.front().second;
    result.end = points.back().second;
    result.peak = points.front().second;
    for (const auto& point : points) result.peak = std::max(*result.peak, point.second);
    result.growth = *result.end - *result.start;
    result.recoveryFromPeak = *result.peak - *result.end;

    const qsizetype quarter = std::max<qsizetype>(1, points.size() / 4);
    double first = 0.0;
    double last = 0.0;
    for (qsizetype index = 0; index < quarter; ++index) {
        first += points[index].second;
        last += points[points.size() - quarter + index].second;
    }
    result.firstQuarterAverage = first / static_cast<double>(quarter);
    result.lastQuarterAverage = last / static_cast<double>(quarter);
    result.quarterGrowth = *result.lastQuarterAverage - *result.firstQuarterAverage;

    if (points.size() >= 2) {
        result.stepCount = points.size() - 1;
        qsizetype increases = 0;
        for (qsizetype index = 1; index < points.size(); ++index)
            increases += points[index].second > points[index - 1].second;
        result.positiveFraction = static_cast<double>(increases) / result.stepCount;
        const double origin = points.front().first;
        double meanX = 0.0;
        double meanY = 0.0;
        for (const auto& point : points) {
            meanX += point.first - origin;
            meanY += point.second;
        }
        meanX /= static_cast<double>(points.size());
        meanY /= static_cast<double>(points.size());
        double covariance = 0.0;
        double varianceX = 0.0;
        double varianceY = 0.0;
        for (const auto& point : points) {
            const double dx = (point.first - origin) - meanX;
            const double dy = point.second - meanY;
            covariance += dx * dy;
            varianceX += dx * dx;
            varianceY += dy * dy;
        }
        if (varianceX > 0.0) {
            const double slope = covariance / varianceX;
            result.slopePerMinute = slope * 60.0;
            double residual = 0.0;
            for (const auto& point : points) {
                const double error = point.second - (meanY + slope * ((point.first - origin) - meanX));
                residual += error * error;
            }
            result.r2 = std::clamp(varianceY > 0 ? 1.0 - residual / varianceY : 1.0, 0.0, 1.0);
        }
    }
    return result;
}

void insertTrend(QJsonObject& summary, const QString& prefix, const Trend& value)
{
    summary.insert(prefix + QStringLiteral("_start_mb"), optionalValue(value.start));
    summary.insert(prefix + QStringLiteral("_end_mb"), optionalValue(value.end));
    summary.insert(prefix + QStringLiteral("_peak_mb"), optionalValue(value.peak));
    summary.insert(prefix + QStringLiteral("_growth_mb"), optionalValue(value.growth));
    summary.insert(prefix + QStringLiteral("_growth_mb_per_min"), optionalValue(value.slopePerMinute));
    summary.insert(prefix + QStringLiteral("_growth_r2"), optionalValue(value.r2, 4));
    summary.insert(prefix + QStringLiteral("_first_quarter_avg_mb"), optionalValue(value.firstQuarterAverage));
    summary.insert(prefix + QStringLiteral("_last_quarter_avg_mb"), optionalValue(value.lastQuarterAverage));
    summary.insert(prefix + QStringLiteral("_tail_growth_mb"), optionalValue(value.quarterGrowth));
    summary.insert(prefix + QStringLiteral("_recovery_from_peak_mb"), optionalValue(value.recoveryFromPeak));
}

[[nodiscard]] QJsonObject peakMoment(const QJsonArray& samples, const QString& key)
{
    QJsonObject best;
    std::optional<double> peak;
    for (const auto& value : samples) {
        const auto row = value.toObject();
        const auto current = finiteNumber(row.value(key));
        if (current.has_value() && (!peak.has_value() || *current > *peak)) {
            peak = current;
            best = {
                {QStringLiteral("value"), rounded(*current)},
                {QStringLiteral("elapsed"), row.value(QStringLiteral("elapsed"))},
                {QStringLiteral("observed_at"), row.value(QStringLiteral("observed_at"))},
            };
        }
    }
    return best;
}

[[nodiscard]] QJsonArray selectWindow(
    const QJsonArray& rows,
    const double start,
    const double end,
    const bool includeStart = true,
    const bool includeEnd = true)
{
    QJsonArray result;
    for (const auto& value : rows) {
        const auto monotonic = finiteNumber(value.toObject().value(QStringLiteral("monotonic")));
        if (monotonic.has_value()
            && (includeStart ? *monotonic >= start : *monotonic > start)
            && (includeEnd ? *monotonic <= end : *monotonic < end)) {
            auto selected = value.toObject();
            if (selected.value("measurement_contract") == kIncidentMeasurementContract) {
                const auto inWindow = [&](const QJsonValue& stamp) {
                    const auto t = finiteNumber(stamp);
                    return t && (includeStart ? *t >= start : *t > start) && (includeEnd ? *t <= end : *t < end);
                };
                const auto fields = selected.value("metric_fields").toObject();
                for (auto it = fields.begin(); it != fields.end(); ++it)
                    if (!it.value().toObject().value("accepted").toBool()
                        || !inWindow(it.value().toObject().value("monotonic"))) selected.insert(it.key(), QJsonValue::Null);
                if (!inWindow(selected.value("app").toObject().value("monotonic"))) selected.remove("app");
            }
            result.append(selected);
        }
    }
    return result;
}

[[nodiscard]] QJsonObject summarizeAppContext(const QJsonArray& focus)
{
    QJsonArray rows;
    for (const auto& value : focus) {
        const auto app = value.toObject().value(QStringLiteral("app"));
        if (app.isObject()) rows.append(app);
    }
    if (rows.isEmpty()) return {};
    QJsonObject result {
        {QStringLiteral("sample_count"), rows.size()},
    };
    for (qsizetype index = rows.size(); index > 0; --index) {
        const auto path = rows[index - 1].toObject().value(QStringLiteral("exe_path")).toString();
        if (!path.isEmpty()) {
            result.insert(QStringLiteral("exe_path"), path);
            break;
        }
    }
    int peakProcesses = 0;
    for (const auto& value : rows) {
        peakProcesses = std::max(peakProcesses,
            value.toObject().value(QStringLiteral("process_count")).toInt());
    }
    result.insert(QStringLiteral("process_count_peak"), peakProcesses);
    for (const auto& key : {
             QStringLiteral("cpu_percent"), QStringLiteral("ram_mb"),
             QStringLiteral("ram_share_percent"), QStringLiteral("read_mbps"),
             QStringLiteral("write_mbps"), QStringLiteral("system_disk_busy_percent"),
             QStringLiteral("system_ram_percent"), QStringLiteral("system_available_percent")}) {
        result.insert(key, statistics(rows, key));
    }
    const auto firstRam = finiteNumber(rows.first().toObject().value(QStringLiteral("ram_mb")));
    const auto lastRam = finiteNumber(rows.last().toObject().value(QStringLiteral("ram_mb")));
    result.insert(QStringLiteral("ram_growth_mb"), firstRam.has_value() && lastRam.has_value()
        ? QJsonValue {rounded(*lastRam - *firstRam, 2)} : QJsonValue {QJsonValue::Null});
    return result;
}

[[nodiscard]] QString formatted(const QJsonValue& value, const QString& suffix = {}, const int digits = 1)
{
    const auto number = finiteNumber(value);
    return number.has_value() ? QStringLiteral("%1%2").arg(*number, 0, 'f', digits).arg(suffix)
                              : QStringLiteral("н/д");
}

} // namespace

double appMonitorSampleInterval(const int durationSeconds) noexcept
{
    const double minutes = static_cast<double>(durationSeconds) / 60.0;
    if (minutes <= 30.0) return 1.0;
    if (minutes <= 120.0) return 2.0;
    return 5.0;
}

QJsonObject decodeWindowsExitCode(const QJsonValue& exitCode)
{
    QJsonObject result {{"code", exitCode.isUndefined() ? QJsonValue(QJsonValue::Null) : exitCode},
        {"hex", QJsonValue::Null}, {"name", QJsonValue::Null}, {"category", QJsonValue::Null}};
    const auto numeric = finiteNumber(exitCode);
    if (!numeric || std::trunc(*numeric) != *numeric || *numeric < -2147483648.0 || *numeric > 4294967295.0)
        return result;
    const auto code = static_cast<quint32>(static_cast<qint64>(*numeric));
    result.insert("unsigned", static_cast<qint64>(code));
    result.insert("hex", "0x" + QStringLiteral("%1").arg(code, 8, 16, QChar('0')).toUpper());
    struct Entry { quint32 code; const char* name; const char* category; };
    // Source-led parity: Python core/process_memory.py::decode_windows_exit_code.
    // A process may return arbitrary codes; a matching value is not a proven cause.
    static constexpr Entry entries[] {
        {0xC0000005u, "ACCESS_VIOLATION", "memory_access"},
        {0xC0000017u, "NO_MEMORY", "out_of_memory"},
        {0xC0000094u, "INTEGER_DIVIDE_BY_ZERO", "application_fault"},
        {0xC0000096u, "PRIVILEGED_INSTRUCTION", "application_fault"},
        {0xC00000FDu, "STACK_OVERFLOW", "stack_overflow"},
        {0xC0000374u, "HEAP_CORRUPTION", "heap_corruption"},
        {0xC0000409u, "STACK_BUFFER_OVERRUN / FAIL_FAST", "fail_fast"},
        {0xC0000420u, "ASSERTION_FAILURE", "application_fault"},
        {0xE06D7363u, "MICROSOFT_CPP_EXCEPTION", "application_exception"},
        {0x40000015u, "FATAL_APP_EXIT", "application_fault"},
    };
    for (const auto& entry : entries) if (entry.code == code) {
        result.insert("name", QLatin1String(entry.name));
        result.insert("category", QLatin1String(entry.category));
        break;
    }
    return result;
}

QJsonObject summarizeAppMonitorSamples(const QJsonArray& samples)
{
    if (samples.isEmpty()) return {{QStringLiteral("sample_count"), 0}};
    const auto cpu = values(samples, QStringLiteral("cpu_percent"));
    const auto ramShare = values(samples, QStringLiteral("ram_share_percent"));
    const auto read = values(samples, QStringLiteral("read_mbps"));
    const auto write = values(samples, QStringLiteral("write_mbps"));
    const auto busy = values(samples, QStringLiteral("system_disk_busy_percent"));
    const auto readLatency = values(samples, QStringLiteral("system_disk_read_latency_ms"));
    const auto writeLatency = values(samples, QStringLiteral("system_disk_write_latency_ms"));
    const auto systemSwitches = values(samples, QStringLiteral("system_context_switches_per_sec"));
    const auto systemRam = values(samples, QStringLiteral("system_ram_percent"));
    const auto available = values(samples, QStringLiteral("system_available_percent"));
    const auto commit = values(samples, QStringLiteral("system_commit_used_percent"));
    const auto gpuUsage = values(samples, QStringLiteral("gpu_usage_percent"));
    const auto gpuTemp = values(samples, QStringLiteral("gpu_temperature_c"));
    const auto gpuVram = values(samples, QStringLiteral("gpu_vram_used_mb"));
    const auto gpuPower = values(samples, QStringLiteral("gpu_power_w"));
    const auto gpuClock = values(samples, QStringLiteral("gpu_clock_mhz"));
    const auto faults = values(samples, QStringLiteral("page_faults_per_sec"));
    const auto switches = values(samples, QStringLiteral("context_switches_per_sec"));
    const auto intervals = values(samples, QStringLiteral("sample_interval"));
    const auto ramTrend = trend(samples, QStringLiteral("ram_mb"));
    const auto privateTrend = trend(samples, QStringLiteral("private_mb"));
    const auto handleTrend = trend(samples, QStringLiteral("handle_count"));
    const double firstElapsed = finiteNumber(samples.first().toObject().value(QStringLiteral("elapsed"))).value_or(0.0);
    const double lastElapsed = finiteNumber(samples.last().toObject().value(QStringLiteral("elapsed"))).value_or(firstElapsed);

    QJsonObject summary {
        {QStringLiteral("sample_count"), samples.size()},
        {QStringLiteral("observed_seconds"), rounded(std::max(0.001, lastElapsed - firstElapsed))},
        {QStringLiteral("sample_interval_seconds"), optionalValue(median(intervals))},
        {QStringLiteral("cpu_avg_percent"), optionalValue(average(cpu))},
        {QStringLiteral("cpu_peak_percent"), optionalValue(maximum(cpu))},
        {QStringLiteral("ram_share_peak_percent"), optionalValue(maximum(ramShare))},
        {QStringLiteral("read_peak_mbps"), optionalValue(maximum(read))},
        {QStringLiteral("write_peak_mbps"), optionalValue(maximum(write))},
        {QStringLiteral("system_disk_busy_peak_percent"), optionalValue(maximum(busy))},
        {QStringLiteral("system_disk_read_latency_avg_ms"), optionalValue(average(readLatency))},
        {QStringLiteral("system_disk_read_latency_peak_ms"), optionalValue(maximum(readLatency))},
        {QStringLiteral("system_disk_write_latency_avg_ms"), optionalValue(average(writeLatency))},
        {QStringLiteral("system_disk_write_latency_peak_ms"), optionalValue(maximum(writeLatency))},
        {QStringLiteral("system_context_switches_avg_per_sec"), optionalValue(average(systemSwitches))},
        {QStringLiteral("system_context_switches_peak_per_sec"), optionalValue(maximum(systemSwitches))},
        {QStringLiteral("system_ram_peak_percent"), optionalValue(maximum(systemRam))},
        {QStringLiteral("system_available_min_percent"), optionalValue(minimum(available))},
        {QStringLiteral("system_commit_peak_percent"), optionalValue(maximum(commit))},
        {QStringLiteral("gpu_usage_avg_percent"), optionalValue(average(gpuUsage))},
        {QStringLiteral("gpu_usage_peak_percent"), optionalValue(maximum(gpuUsage))},
        {QStringLiteral("gpu_temperature_peak_c"), optionalValue(maximum(gpuTemp))},
        {QStringLiteral("gpu_vram_peak_mb"), optionalValue(maximum(gpuVram))},
        {QStringLiteral("gpu_power_peak_w"), optionalValue(maximum(gpuPower))},
        {QStringLiteral("gpu_clock_min_mhz"), optionalValue(minimum(gpuClock))},
        {QStringLiteral("gpu_clock_max_mhz"), optionalValue(maximum(gpuClock))},
        {QStringLiteral("page_faults_avg_per_sec"), optionalValue(average(faults))},
        {QStringLiteral("page_faults_peak_per_sec"), optionalValue(maximum(faults))},
        {QStringLiteral("context_switches_avg_per_sec"), optionalValue(average(switches))},
        {QStringLiteral("context_switches_peak_per_sec"), optionalValue(maximum(switches))},
        {QStringLiteral("read_total_mb"), samples.last().toObject().value(QStringLiteral("read_total_mb"))},
        {QStringLiteral("write_total_mb"), samples.last().toObject().value(QStringLiteral("write_total_mb"))},
    };
    insertTrend(summary, QStringLiteral("ram"), ramTrend);
    summary.insert("ram_growth_positive_fraction", optionalValue(ramTrend.positiveFraction));
    summary.insert("ram_growth_step_count", ramTrend.stepCount);
    summary.insert("ram_growth_fraction_scope", "adjacent_known_values");
    insertTrend(summary, QStringLiteral("private"), privateTrend);
    summary.insert(QStringLiteral("private_plateau_detected"),
        privateTrend.recoveryFromPeak.has_value() && privateTrend.peak.has_value()
        && privateTrend.quarterGrowth.has_value()
        && std::abs(*privateTrend.quarterGrowth) <= std::max(32.0, *privateTrend.peak * 0.03));
    summary.insert(QStringLiteral("handle_start"), optionalValue(handleTrend.start));
    summary.insert(QStringLiteral("handle_end"), optionalValue(handleTrend.end));
    summary.insert(QStringLiteral("handle_peak"), optionalValue(handleTrend.peak));
    summary.insert(QStringLiteral("handle_growth"), optionalValue(handleTrend.growth));
    summary.insert(QStringLiteral("handle_growth_per_min"), optionalValue(handleTrend.slopePerMinute));
    summary.insert(QStringLiteral("handle_growth_r2"), optionalValue(handleTrend.r2, 4));

    int highCpu = 0;
    for (const double value : cpu) if (value >= 80.0) ++highCpu;
    summary.insert(QStringLiteral("cpu_high_fraction"), cpu.isEmpty()
        ? QJsonValue {QJsonValue::Null}
        : QJsonValue {rounded(static_cast<double>(highCpu) / cpu.size())});
    int pagingActive = 0;
    int systemUnavailableSamples = 0, systemStaleSamples = 0, ioIncompleteSamples = 0;
    bool identifiedIntervals = false;
    int pagingKnown = 0;
    int hung = 0;
    double longestHung = 0.0;
    double currentHungStart = -1.0;
    int processPeak = 0;
    int threadPeak = 0;
    int responsivenessKnown = 0;
    QJsonArray hungObservations;
    for (const auto& value : samples) {
        const auto row = value.toObject();
        const auto paging = row.value(QStringLiteral("paging_activity")).toString();
        if (row.contains("system_data_quality")) {
            systemUnavailableSamples += row.value("system_data_quality") != "valid";
            systemStaleSamples += row.value("system_stale_fields").toInt() > 0 || row.value("system_data_quality") == "stale";
        }
        if (row.value("io_total_scope") == "observed_intervals_lower_bound") {
            identifiedIntervals = true;
            ioIncompleteSamples += !row.value("read_mbps").isDouble() || !row.value("write_mbps").isDouble();
        }
        if (paging == QStringLiteral("active")) ++pagingActive;
        if (paging == QStringLiteral("active") || paging == QStringLiteral("idle")) ++pagingKnown;
        processPeak = std::max(processPeak, row.value(QStringLiteral("process_count")).toInt());
        threadPeak = std::max(threadPeak, row.value(QStringLiteral("thread_count")).toInt());
        if (row.value(QStringLiteral("ui_hung")).isBool()) {
            ++responsivenessKnown;
            const double elapsed = row.value(QStringLiteral("elapsed")).toDouble();
            if (row.value(QStringLiteral("ui_hung")).toBool()) {
                ++hung;
                if (currentHungStart < 0.0) currentHungStart = elapsed;
                longestHung = std::max(longestHung, elapsed - currentHungStart);
                if (hungObservations.size() < 40) {
                    hungObservations.append(QJsonObject {
                        {QStringLiteral("elapsed"), row.value(QStringLiteral("elapsed"))},
                        {QStringLiteral("observed_at"), row.value(QStringLiteral("observed_at"))},
                        {QStringLiteral("titles"), row.value(QStringLiteral("hung_window_titles"))},
                    });
                }
            } else {
                currentHungStart = -1.0;
            }
        } else {
            currentHungStart = -1.0;
        }
    }
    summary.insert(QStringLiteral("ui_hung_fraction"), responsivenessKnown > 0
        ? QJsonValue(rounded(static_cast<double>(hung) / responsivenessKnown)) : QJsonValue(QJsonValue::Null));
    summary.insert(QStringLiteral("ui_hung_max_streak_seconds"), responsivenessKnown > 0
        ? QJsonValue(rounded(longestHung)) : QJsonValue(QJsonValue::Null));
    summary.insert(QStringLiteral("hung_observations"), hungObservations);
    summary.insert(QStringLiteral("paging_active_fraction"), pagingKnown > 0
        ? QJsonValue(rounded(static_cast<double>(pagingActive) / pagingKnown)) : QJsonValue(QJsonValue::Null));
    summary.insert("system_unavailable_sample_count", systemUnavailableSamples);
    summary.insert("system_stale_sample_count", systemStaleSamples);
    summary.insert("io_incomplete_sample_count", ioIncompleteSamples);
    if (identifiedIntervals) summary.insert("io_total_scope", "observed_intervals_lower_bound");
    summary.insert(QStringLiteral("process_count_peak"), processPeak);
    summary.insert(QStringLiteral("thread_count_peak"), threadPeak);
    for (qsizetype index = samples.size(); index > 0; --index) {
        const auto processes = samples[index - 1].toObject().value(QStringLiteral("top_processes")).toArray();
        if (!processes.isEmpty()) { summary.insert(QStringLiteral("top_processes_last"), processes); break; }
    }
    for (qsizetype index = samples.size(); index > 0; --index) {
        const QString gpuName = samples[index - 1].toObject().value(QStringLiteral("gpu_name")).toString();
        if (!gpuName.isEmpty()) {
            summary.insert(QStringLiteral("gpu_name"), gpuName);
            break;
        }
    }
    summary.insert(QStringLiteral("peak_moments"), QJsonObject {
        {QStringLiteral("cpu"), peakMoment(samples, QStringLiteral("cpu_percent"))},
        {QStringLiteral("working_set"), peakMoment(samples, QStringLiteral("ram_mb"))},
        {QStringLiteral("private_memory"), peakMoment(samples, QStringLiteral("private_mb"))},
        {QStringLiteral("page_faults"), peakMoment(samples, QStringLiteral("page_faults_per_sec"))},
        {QStringLiteral("disk_busy"), peakMoment(samples, QStringLiteral("system_disk_busy_percent"))},
        {QStringLiteral("gpu_usage"), peakMoment(samples, QStringLiteral("gpu_usage_percent"))},
        {QStringLiteral("gpu_temperature"), peakMoment(samples, QStringLiteral("gpu_temperature_c"))},
    });
    return summary;
}

bool validIncidentWindow(double marker, double pre, double post) noexcept
{
    return std::isfinite(marker) && marker >= 0 && std::isfinite(pre) && pre >= 0 && pre <= 300
        && std::isfinite(post) && post >= 0 && post <= 300
        && std::isfinite(marker + post) && (post == 0 || marker + post > marker)
        && (pre == 0 || marker - pre < marker);
}

IncidentWindowSamples normalizeIncidentSamples(const QJsonArray& input, double marker, double pre, double post)
{
    IncidentWindowSamples result;
    result.parametersValid = validIncidentWindow(marker, pre, post);
    if (!result.parametersValid) return result;
    QVector<QJsonObject> ordered;
    for (const auto& value : input) {
        const auto row = value.toObject();
        const auto time = finiteNumber(row.value("monotonic"));
        if (!value.isObject() || !time || *time < 0) { ++result.invalidTimestampCount; continue; }
        if (*time < marker - pre || *time > marker + post) { ++result.outsideWindowCount; continue; }
        ordered.append(row);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return a.value("monotonic").toDouble() < b.value("monotonic").toDouble();
    });
    for (qsizetype i = 0; i < ordered.size();) {
        qsizetype end = i + 1;
        bool conflict = false;
        while (end < ordered.size() && ordered[end].value("monotonic") == ordered[i].value("monotonic")) {
            conflict |= ordered[end] != ordered[i];
            ++end;
        }
        result.duplicateCount += static_cast<int>(end - i - 1);
        if (conflict) ++result.conflictingTimestampCount;
        else result.samples.append(ordered[i]);
        i = end;
    }
    return result;
}

QJsonObject summarizeIncidentSamples(
    const QJsonArray& input,
    const double markerMonotonic,
    const double preSeconds,
    const double postSeconds)
{
    const auto normalized = normalizeIncidentSamples(input, markerMonotonic, preSeconds, postSeconds);
    if (!normalized.parametersValid) return {{"data_quality", "unknown"}, {"sample_count", 0},
        {"window_valid", false}, {"window_error", "invalid_parameters"}};
    auto rows = normalized.samples;
    const bool timestamped = std::any_of(rows.begin(), rows.end(), [](const auto& row) {
        return row.toObject().value("measurement_contract") == kIncidentMeasurementContract;
    });
    if (timestamped) for (qsizetype i = 0; i < rows.size(); ++i) {
        const auto row = rows[i].toObject();
        if (row.value("measurement_contract") != kIncidentMeasurementContract)
            rows[i] = QJsonObject{{"monotonic", row.value("monotonic")}, {"observed_at", row.value("observed_at")}};
    }
    const double focusStart = markerMonotonic - 10.0;
    const auto baseline = selectWindow(rows, markerMonotonic - preSeconds, focusStart, true, false);
    const auto focus = selectWindow(rows, focusStart, markerMonotonic + 5.0);
    const auto recovery = selectWindow(rows, markerMonotonic + 5.0, markerMonotonic + postSeconds, false);

    int beforeCount = 0;
    int afterCount = 0;
    double maxGap = -1.0;
    for (qsizetype index = 0; index < rows.size(); ++index) {
        const double current = rows[index].toObject().value(QStringLiteral("monotonic")).toDouble();
        current <= markerMonotonic ? ++beforeCount : ++afterCount;
        if (index > 0) {
            maxGap = std::max(maxGap, current
                - rows[index - 1].toObject().value(QStringLiteral("monotonic")).toDouble());
        }
    }
    const int expectedPre = preSeconds > 0 ? std::max(1, static_cast<int>(preSeconds * 0.5)) : 0;
    const int expectedPost = postSeconds > 0.0
        ? std::max(1, static_cast<int>(postSeconds * 0.5)) : 0;
    const double leadingGap = rows.isEmpty() ? preSeconds + postSeconds
        : std::max(0.0, rows.first().toObject().value("monotonic").toDouble() - (markerMonotonic - preSeconds));
    const double trailingGap = rows.isEmpty() ? preSeconds + postSeconds
        : std::max(0.0, markerMonotonic + postSeconds - rows.last().toObject().value("monotonic").toDouble());
    const QString quality = !rows.isEmpty() && beforeCount >= expectedPre && afterCount >= expectedPost
            && (maxGap < 0.0 || maxGap <= 3.0)
            && leadingGap <= 3.0 && trailingGap <= 3.0 && normalized.invalidTimestampCount == 0
            && normalized.conflictingTimestampCount == 0
        ? QStringLiteral("valid")
        : rows.size() >= 5 ? QStringLiteral("estimated") : QStringLiteral("stale");

    const QStringList metricKeys {
        QStringLiteral("cpu_usage_percent"), QStringLiteral("cpu_freq_mhz"),
        QStringLiteral("cpu_temp_c"), QStringLiteral("ram_used_percent"),
        QStringLiteral("ram_available_percent"), QStringLiteral("swap_used_percent"),
        QStringLiteral("page_in_mbps"), QStringLiteral("page_out_mbps"),
        QStringLiteral("gpu_usage_percent"), QStringLiteral("gpu_temp_c"),
        QStringLiteral("gpu_vram_used_percent"), QStringLiteral("disk_read_mbps"),
        QStringLiteral("disk_write_mbps"), QStringLiteral("net_download_mbps"),
        QStringLiteral("net_upload_mbps"), QStringLiteral("net_ping_ms"),
    };
    QJsonObject baselineStats;
    QJsonObject focusStats;
    QJsonObject recoveryStats;
    for (const auto& key : metricKeys) {
        baselineStats.insert(key, statistics(baseline, key));
        focusStats.insert(key, statistics(focus, key));
        recoveryStats.insert(key, statistics(recovery, key));
    }
    const auto baselineFrequency = finiteNumber(
        baselineStats.value(QStringLiteral("cpu_freq_mhz")).toObject().value(QStringLiteral("avg")));
    const auto focusFrequency = finiteNumber(
        focusStats.value(QStringLiteral("cpu_freq_mhz")).toObject().value(QStringLiteral("avg")));
    std::optional<double> frequencyDrop;
    if (baselineFrequency.value_or(0.0) > 0.0 && focusFrequency.has_value()) {
        frequencyDrop = std::max(0.0,
            (*baselineFrequency - *focusFrequency) / *baselineFrequency * 100.0);
    }

    bool pagingActive = false;
    bool pagingKnown = false;
    QString pagingQuality = QStringLiteral("stale");
    for (const auto& value : focus) {
        const auto row = value.toObject();
        const QString activity = row.value(QStringLiteral("paging_activity")).toString();
        pagingActive |= activity == QStringLiteral("active");
        pagingKnown |= activity == QStringLiteral("active") || activity == QStringLiteral("idle");
        const QString currentQuality = row.value(QStringLiteral("paging_rate_quality")).toString();
        if (currentQuality == QStringLiteral("valid")) pagingQuality = currentQuality;
        else if (pagingQuality != QStringLiteral("valid") && currentQuality == QStringLiteral("estimated")) pagingQuality = currentQuality;
        else if (pagingQuality == QStringLiteral("stale") && !currentQuality.isEmpty()) pagingQuality = currentQuality;
    }
    const auto availableMin = finiteNumber(
        focusStats.value(QStringLiteral("ram_available_percent")).toObject().value(QStringLiteral("min")));
    const auto ramMax = finiteNumber(
        focusStats.value(QStringLiteral("ram_used_percent")).toObject().value(QStringLiteral("max")));
    const bool lowAvailable = availableMin.has_value() && *availableMin < 10.0;
    const QString memoryPressure = pagingActive && lowAvailable
        ? QStringLiteral("confirmed")
        : pagingActive || lowAvailable || ramMax.value_or(0.0) >= 95.0
            ? QStringLiteral("possible")
            : pagingKnown && availableMin.has_value() ? QStringLiteral("not_observed")
                                                     : QStringLiteral("unknown");

    QJsonArray missing;
    for (const auto& key : {QStringLiteral("cpu_temp_c"), QStringLiteral("cpu_freq_mhz"),
             QStringLiteral("gpu_temp_c"), QStringLiteral("net_ping_ms"),
             QStringLiteral("ram_available_percent")}) {
        if (focusStats.value(key).toObject().value(QStringLiteral("count")).toInt() == 0) missing.append(key);
    }
    if (!pagingKnown) missing.append(QStringLiteral("paging_activity"));
    double netErrors = 0.0;
    double netDrops = 0.0;
    int netErrorsKnown = 0, netDropsKnown = 0;
    int pagingActiveCount = 0;
    for (const auto& value : focus) {
        const auto row = value.toObject();
        const auto errors = finiteNumber(row.value("net_errors_delta"));
        const auto drops = finiteNumber(row.value("net_drops_delta"));
        if (errors && *errors >= 0) { netErrors += *errors; ++netErrorsKnown; }
        if (drops && *drops >= 0) { netDrops += *drops; ++netDropsKnown; }
        if (row.value(QStringLiteral("paging_activity")).toString() == QStringLiteral("active")) ++pagingActiveCount;
    }
    if (!netErrorsKnown) missing.append("net_errors_delta");
    if (!netDropsKnown) missing.append("net_drops_delta");
    const auto counterQuality = [&focus](int known, double sum) {
        return known == 0 || !std::isfinite(sum) ? "unknown" : known == focus.size() ? "valid" : "partial";
    };
    QJsonObject summary {
        {QStringLiteral("data_quality"), quality},
        {"window_valid", true}, {"window_contract", "filtered_temporal_window_v1"},
        {"data_quality_scope", "temporal_window_only"},
        {"invalid_timestamp_count", normalized.invalidTimestampCount},
        {"outside_window_count", normalized.outsideWindowCount},
        {"duplicate_sample_count", normalized.duplicateCount},
        {"conflicting_timestamp_count", normalized.conflictingTimestampCount},
        {"leading_gap_seconds", rows.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(rounded(leadingGap))},
        {"trailing_gap_seconds", rows.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(rounded(trailingGap))},
        {QStringLiteral("sample_count"), rows.size()},
        {QStringLiteral("before_sample_count"), beforeCount},
        {QStringLiteral("after_sample_count"), afterCount},
        {QStringLiteral("baseline_sample_count"), baseline.size()},
        {QStringLiteral("focus_sample_count"), focus.size()},
        {QStringLiteral("recovery_sample_count"), recovery.size()},
        {QStringLiteral("first_sample_at"), rows.isEmpty() ? QJsonValue {QJsonValue::Null}
             : rows.first().toObject().value(QStringLiteral("observed_at"))},
        {QStringLiteral("last_sample_at"), rows.isEmpty() ? QJsonValue {QJsonValue::Null}
             : rows.last().toObject().value(QStringLiteral("observed_at"))},
        {QStringLiteral("max_sample_gap_seconds"), maxGap >= 0.0
             ? QJsonValue {rounded(maxGap)} : QJsonValue {QJsonValue::Null}},
        {QStringLiteral("missing_metrics"), missing},
        {QStringLiteral("baseline"), baselineStats},
        {QStringLiteral("focus"), focusStats},
        {QStringLiteral("recovery"), recoveryStats},
        {QStringLiteral("cpu_frequency_drop_percent"), optionalValue(frequencyDrop, 2)},
        {QStringLiteral("focus_memory_pressure"), memoryPressure},
        {QStringLiteral("focus_paging_activity"), pagingActive ? QStringLiteral("active")
             : pagingKnown ? QStringLiteral("idle") : QStringLiteral("unknown")},
        {QStringLiteral("paging_data_quality"), pagingQuality},
        {QStringLiteral("focus_paging_active_sample_count"), pagingActiveCount},
        {QStringLiteral("focus_net_error_count"), netErrorsKnown && std::isfinite(netErrors) ? QJsonValue(netErrors) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("focus_net_drop_count"), netDropsKnown && std::isfinite(netDrops) ? QJsonValue(netDrops) : QJsonValue(QJsonValue::Null)},
        {"net_counter_scope", "known_intervals_lower_bound"},
        {"net_errors_known_sample_count", netErrorsKnown}, {"net_drops_known_sample_count", netDropsKnown},
        {"net_errors_data_quality", counterQuality(netErrorsKnown, netErrors)},
        {"net_drops_data_quality", counterQuality(netDropsKnown, netDrops)},
        {QStringLiteral("app"), summarizeAppContext(focus).isEmpty()
             ? QJsonValue {QJsonValue::Null} : QJsonValue {summarizeAppContext(focus)}},
    };
    if (timestamped) {
        const auto evidence = incidentCoincidences(baseline, focus);
        for (auto it = evidence.begin(); it != evidence.end(); ++it) summary.insert(it.key(), it.value());
        int stale = 0, repeated = 0, staleApp = 0, partial = 0;
        for (const auto& v : rows) {
            const auto row = v.toObject();
            stale += row.value("stale_metric_count").toInt();
            repeated += row.value("repeated_interval_count").toInt();
            staleApp += row.value("app_context_quality") == "stale_or_untimestamped";
            partial += row.value("metric_data_quality") != "valid";
        }
        summary.insert("stale_metric_count", stale); summary.insert("repeated_interval_count", repeated);
        summary.insert("stale_app_sample_count", staleApp); summary.insert("partial_metric_sample_count", partial);
    }
    return summary;
}

QString appMonitorOutcomeText(const QJsonObject& report)
{
    const auto termination = report.value(QStringLiteral("termination")).toObject();
    if (!report.value(QStringLiteral("launch_error")).toString().isEmpty())
        return QStringLiteral("Не удалось запустить приложение.");
    if (termination.value(QStringLiteral("cancelled")).toBool())
        return QStringLiteral("Автозакрытие отменено; дальнейшее принудительное завершение остановлено. Уже отправленные запросы закрытия отменить нельзя.");
    if (report.value(QStringLiteral("stopped_manually")).toBool())
        return QStringLiteral("Наблюдение остановлено вручную; команда остановки не закрывает приложение.");
    if (report.value(QStringLiteral("timed_out")).toBool()) {
        if (!report.value(QStringLiteral("close_on_timeout")).toBool())
            return QStringLiteral("Лимит наблюдения истёк; приложение оставлено работать.");
        return termination.value(QStringLiteral("all_exited")).toBool()
            ? QStringLiteral("Наблюдение завершено; отслеживаемое приложение закрыто.")
            : QStringLiteral("Наблюдение завершено; не удалось подтвердить закрытие всего дерева процессов.");
    }
    if (report.value(QStringLiteral("exited_early")).toBool())
        return QStringLiteral("Приложение завершилось раньше окончания наблюдения.");
    return QStringLiteral("Наблюдение завершено.");
}

QJsonArray appMonitorReasons(const QJsonObject& report)
{
    const auto saved = report.value("reasons").toArray();
    if (!saved.isEmpty()) return saved;
    QJsonArray reasons;
    const auto error = report.value("launch_error").toString();
    if (!error.isEmpty()) reasons.append(QStringLiteral("Ошибка запуска: %1").arg(error));
    const auto summary = report.value("summary").toObject();
    if (summary.value("sample_count").toInt() < 2 || !summary.value("cpu_avg_percent").isDouble()
        || !summary.value("ram_peak_mb").isDouble()) {
        reasons.append(QStringLiteral("Недостаточно пригодных замеров для уверенного вывода."));
    } else {
        for (const auto& value : report.value("verdict_findings").toArray()) {
            const auto finding = value.toObject();
            const auto detail = finding.value("detail").toString(finding.value("explanation").toString());
            reasons.append(detail.isEmpty() ? finding.value("title") : QJsonValue(detail));
            if (reasons.size() >= 5) break;
        }
        if (reasons.isEmpty()) reasons.append(QStringLiteral("Устойчивых аномалий в полученных замерах не выявлено; редкий сбой вне окна наблюдения не исключён."));
    }
    if (report.value("exited_early").toBool() || report.value("timed_out").toBool()
        || report.value("stopped_manually").toBool()) reasons.append(appMonitorOutcomeText(report));
    return reasons;
}

QString appMonitorVerdictText(const QJsonObject& report)
{
    const auto summary = report.value("summary").toObject();
    QStringList lines{report.value("verdict").toString(QStringLiteral("недостаточно данных")).toUpper()};
    for (const auto& reason : appMonitorReasons(report)) lines.append(QStringLiteral("• %1").arg(reason.toString()));
    lines.append(QStringLiteral("Наблюдение: %1 сек · замеров: %2")
        .arg(formatted(summary.value("observed_seconds")), formatted(summary.value("sample_count"), {}, 0)));
    lines.append(QStringLiteral("WS: старт %1 МБ · пик %2 МБ · рост %3 МБ/мин")
        .arg(formatted(summary.value("ram_start_mb")), formatted(summary.value("ram_peak_mb")),
             formatted(summary.value("ram_growth_mb_per_min"))));
    lines.append(QStringLiteral("Private: старт %1 МБ · пик %2 МБ")
        .arg(formatted(summary.value("private_start_mb")), formatted(summary.value("private_peak_mb"))));
    lines.append(QStringLiteral("Пик page faults: %1 /с · handles: %2")
        .arg(formatted(summary.value("page_faults_peak_per_sec")), formatted(summary.value("handle_peak"), {}, 0)));
    int count = 0;
    for (const auto& step : report.value("action_plan").toArray()) {
        const auto action = step.isString() ? step.toString() : step.toObject().value("action").toString();
        if (!action.isEmpty()) { lines.append(QStringLiteral("%1. %2").arg(++count).arg(action)); if (count == 4) break; }
    }
    const auto coverage = report.value("coverage").toObject();
    if (coverage.value("level").toString() != "complete" && !coverage.value("message").toString().isEmpty())
        lines.append(coverage.value("message").toString());
    return lines.join(u'\n');
}

QString appMonitorReportToText(const QJsonObject& report)
{
    const auto summary = report.value("summary").toObject();
    const auto termination = report.value("termination").toObject();
    const auto raw = [](const QJsonValue& value) {
        if (value.isString()) return value.toString();
        if (value.isDouble()) return QString::number(value.toDouble(), 'g', 12);
        if (value.isBool()) return value.toBool() ? QStringLiteral("да") : QStringLiteral("нет");
        return QStringLiteral("н/д");
    };
    const auto s = [&summary](const char* key) { return formatted(summary.value(QLatin1String(key))); };
    const auto percent = [&summary](const char* key) {
        const auto value = finiteNumber(summary.value(QLatin1String(key)));
        return value ? formatted(*value * 100.0, QStringLiteral("%")) : QStringLiteral("н/д");
    };
    QStringList lines{
        QStringLiteral("O.R.I.O.N. — НАБЛЮДЕНИЕ ПРОБЛЕМНОГО ПРИЛОЖЕНИЯ"),
        QStringLiteral("Приложение: %1").arg(raw(report.value("exe_path"))),
        QStringLiteral("Начало: %1; окончание: %2").arg(raw(report.value("started_at")), raw(report.value("finished_at"))),
        QStringLiteral("План: %1 сек; активное время: %2 сек; пауза: %3 сек")
            .arg(formatted(report.value("duration_seconds")), formatted(report.value("active_seconds"), {}, 3),
                 formatted(report.value("paused_seconds"), {}, 3)),
        QStringLiteral("Замеров: %1; фактически: %2 сек; интервал: %3 сек")
            .arg(formatted(summary.value("sample_count"), {}, 0), s("observed_seconds"), s("sample_interval_seconds")),
        {}, appMonitorVerdictText(report), {}, QStringLiteral("ЗАВЕРШЕНИЕ"),
        QStringLiteral("Результат: %1").arg(appMonitorOutcomeText(report)),
        QStringLiteral("Ошибка запуска: %1").arg(report.value("launch_error").toString(QStringLiteral("нет"))),
        QStringLiteral("Ранний выход: %1; остановка вручную: %2; таймаут: %3; автозакрытие включено: %4")
            .arg(raw(report.value("exited_early")), raw(report.value("stopped_manually")),
                 raw(report.value("timed_out")), raw(report.value("close_on_timeout"))),
    };
    const auto exitCode = report.value("exit_code");
    if (report.value("timing_contract") == "pause_transitions_steady_clock_v1") {
        lines.append(QStringLiteral("Полное время наблюдения: %1 сек (активное + паузы).")
            .arg(formatted(report.value("observation_wall_seconds"), {}, 3)));
        lines.append(QStringLiteral("Подготовка, автозакрытие и составление отчёта в это время не входят."));
    }
    lines.append(QStringLiteral("Код выхода: %1").arg(exitCode.isDouble()
        ? QStringLiteral("0x%1 (%2)").arg(static_cast<quint32>(exitCode.toInteger()), 8, 16, QChar('0')).arg(exitCode.toInteger())
        : QStringLiteral("н/д")));
    const auto exitDetails = report.value("exit_details").toObject();
    if (!exitDetails.value("name").toString().isEmpty()) {
        lines.append(QStringLiteral("Расшифровка выхода: %1; %2")
            .arg(raw(exitDetails.value("name")), raw(exitDetails.value("category"))));
        lines.append(QStringLiteral("Совпадение кода с известной ошибкой само по себе не доказывает причину завершения."));
    }
    lines.append(QStringLiteral("Попытка закрытия: %1; WM_CLOSE: %2; все вышли: %3; отменено: %4")
        .arg(raw(termination.value("attempted")), raw(termination.value("close_messages_sent")),
             raw(termination.value("all_exited")), raw(termination.value("cancelled"))));
    lines.append(QStringLiteral("Мягкое закрытие: %1").arg(raw(termination.value("graceful"))));
    for (const auto& key : {"graceful_pids", "terminated_pids", "killed_pids", "survivor_pids"}) {
        QStringList pids;
        for (const auto& pid : termination.value(QLatin1String(key)).toArray()) pids.append(raw(pid));
        if (!pids.isEmpty()) lines.append(QStringLiteral("%1: %2").arg(QLatin1String(key), pids.join(", ")));
    }
    for (const auto& error : termination.value("errors").toArray())
        lines.append(QStringLiteral("Ограничение закрытия: %1").arg(raw(error)));
    lines << "" << QStringLiteral("CPU, ПРОЦЕССЫ И ОТКЛИК ОКНА")
          << QStringLiteral("CPU дерева avg/peak: %1% / %2%; доля ≥80%: %3").arg(s("cpu_avg_percent"), s("cpu_peak_percent"), percent("cpu_high_fraction"))
          << QStringLiteral("Пик процессов / потоков: %1 / %2").arg(s("process_count_peak"), s("thread_count_peak"))
          << QStringLiteral("Context switches process avg/peak: %1 / %2 /с; system avg/peak: %3 / %4 /с")
                .arg(s("context_switches_avg_per_sec"), s("context_switches_peak_per_sec"), s("system_context_switches_avg_per_sec"), s("system_context_switches_peak_per_sec"))
          << QStringLiteral("Окно без отклика: %1; максимальная серия %2 сек").arg(percent("ui_hung_fraction"), s("ui_hung_max_streak_seconds"))
          << "" << QStringLiteral("ПАМЯТЬ");
    const auto memory = report.value("runtime_memory").toObject();
    if (memory.value("observation_kind") == "timestamped_cache_endpoints") {
        lines << QStringLiteral("Системные снимки до/после: %1").arg(raw(memory.value("data_quality")));
        for (const auto& endpoint : {QStringLiteral("before"), QStringLiteral("after")}) {
            const auto snapshot = memory.value(endpoint).toObject();
            lines << QStringLiteral("  %1: %2; возраст кэша %3 мс; качество %4; причина %5")
                .arg(endpoint, raw(snapshot.value("system_observed_at")), formatted(snapshot.value("system_age_ms"), {}, 0),
                     raw(snapshot.value("system_data_quality")), raw(snapshot.value("system_sample_reason")));
        }
        lines << memory.value("note").toString();
        lines << QStringLiteral("Замеров с неполными системными данными: %1; с устаревшими: %2")
            .arg(s("system_unavailable_sample_count"), s("system_stale_sample_count"));
    }
    for (const auto& prefix : {QStringLiteral("ram"), QStringLiteral("private")}) {
        const auto metric = [&summary, &prefix](const char* suffix) {
            return formatted(summary.value(prefix + QLatin1String(suffix)), {}, QLatin1String(suffix) == "_growth_r2" ? 3 : 1);
        };
        lines << QStringLiteral("%1 start/end/peak: %2 / %3 / %4 МБ")
                    .arg(prefix == "ram" ? QStringLiteral("Working set") : QStringLiteral("Private memory"),
                         metric("_start_mb"), metric("_end_mb"), metric("_peak_mb"))
              << QStringLiteral("  Рост: %1 МБ; тренд: %2 МБ/мин; R²: %3").arg(metric("_growth_mb"), metric("_growth_mb_per_min"), metric("_growth_r2"))
              << QStringLiteral("  Первая/последняя четверть avg: %1 / %2 МБ; хвост: %3 МБ; снижение от пика: %4 МБ")
                    .arg(metric("_first_quarter_avg_mb"), metric("_last_quarter_avg_mb"), metric("_tail_growth_mb"), metric("_recovery_from_peak_mb"));
    }
    lines << QStringLiteral("Доля переходов с ростом Working set: %1; известных переходов: %2")
                .arg(percent("ram_growth_positive_fraction"), s("ram_growth_step_count"))
          << QStringLiteral("Доля рассчитана между известными значениями, не по времени; пропуски не означают отсутствие роста.")
          << QStringLiteral("Private plateau: %1").arg(raw(summary.value("private_plateau_detected")))
          << QStringLiteral("Handles start/end/peak: %1 / %2 / %3; рост: %4; тренд: %5 /мин; R²: %6")
                .arg(s("handle_start"), s("handle_end"), s("handle_peak"), s("handle_growth"), s("handle_growth_per_min"), s("handle_growth_r2"))
          << QStringLiteral("Page faults avg/peak: %1 / %2 /с").arg(s("page_faults_avg_per_sec"), s("page_faults_peak_per_sec"))
          << QStringLiteral("Доля RAM приложения peak: %1%; RAM системы peak: %2%; свободно min: %3%; commit peak: %4%")
                .arg(s("ram_share_peak_percent"), s("system_ram_peak_percent"), s("system_available_min_percent"), s("system_commit_peak_percent"))
          << QStringLiteral("Доля известных замеров с активной подкачкой: %1").arg(percent("paging_active_fraction"))
          << "" << QStringLiteral("ДИСК И GPU СИСТЕМЫ")
          << QStringLiteral("I/O read/write peak: %1 / %2 МБ/с; total: %3 / %4 МБ")
                .arg(s("read_peak_mbps"), s("write_peak_mbps"), s("read_total_mb"), s("write_total_mb"))
          << QStringLiteral("Диск busy peak: %1%; latency R/W peak: %2 / %3 мс")
                .arg(s("system_disk_busy_peak_percent"), s("system_disk_read_latency_peak_ms"), s("system_disk_write_latency_peak_ms"))
          << QStringLiteral("GPU: %1; usage avg/peak: %2% / %3%; температура peak: %4°C")
                .arg(raw(summary.value("gpu_name")), s("gpu_usage_avg_percent"), s("gpu_usage_peak_percent"), s("gpu_temperature_peak_c"))
          << QStringLiteral("VRAM peak: %1 МБ; питание peak: %2 Вт; частота min/max: %3 / %4 МГц")
                .arg(s("gpu_vram_peak_mb"), s("gpu_power_peak_w"), s("gpu_clock_min_mhz"), s("gpu_clock_max_mhz"))
          ;
    const auto peaks = summary.value("peak_moments").toObject();
    if (report.value("io_total_scope") == "observed_intervals_lower_bound")
        lines << QStringLiteral("I/O total — только измеренные приращения: нижняя граница, не полный объём за жизнь процессов. Работа до первого замера, во время паузы и в пропусках не добавляется. Н/д скорости — нет полного соседнего интервала дерева.");
    lines << "" << QStringLiteral("ПИКОВЫЕ МОМЕНТЫ");
    const QList<QPair<QString, QString>> names{{"cpu", "CPU (%)"}, {"working_set", "Working set (МБ)"},
        {"private_memory", "Private (МБ)"}, {"page_faults", "Page faults (/с)"}, {"disk_busy", "Диск (%)"},
        {"gpu_usage", "GPU (%)"}, {"gpu_temperature", "GPU (°C)"}};
    bool anyPeak = false;
    for (const auto& [key, label] : names) {
        const auto moment = peaks.value(key).toObject();
        if (!moment.value("value").isDouble()) continue;
        anyPeak = true;
        lines.append(QStringLiteral("%1: %2; +%3 сек; %4").arg(label, formatted(moment.value("value")),
            formatted(moment.value("elapsed")), raw(moment.value("observed_at"))));
    }
    if (!anyPeak) lines.append(QStringLiteral("Нет доступных замеров."));
    lines << "" << QStringLiteral("ЭПИЗОДЫ ОТСУТСТВИЯ ОТКЛИКА");
    const auto hung = summary.value("hung_observations").toArray();
    for (qsizetype i = 0; i < std::min<qsizetype>(20, hung.size()); ++i) {
        const auto row = hung[i].toObject(); QStringList titles;
        for (const auto& title : row.value("titles").toArray()) titles.append(raw(title));
        lines.append(QStringLiteral("+%1 сек; %2; %3").arg(formatted(row.value("elapsed")), raw(row.value("observed_at")), titles.join(" | ")));
    }
    if (hung.isEmpty()) lines.append(QStringLiteral("Нет записанных эпизодов (не гарантирует наличие данных об отклике)."));
    if (hung.size() > 20) lines.append(QStringLiteral("Ещё записей: %1").arg(hung.size() - 20));
    lines << "" << QStringLiteral("ПОСЛЕДНИЙ ДОСТУПНЫЙ СПИСОК ПРОЦЕССОВ");
    const auto processes = summary.value("top_processes_last").toArray();
    for (const auto& value : processes) {
        const auto row = value.toObject();
        lines.append(QStringLiteral("%1 · PID %2 · CPU %3% · WS %4 МБ · private %5 МБ · handles %6")
            .arg(raw(row.value("name")), raw(row.value("pid")), formatted(row.value("cpu_percent")),
                 formatted(row.value("rss_mb")), formatted(row.value("private_mb")), formatted(row.value("handle_count"), {}, 0)));
    }
    if (processes.isEmpty()) lines.append(QStringLiteral("Нет доступных данных."));
    lines << "" << QStringLiteral("НОВЫЕ СИСТЕМНЫЕ СОБЫТИЯ")
          << QStringLiteral("Сравнение журнала доступно: %1; качество: %2")
                .arg(raw(report.value("system_errors_checked_after")), raw(report.value("system_error_comparison_quality")));
    const auto events = report.value("new_system_errors").toArray();
    lines.append(QStringLiteral("Записей: %1").arg(events.size()));
    const auto fallback = [](const QJsonObject& row, const char* key, const char* other) {
        const auto value = row.value(QLatin1String(key));
        return value.isUndefined() || value.isNull() ? row.value(QLatin1String(other)) : value;
    };
    for (qsizetype i = 0; i < std::min<qsizetype>(20, events.size()); ++i) {
        if (events[i].isString()) { lines.append(events[i].toString()); continue; }
        const auto row = events[i].toObject();
        lines.append(QStringLiteral("%1 · %2 · ID %3 · %4").arg(raw(fallback(row, "timestamp", "time")),
            raw(fallback(row, "source", "provider")), raw(fallback(row, "event_id", "id")), raw(fallback(row, "message", "text"))));
    }
    if (events.size() > 20) lines.append(QStringLiteral("Ещё событий: %1").arg(events.size() - 20));
    lines << "" << QStringLiteral("ДИАГНОСТИЧЕСКИЕ ВЫВОДЫ");
    auto findings = report.value("findings").toArray();
    if (findings.isEmpty()) findings = report.value("verdict_findings").toArray();
    for (const auto& value : findings) {
        const auto item = value.toObject();
        lines.append(QStringLiteral("[%1] %2 · уверенность: %3").arg(raw(item.value("severity")),
            raw(item.value("title")), raw(item.value("confidence"))));
        lines.append(item.value("detail").toString(item.value("explanation").toString()));
        const auto evidence = item.value("evidence").toArray();
        for (qsizetype i = 0; i < std::min<qsizetype>(8, evidence.size()); ++i) {
            const auto row = evidence[i].toObject();
            lines.append(QStringLiteral("  %1: %2 (%3)").arg(raw(fallback(row, "label", "metric")),
                raw(row.value("value")), raw(row.value("source"))));
        }
    }
    if (findings.isEmpty()) lines.append(QStringLiteral("Выводы не сформированы; отсутствие выводов само по себе не означает исправность."));
    lines << "" << QStringLiteral("ПЛАН ДЕЙСТВИЙ");
    for (const auto& step : report.value("action_plan").toArray())
        lines.append(QStringLiteral("Что сделать: %1").arg(step.isString() ? step.toString() : step.toObject().value("action").toString()));
    lines << "" << QStringLiteral("ПОКРЫТИЕ ПРОВЕРКИ")
          << report.value("coverage").toObject().value("message").toString(QStringLiteral("н/д"))
          << QStringLiteral("Редкий сбой вне окна наблюдения не исключён. GPU и диск — показатели системы, не только выбранного приложения.");
    return lines.join(u'\n');
}

} // namespace orion::diagnostics
