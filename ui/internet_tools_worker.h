#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QMutex>
#include <QThread>
#include <QVector>

#include <atomic>
#include <functional>

namespace orion::app {

enum class InternetOperation {
    PublicIpLookup,
    SpeedTest,
};

class InternetToolsWorker final : public QThread {
    Q_OBJECT

public:
    using ProgressFunction = std::function<void(int, const QString&)>;
    using OperationFunction = std::function<QJsonObject(
        InternetOperation operation,
        const std::atomic_bool& stopRequested,
        const ProgressFunction& progress)>;

    explicit InternetToolsWorker(
        QObject* parent = nullptr,
        OperationFunction operation = {});
    ~InternetToolsWorker() override;

    bool startPublicIpLookup();
    bool startSpeedTest();
    void requestStop();

    [[nodiscard]] static QJsonObject parseIpInfoResponse(
        const QByteArray& payload,
        const QString& checkedAt);
    [[nodiscard]] static QJsonObject parseIpApiResponse(
        const QByteArray& payload,
        const QString& checkedAt);
    [[nodiscard]] static QJsonObject speedResultFromMeasurements(
        qint64 downloadBytes,
        qint64 downloadElapsedMs,
        qint64 uploadBytes,
        qint64 uploadElapsedMs,
        const QVector<double>& pingSamplesMs,
        const QString& testedAt);

signals:
    void progressChanged(orion::app::InternetOperation operation,
        int percent, const QString& status);
    void resultReady(orion::app::InternetOperation operation,
        const QJsonObject& result);

protected:
    void run() override;

private:
    bool startOperation(InternetOperation operation);

    mutable QMutex mutex_;
    InternetOperation pendingOperation_ {InternetOperation::PublicIpLookup};
    OperationFunction operation_;
    std::atomic_bool stopRequested_ {false};
};

} // namespace orion::app

Q_DECLARE_METATYPE(orion::app::InternetOperation)
