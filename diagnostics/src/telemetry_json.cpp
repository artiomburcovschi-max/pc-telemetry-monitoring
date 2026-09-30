#include "orion/diagnostics/telemetry_json.h"

#include "orion/core/data_quality.h"

#include <QDateTime>
#include <QJsonArray>
#include <QString>
#include <QTimeZone>

#include <chrono>
#include <cstdint>

namespace orion::diagnostics {
namespace {

[[nodiscard]] QString isoTimestamp(
    const std::chrono::system_clock::time_point timestamp)
{
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch());
    return QDateTime::fromMSecsSinceEpoch(milliseconds.count(), QTimeZone::UTC)
        .toString(Qt::ISODateWithMs);
}

template<typename T, typename Converter>
[[nodiscard]] QJsonObject metricObject(
    const orion::core::Metric<T>& metric,
    Converter converter)
{
    QJsonObject object {
        {QStringLiteral("quality"), QString::fromLatin1(
             orion::core::toString(metric.quality).data(),
             static_cast<qsizetype>(orion::core::toString(metric.quality).size()))},
    };
    if (metric.value.has_value()) {
        object.insert(QStringLiteral("value"), converter(*metric.value));
    }
    if (metric.observedAt.has_value()) {
        object.insert(QStringLiteral("observed_at"), isoTimestamp(*metric.observedAt));
    }
    if (!metric.source.empty()) {
        object.insert(QStringLiteral("source"), QString::fromUtf8(metric.source));
    }
    if (!metric.reason.empty()) {
        object.insert(QStringLiteral("reason"), QString::fromUtf8(metric.reason));
    }
    return object;
}

[[nodiscard]] QJsonObject doubleMetric(const orion::core::Metric<double>& metric)
{
    return metricObject(metric, [](const double value) { return QJsonValue {value}; });
}

[[nodiscard]] QJsonObject integerMetric(
    const orion::core::Metric<std::uint64_t>& metric)
{
    return metricObject(metric, [](const std::uint64_t value) {
        return QJsonValue {static_cast<double>(value)};
    });
}

[[nodiscard]] QJsonObject coresMetric(
    const orion::core::Metric<std::vector<orion::core::CpuCoreLoad>>& metric)
{
    return metricObject(metric, [](const auto& cores) {
        QJsonArray values;
        for (const auto& core : cores) {
            values.append(QJsonObject {
                {QStringLiteral("index"), static_cast<int>(core.index)},
                {QStringLiteral("percent"), core.percent},
            });
        }
        return QJsonValue {values};
    });
}

[[nodiscard]] QJsonObject disksMetric(
    const orion::core::Metric<std::vector<orion::core::DiskVolume>>& metric)
{
    return metricObject(metric, [](const auto& disks) {
        QJsonArray values;
        for (const auto& disk : disks) {
            QJsonObject object {
                {QStringLiteral("name"), QString::fromUtf8(disk.name)},
                {QStringLiteral("mount_point"), QString::fromUtf8(disk.mountPoint)},
                {QStringLiteral("file_system"), QString::fromUtf8(disk.fileSystem)},
                {QStringLiteral("storage_type"), QString::fromUtf8(disk.storageType)},
                {QStringLiteral("total_bytes"), static_cast<double>(disk.totalBytes)},
                {QStringLiteral("used_bytes"), static_cast<double>(disk.usedBytes)},
                {QStringLiteral("free_bytes"), static_cast<double>(disk.freeBytes)},
                {QStringLiteral("used_percent"), disk.usedPercent},
            };
            if (disk.readBytesPerSecond.has_value()) {
                object.insert(QStringLiteral("read_bytes_per_second"), *disk.readBytesPerSecond);
            }
            if (disk.writeBytesPerSecond.has_value()) {
                object.insert(QStringLiteral("write_bytes_per_second"), *disk.writeBytesPerSecond);
            }
            if (disk.totalReadBytes.has_value()) {
                object.insert(QStringLiteral("total_read_bytes"), static_cast<double>(*disk.totalReadBytes));
            }
            if (disk.totalWrittenBytes.has_value()) {
                object.insert(QStringLiteral("total_written_bytes"), static_cast<double>(*disk.totalWrittenBytes));
            }
            if (disk.busyPercent.has_value()) {
                object.insert(QStringLiteral("busy_percent"), *disk.busyPercent);
            }
            if (disk.readLatencyMs.has_value()) {
                object.insert(QStringLiteral("read_latency_ms"), *disk.readLatencyMs);
            }
            if (disk.writeLatencyMs.has_value()) {
                object.insert(QStringLiteral("write_latency_ms"), *disk.writeLatencyMs);
            }
            values.append(object);
        }
        return QJsonValue {values};
    });
}

[[nodiscard]] QJsonObject temperaturesMetric(
    const orion::core::Metric<std::vector<orion::core::TemperatureSensor>>& metric)
{
    return metricObject(metric, [](const auto& sensors) {
        QJsonArray values;
        for (const auto& sensor : sensors) {
            QJsonObject object {
                {QStringLiteral("component"), QString::fromLatin1(
                     orion::core::toString(sensor.component).data(),
                     static_cast<qsizetype>(orion::core::toString(sensor.component).size()))},
                {QStringLiteral("label"), QString::fromUtf8(sensor.label)},
                {QStringLiteral("value_c"), sensor.valueC},
                {QStringLiteral("source"), QString::fromUtf8(sensor.source)},
                {QStringLiteral("quality"), QStringLiteral("valid")},
            };
            if (sensor.highC.has_value()) {
                object.insert(QStringLiteral("high_c"), *sensor.highC);
            }
            if (sensor.criticalC.has_value()) {
                object.insert(QStringLiteral("critical_c"), *sensor.criticalC);
            }
            if (!sensor.identifier.empty()) {
                object.insert(QStringLiteral("identifier"), QString::fromUtf8(sensor.identifier));
            }
            values.append(object);
        }
        return QJsonValue {values};
    });
}

[[nodiscard]] QJsonObject fansMetric(
    const orion::core::Metric<std::vector<orion::core::FanSensor>>& metric)
{
    return metricObject(metric, [](const auto& sensors) {
        QJsonArray values;
        for (const auto& sensor : sensors) {
            QJsonObject object {
                {QStringLiteral("component"), QString::fromLatin1(
                     orion::core::toString(sensor.component).data(),
                     static_cast<qsizetype>(orion::core::toString(sensor.component).size()))},
                {QStringLiteral("label"), QString::fromUtf8(sensor.label)},
                {QStringLiteral("source"), QString::fromUtf8(sensor.source)},
                {QStringLiteral("quality"), QStringLiteral("valid")},
            };
            if (sensor.rpm.has_value()) {
                object.insert(QStringLiteral("rpm"), *sensor.rpm);
            }
            if (sensor.percent.has_value()) {
                object.insert(QStringLiteral("percent"), *sensor.percent);
            }
            if (!sensor.identifier.empty()) {
                object.insert(QStringLiteral("identifier"), QString::fromUtf8(sensor.identifier));
            }
            values.append(object);
        }
        return QJsonValue {values};
    });
}

} // namespace

