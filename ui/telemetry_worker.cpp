#include "telemetry_worker.h"

#include "orion/platform/system_backend.h"
#include "orion/core/memory_pressure.h"
#include "orion/diagnostics/app_monitor_data.h"
#include "orion/diagnostics/incident_data.h"

#include <QString>
#include <QElapsedTimer>
#include <QTimeZone>

#include <chrono>

namespace orion::app {

TelemetryWorker::TelemetryWorker(QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<QVector<DiskTelemetry>>();
    qRegisterMetaType<RuntimeTelemetry>();
    qRegisterMetaType<QVector<TemperatureTelemetry>>();
    qRegisterMetaType<QVector<FanTelemetry>>();
}

void TelemetryWorker::setPaused(const bool paused)
{
    if (paused_.exchange(paused, std::memory_order_acq_rel) != paused)
        pauseGeneration_.fetch_add(1, std::memory_order_acq_rel);
    waitCondition_.wakeAll();
}

bool TelemetryWorker::isPaused() const noexcept
{
    return paused_.load(std::memory_order_acquire);
}

void TelemetryWorker::stop()
{
    requestInterruption();
    waitCondition_.wakeAll();
}

void TelemetryWorker::run()
{
    auto backend = orion::platform::makeSystemBackend();
    if (!backend) {
        return;
    }

    orion::core::NetworkCounterTracker networkCounters;
    std::uint64_t networkGeneration = pauseGeneration_.load(std::memory_order_acquire);
    while (!isInterruptionRequested()) {
        if (isPaused()) {
            waitMutex_.lock();
            while (isPaused() && !isInterruptionRequested()) {
                waitCondition_.wait(&waitMutex_, 250);
            }
            waitMutex_.unlock();
            continue;
        }
        const auto generation = pauseGeneration_.load(std::memory_order_acquire);
        if (generation != networkGeneration) {
            networkCounters.reset();
            networkGeneration = generation;
        }
        const auto data = backend->sample(std::chrono::milliseconds {200});
        QVector<double> coreLoads;
        if (data.cpuCores.usable()) {
            coreLoads.reserve(static_cast<qsizetype>(data.cpuCores.value->size()));
            for (const auto& core : *data.cpuCores.value) {
                coreLoads.append(core.percent);
            }
        }
        QVector<double> coreFrequenciesMhz;
        if (data.cpuCoreFrequenciesMhz.usable()) {
            coreFrequenciesMhz.reserve(
                static_cast<qsizetype>(data.cpuCoreFrequenciesMhz.value->size()));
            for (const double frequency : *data.cpuCoreFrequenciesMhz.value) {
                coreFrequenciesMhz.append(frequency);
            }
        }
        if (isPaused() || isInterruptionRequested()
            || generation != pauseGeneration_.load(std::memory_order_acquire)) {
            continue;
        }
        QVector<DiskTelemetry> disks;
        if (data.disks.usable()) {
            disks.reserve(static_cast<qsizetype>(data.disks.value->size()));
            constexpr double bytesPerGiB = 1024.0 * 1024.0 * 1024.0;
            constexpr double bytesPerMiB = 1024.0 * 1024.0;
            for (const auto& disk : *data.disks.value) {
                disks.append({
                    QString::fromUtf8(disk.name),
                    QString::fromUtf8(disk.mountPoint),
                    QString::fromUtf8(disk.fileSystem),
                    QString::fromUtf8(disk.storageType),
                    static_cast<double>(disk.totalBytes) / bytesPerGiB,
                    static_cast<double>(disk.usedBytes) / bytesPerGiB,
                    static_cast<double>(disk.freeBytes) / bytesPerGiB,
                    disk.usedPercent,
                    disk.readBytesPerSecond.has_value()
                        ? *disk.readBytesPerSecond / bytesPerMiB : -1.0,
                    disk.writeBytesPerSecond.has_value()
                        ? *disk.writeBytesPerSecond / bytesPerMiB : -1.0,
                    disk.totalReadBytes.has_value()
                        ? static_cast<double>(*disk.totalReadBytes) / bytesPerGiB : -1.0,
                    disk.totalWrittenBytes.has_value()
                        ? static_cast<double>(*disk.totalWrittenBytes) / bytesPerGiB : -1.0,
                    disk.busyPercent.value_or(-1.0),
                    disk.readLatencyMs.value_or(-1.0),
                    disk.writeLatencyMs.value_or(-1.0),
                });
            }
        }
        const auto optionalValue = [](const orion::core::Metric<double>& metric) {
            return metric.usable() ? metric.value : std::optional<double> {};
        };
        const auto assessment = orion::core::assessWindowsPaging(
            optionalValue(data.ramAvailablePercent),
            optionalValue(data.commitUsedPercent),
            optionalValue(data.pagesInputPerSecond),
            optionalValue(data.pageReadsPerSecond),
            optionalValue(data.pagesPerSecond));
        RuntimeTelemetry runtime;
        QElapsedTimer sourceClock;
        sourceClock.start();
        // Match Metric::observedAt's clock; Qt and std clock precision can differ on Windows.
        const auto sourceUtc = QDateTime::fromMSecsSinceEpoch(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count(), QTimeZone::UTC);
        runtime.appMonitorSystemEnvelope = orion::diagnostics::appSystemEnvelope(
            data, sourceClock.msecsSinceReference(), sourceUtc);
        runtime.network = networkCounters.update(data);
        runtime.incidentSystemEnvelope = orion::diagnostics::incidentSystemEnvelope(
            data, runtime.network, sourceClock.msecsSinceReference(), sourceUtc);
        runtime.ramAvailablePercent = data.ramAvailablePercent.valueOr(-1.0);
        runtime.swapUsedPercent = data.swapUsedPercent.valueOr(-1.0);
        runtime.commitUsedPercent = data.commitUsedPercent.valueOr(-1.0);
        runtime.commitUsedMiB = data.commitUsedBytes.usable()
            ? static_cast<double>(*data.commitUsedBytes.value) / (1024.0 * 1024.0) : -1.0;
        runtime.commitLimitMiB = data.commitLimitBytes.usable()
            ? static_cast<double>(*data.commitLimitBytes.value) / (1024.0 * 1024.0) : -1.0;
        runtime.pagefileUsedPercent = data.pagefileUsedPercent.valueOr(-1.0);
        runtime.pagesInputPerSecond = data.pagesInputPerSecond.valueOr(-1.0);
        runtime.pageReadsPerSecond = data.pageReadsPerSecond.valueOr(-1.0);
        runtime.pagesPerSecond = data.pagesPerSecond.valueOr(-1.0);
        runtime.pagesOutputPerSecond = data.pagesOutputPerSecond.valueOr(-1.0);
        runtime.pageWritesPerSecond = data.pageWritesPerSecond.valueOr(-1.0);
        runtime.systemContextSwitchesPerSecond = data.systemContextSwitchesPerSecond.valueOr(-1.0);
        runtime.diskBusyPercent = data.diskBusyPercent.valueOr(-1.0);
        runtime.diskReadLatencyMs = data.diskReadLatencyMs.valueOr(-1.0);
        runtime.diskWriteLatencyMs = data.diskWriteLatencyMs.valueOr(-1.0);
        runtime.pagingActivity = QString::fromStdString(assessment.pagingActivity);
        runtime.hardFaultActivity = QString::fromStdString(assessment.hardFaultActivity);
        runtime.memoryPressure = QString::fromStdString(assessment.memoryPressure);
        runtime.pagingInterpretation = QString::fromStdString(assessment.interpretation);
        if (data.pagesInputPerSecond.usable() || data.pageReadsPerSecond.usable()
            || data.pagesPerSecond.usable()) {
            runtime.pagingRateQuality = QStringLiteral("estimated");
        }
        QVector<TemperatureTelemetry> temperatures;
        if (data.temperatures.usable()) {
            temperatures.reserve(static_cast<qsizetype>(data.temperatures.value->size()));
            for (const auto& sensor : *data.temperatures.value) {
                temperatures.append({
                    QString::fromLatin1(
                        orion::core::toString(sensor.component).data(),
                        static_cast<qsizetype>(orion::core::toString(sensor.component).size())),
                    QString::fromUtf8(sensor.label),
                    sensor.valueC,
                    sensor.highC.value_or(-1.0),
                    sensor.criticalC.value_or(-1.0),
                    QString::fromUtf8(sensor.source),
                });
            }
        }
        QVector<FanTelemetry> fans;
        if (data.fans.usable()) {
            fans.reserve(static_cast<qsizetype>(data.fans.value->size()));
            for (const auto& sensor : *data.fans.value) {
                fans.append({
                    QString::fromLatin1(
                        orion::core::toString(sensor.component).data(),
                        static_cast<qsizetype>(orion::core::toString(sensor.component).size())),
                    QString::fromUtf8(sensor.label),
                    sensor.rpm.value_or(-1.0),
                    sensor.percent.value_or(-1.0),
                    QString::fromUtf8(sensor.source),
                });
            }
        }
        emit sampleReady(
            QString::fromUtf8(backend->name()),
            QString::fromUtf8(data.osName),
            QString::fromUtf8(data.cpuName),
            QString::fromUtf8(data.gpuName),
            data.cpuUsagePercent.valueOr(-1.0),
            coreLoads,
            coreFrequenciesMhz,
            data.cpuFrequencyMhz.valueOr(-1.0),
            data.cpuTemperatureC.valueOr(-1.0),
            data.gpuUsagePercent.valueOr(-1.0),
            data.gpuTemperatureC.valueOr(-1.0),
            data.gpuMemoryTotalMb.valueOr(-1024.0) / 1024.0,
            data.gpuMemoryUsedMb.valueOr(-1024.0) / 1024.0,
            data.gpuMemoryPercent.valueOr(-1.0),
            data.ramUsagePercent.valueOr(-1.0),
            data.ramTotalBytes.usable()
                ? static_cast<double>(*data.ramTotalBytes.value)
                    / (1024.0 * 1024.0 * 1024.0)
                : -1.0,
            runtime,
            disks,
            temperatures,
            fans,
            data.netDownloadBytesPerSecond.valueOr(-1.0),
            data.netUploadBytesPerSecond.valueOr(-1.0));

        for (int elapsed = 0;
             elapsed < 800 && !isInterruptionRequested() && !isPaused();
             elapsed += 50) {
            waitMutex_.lock();
            waitCondition_.wait(&waitMutex_, 50);
            waitMutex_.unlock();
        }
    }
}

} // namespace orion::app
