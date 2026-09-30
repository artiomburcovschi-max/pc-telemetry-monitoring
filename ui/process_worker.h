#pragma once

#include "telemetry_types.h"

#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include <atomic>

namespace orion::app {

class ProcessWorker final : public QThread {
    Q_OBJECT

public:
    explicit ProcessWorker(QObject* parent = nullptr);
    void setActive(bool active);
    [[nodiscard]] bool isActive() const noexcept;
    void refresh();
    void stop();

signals:
    void processesReady(
        const QVector<ProcessTelemetry>& processes,
        int logicalProcessorCount,
        const QString& source);

protected:
    void run() override;

private:
    std::atomic_bool active_ {false};
    std::atomic_bool refreshRequested_ {false};
    QMutex waitMutex_;
    QWaitCondition waitCondition_;
};

} // namespace orion::app