QJsonObject telemetryToJson(const orion::core::TelemetryData& data)
{
    const QJsonObject metrics {
        {QStringLiteral("cpu_usage_percent"), doubleMetric(data.cpuUsagePercent)},
        {QStringLiteral("cpu_core_percent"), coresMetric(data.cpuCores)},
        {QStringLiteral("cpu_freq_mhz"), doubleMetric(data.cpuFrequencyMhz)},
        {QStringLiteral("cpu_temp_c"), doubleMetric(data.cpuTemperatureC)},
        {QStringLiteral("ram_used_percent"), doubleMetric(data.ramUsagePercent)},
        {QStringLiteral("ram_total_bytes"), integerMetric(data.ramTotalBytes)},
        {QStringLiteral("ram_available_percent"), doubleMetric(data.ramAvailablePercent)},
        {QStringLiteral("swap_used_percent"), doubleMetric(data.swapUsedPercent)},
        {QStringLiteral("paging_in_bytes"), integerMetric(data.pagingInBytes)},
        {QStringLiteral("paging_out_bytes"), integerMetric(data.pagingOutBytes)},
        {QStringLiteral("commit_used_bytes"), integerMetric(data.commitUsedBytes)},
        {QStringLiteral("commit_limit_bytes"), integerMetric(data.commitLimitBytes)},
        {QStringLiteral("commit_peak_bytes"), integerMetric(data.commitPeakBytes)},
        {QStringLiteral("commit_used_percent"), doubleMetric(data.commitUsedPercent)},
        {QStringLiteral("pagefile_used_percent"), doubleMetric(data.pagefileUsedPercent)},
        {QStringLiteral("pagefile_peak_percent"), doubleMetric(data.pagefilePeakPercent)},
        {QStringLiteral("pages_input_per_second"), doubleMetric(data.pagesInputPerSecond)},
        {QStringLiteral("page_reads_per_second"), doubleMetric(data.pageReadsPerSecond)},
        {QStringLiteral("pages_per_second"), doubleMetric(data.pagesPerSecond)},
        {QStringLiteral("pages_output_per_second"), doubleMetric(data.pagesOutputPerSecond)},
        {QStringLiteral("page_writes_per_second"), doubleMetric(data.pageWritesPerSecond)},
        {QStringLiteral("system_context_switches_per_second"), doubleMetric(data.systemContextSwitchesPerSecond)},
        {QStringLiteral("disk_busy_percent"), doubleMetric(data.diskBusyPercent)},
        {QStringLiteral("disk_read_latency_ms"), doubleMetric(data.diskReadLatencyMs)},
        {QStringLiteral("disk_write_latency_ms"), doubleMetric(data.diskWriteLatencyMs)},
        {QStringLiteral("disk_volumes"), disksMetric(data.disks)},
        {QStringLiteral("temperature_sensors"), temperaturesMetric(data.temperatures)},
        {QStringLiteral("fan_sensors"), fansMetric(data.fans)},
        {QStringLiteral("gpu_usage_percent"), doubleMetric(data.gpuUsagePercent)},
        {QStringLiteral("gpu_temp_c"), doubleMetric(data.gpuTemperatureC)},
        {QStringLiteral("gpu_vram_total_mb"), doubleMetric(data.gpuMemoryTotalMb)},
        {QStringLiteral("gpu_vram_used_mb"), doubleMetric(data.gpuMemoryUsedMb)},
        {QStringLiteral("gpu_vram_used_percent"), doubleMetric(data.gpuMemoryPercent)},
        {QStringLiteral("net_download_bytes_per_second"), doubleMetric(data.netDownloadBytesPerSecond)},
        {QStringLiteral("net_upload_bytes_per_second"), doubleMetric(data.netUploadBytesPerSecond)},
        {QStringLiteral("net_ping_ms"), doubleMetric(data.netPingMs)},
        {QStringLiteral("net_total_received_bytes"), integerMetric(data.netTotalReceivedBytes)},
        {QStringLiteral("net_total_sent_bytes"), integerMetric(data.netTotalSentBytes)},
        {QStringLiteral("net_total_errors"), integerMetric(data.totalErrors)},
        {QStringLiteral("net_total_drops"), integerMetric(data.totalDrops)},
    };
    return {
        {QStringLiteral("schema_version"), kTelemetrySchemaVersion},
        {QStringLiteral("observed_at"), isoTimestamp(data.collectedAt)},
        {QStringLiteral("system"), QJsonObject {
             {QStringLiteral("os_name"), QString::fromUtf8(data.osName)},
             {QStringLiteral("cpu_name"), QString::fromUtf8(data.cpuName)},
             {QStringLiteral("gpu_name"), QString::fromUtf8(data.gpuName)},
         }},
        {QStringLiteral("metrics"), metrics},
    };
}

} // namespace orion::diagnostics
