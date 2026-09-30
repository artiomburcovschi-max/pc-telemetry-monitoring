#include "server_status_checker.h"

#include <QElapsedTimer>
#include <QTcpSocket>

#include <algorithm>
#include <utility>

namespace orion::app {

ServerStatusChecker::ServerStatusChecker(
    const qint64 profileId,
    QString host,
    const quint16 port,
    const int timeoutMs,
    QObject* parent)
    : QThread(parent)
    , profileId_(profileId)
    , host_(std::move(host))
    , port_(port)
    , timeoutMs_(std::clamp(timeoutMs, 100, 10000))
{
}

void ServerStatusChecker::requestStop() noexcept
{
    stopRequested_.store(true);
}

void ServerStatusChecker::run()
{
    if (stopRequested_.load()) return;

    QTcpSocket socket;
    QElapsedTimer timer;
    timer.start();
    socket.connectToHost(host_, port_);
    const bool connected = socket.waitForConnected(timeoutMs_);
    const double latencyMs = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
    if (stopRequested_.load()) {
        socket.abort();
        return;
    }
    if (connected) {
        socket.disconnectFromHost();
        emit resultReady(profileId_, true, latencyMs, QStringLiteral("В сети"));
        return;
    }

    const bool timedOut = socket.error() == QAbstractSocket::SocketTimeoutError
        || timer.elapsed() >= timeoutMs_;
    const QString message = timedOut
        ? QStringLiteral("Офлайн (таймаут)")
        : QStringLiteral("Офлайн (%1)").arg(socket.errorString());
    emit resultReady(profileId_, false, 0.0, message);
}

} // namespace orion::app
