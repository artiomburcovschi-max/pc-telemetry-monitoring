#pragma once

#include <QMetaType>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <atomic>

namespace orion::app {

struct PingTelemetry {
    QString target;
    quint16 port {53};
    double latencyMs {-1.0};
    QString quality {QStringLiteral("unavailable")};
    QString source {QStringLiteral("tcp_connect")};
    QString reason;
    QString observedAt;
    qint64 observedMonotonicMs {-1};

    [[nodiscard]] bool usable() const noexcept
    {
        return quality == QStringLiteral("valid") && latencyMs >= 0.0;
    }
};

[[nodiscard]] PingTelemetry probeTcpEndpoint(
    const QString& target,
    quint16 port = 53,
    int timeoutMs = 1000);

class PingWorker final : public QThread {
    Q_OBJECT

public:
    explicit PingWorker(
        QString target,
        quint16 port = 53,
        int timeoutMs = 1000,
        int intervalMs = 2000,
        QObject* parent = nullptr);

    void setTarget(const QString& target);
    void setPaused(bool paused);
    [[nodiscard]] bool isPaused() const noexcept;
    void stop();

signals:
    void sampleReady(const PingTelemetry& sample);

protected:
    void run() override;

private:
    QString target_;
    quint16 port_ {53};
    int timeoutMs_ {1000};
    int intervalMs_ {2000};
    quint64 targetGeneration_ {0};
    std::atomic_bool paused_ {false};
    QMutex mutex_;
    QWaitCondition waitCondition_;
};

} // namespace orion::app

Q_DECLARE_METATYPE(orion::app::PingTelemetry)
