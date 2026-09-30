#pragma once

#include <QThread>

#include <atomic>

namespace orion::app {

class ServerStatusChecker final : public QThread {
    Q_OBJECT

public:
    ServerStatusChecker(
        qint64 profileId,
        QString host,
        quint16 port,
        int timeoutMs = 2000,
        QObject* parent = nullptr);

    void requestStop() noexcept;

signals:
    void resultReady(qint64 profileId, bool online, double latencyMs, const QString& message);

protected:
    void run() override;

private:
    qint64 profileId_ {-1};
    QString host_;
    quint16 port_ {0};
    int timeoutMs_ {2000};
    std::atomic_bool stopRequested_ {false};
};

} // namespace orion::app
