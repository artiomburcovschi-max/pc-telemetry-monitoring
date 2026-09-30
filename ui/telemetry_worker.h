#pragma once

#include "telemetry_types.h"

#include <QMutex>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

#include <atomic>

namespace orion::app {

class TelemetryWorker final : public QThread {
    Q_OBJECT

public:
    explicit TelemetryWorker(QObject* parent = nullptr);
    void setPaused(bool paused);
    [[nodiscard]] bool isPaused() const noexcept;
    void stop();

signals:
    void sampleReady(
        const QString& backend,
        const QString& operatingSystem,
        const QString& cpuName,
        const QString& gpuName,
        double cpuPercent,
        const QVector<double>& cpuCores,
        const QVector<double>& cpuCoreFrequenciesMhz,
        double cpuFrequencyMhz,
        double cpuTemperatureC,
        double gpuPercent,
        double gpuTemperatureC,
        double gpuMemoryTotalGiB,
        double gpuMemoryUsedGiB,
        double gpuMemoryPercent,
        double ramPercent,
        double ramTotalGiB,
        const RuntimeTelemetry& runtime,
        const QVector<DiskTelemetry>& disks,
        const QVector<TemperatureTelemetry>& temperatures,
        const QVector<FanTelemetry>& fans,
        double downloadBytesPerSecond,
        double uploadBytesPerSecond);

protected:
    void run() override;

private:
    std::atomic_bool paused_ {false};
    std::atomic_uint64_t pauseGeneration_ {0};
    QMutex waitMutex_;
    QWaitCondition waitCondition_;
};

} // namespace orion::app
