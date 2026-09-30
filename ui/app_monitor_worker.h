#pragma once

#include <QJsonObject>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include "orion/core/observation_clock.h"

namespace orion::app {

struct AppMonitorOptions {
    QString executablePath;
    int durationSeconds {3600};
    bool closeOnTimeout {true};
};

class AppMonitorWorker final : public QThread {
    Q_OBJECT

public:
    explicit AppMonitorWorker(QObject* parent = nullptr);
    void monitor(const AppMonitorOptions& options);
    void updateSystemSample(const QJsonObject& sample);
    void setPaused(bool paused);
    void stopObservation();

signals:
    void phaseChanged(const QString& phase);
    void progressChanged(int percent);
    void sampleReady(const QJsonObject& sample);
    void reportReady(const QJsonObject& report);

protected:
    void run() override;

private:
    [[nodiscard]] QJsonObject systemSample(qint64 notBeforeMs = -1, int waitMs = 0);
    [[nodiscard]] orion::core::ObservationClock::Snapshot observationState();
    QMutex clockMutex_;
    orion::core::ObservationClock observationClock_;
    QMutex mutex_;
    QMutex waitMutex_;
    QWaitCondition waitCondition_;
    QWaitCondition systemCondition_;
    AppMonitorOptions options_;
    QJsonObject systemSample_;
    std::atomic_bool stopRequested_ {false};
    std::atomic<qint64> systemNotBeforeMs_ {-1};
};

} // namespace orion::app
