#pragma once

#include <QJsonObject>
#include <QThread>

#include <atomic>
#include <functional>

namespace orion::app
{

enum class FullScanStep
{
    DeepLocal,
    Antivirus,
    SpeedTest,
    PublicIp
};

class FullScanWorker final : public QThread
{
    Q_OBJECT
  public:
    using Cancel = std::function<bool()>;
    using Progress = std::function<void(int, const QString&)>;
    using StepFunction =
        std::function<QJsonObject(FullScanStep, const QJsonObject&, const Cancel&, const Progress&)>;

    explicit FullScanWorker(QObject* parent = nullptr);
    // Test fixtures replace every expensive/network boundary. Missing hooks do
    // not fall back to live activity.
    explicit FullScanWorker(StepFunction execute, QObject* parent = nullptr);

    bool startScan(const QJsonObject& snapshot, const QJsonObject& hardwareSeed, bool confirmed);
    void requestStop();

    [[nodiscard]] static QJsonObject antivirusInfoFromJson(const QByteArray& payload);
    [[nodiscard]] static QJsonObject rateInternet(const QJsonObject& speedResult);
    [[nodiscard]] static QString reportText(const QJsonObject& report);

  signals:
    void progressChanged(int percent, const QString& status);
    void logLine(const QString& line);
    void reportReady(const QJsonObject& report);

  protected:
    void run() override;

  private:
    static QJsonObject executeNative(FullScanStep step, const QJsonObject& seed,
                                     const Cancel& cancelled, const Progress& progress);
    static QJsonObject collectAntivirusInfo(const Cancel& cancelled);

    StepFunction execute_;
    QJsonObject snapshot_;
    QJsonObject hardwareSeed_;
    std::atomic_bool cancelled_{false};
};

} // namespace orion::app

