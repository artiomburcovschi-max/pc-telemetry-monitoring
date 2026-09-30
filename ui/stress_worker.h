#pragma once

#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QThread>

#include <atomic>

namespace orion::app {

struct StressOptions {
    bool runCpu {true};
    bool runGpu {false};
    bool runDisk {false};
    int durationSeconds {30};
    int diskSizeMiB {200};
    QString diskDirectory;
};

class StressWorker final : public QThread {
    Q_OBJECT

public:
    explicit StressWorker(QObject* parent = nullptr);
    void startTest(const StressOptions& options);
    void requestStop();

signals:
    void progressChanged(int percent, const QString& status);
    void testCompleted(const QJsonObject& report);

protected:
    void run() override;

private:
    void runCpu(QJsonObject& report, int phase, int phases);
    void runGpu(QJsonObject& report, int phase, int phases);
    void runDisk(QJsonObject& report, int phase, int phases);
    void emitPhaseProgress(int phase, int phases, double fraction, const QString& status);

    QMutex mutex_;
    StressOptions options_;
    std::atomic_bool stopRequested_ {false};
    std::atomic_bool userStopRequested_ {false};
    QString safetyReason_;
};

} // namespace orion::app
