#pragma once

#include <QJsonObject>
#include <QThread>
#include <atomic>
#include <functional>

namespace orion::core { struct TelemetryData; }

namespace orion::app
{

enum class DeepScanStep
{
    Hardware,
    Smart,
    LogsBefore,
    Autostart,
    MemoryBefore,
    Stress,
    MemoryAfter,
    LogsAfter,
    SensorsAfter
};

class DeepScanWorker final : public QThread
{
    Q_OBJECT
  public:
    using Cancel = std::function<bool()>;
    using Progress = std::function<void(int, const QString&)>;
    using StepFunction =
        std::function<QJsonObject(DeepScanStep, const QJsonObject&, const Cancel&, const Progress&)>;
    explicit DeepScanWorker(QObject* parent = nullptr);
    // Test fixtures replace the entire execution boundary; missing hooks never
    // fall back to live load.
    explicit DeepScanWorker(StepFunction execute, QObject* parent = nullptr);
    bool startScan(const QJsonObject& snapshot, const QJsonObject& hardwareSeed, bool confirmed);
    void requestStop();
    static QString reportText(const QJsonObject& report);
    // Pure conversion boundary: permits quality/identity regression checks without live collection.
    static QJsonObject sensorSnapshot(const orion::core::TelemetryData& data);
  signals:
    void progressChanged(int percent, const QString& status);
    void logLine(const QString& line);
    void reportReady(const QJsonObject& report);

  protected:
    void run() override;

  private:
    static QJsonObject executeNative(DeepScanStep step, const QJsonObject& seed, const Cancel& cancelled,
                                     const Progress& progress);
    StepFunction execute_;
    QJsonObject snapshot_;
    QJsonObject hardwareSeed_;
    std::atomic_bool cancelled_{false};
};

} // namespace orion::app
