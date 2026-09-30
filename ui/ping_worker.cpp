#include "ping_worker.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QTcpSocket>

#include <algorithm>
#include <utility>

namespace orion::app {

PingTelemetry probeTcpEndpoint(
    const QString& suppliedTarget,
    const quint16 port,
    const int timeoutMs)
{
    const QString target = suppliedTarget.trimmed();
    PingTelemetry result;
    QElapsedTimer sourceClock;
    sourceClock.start();
    result.observedMonotonicMs = sourceClock.msecsSinceReference();
    result.target = target;
    result.port = port;
    result.source = QStringLiteral("tcp_connect:%1").arg(port);
    result.observedAt = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    if (target.isEmpty()) {
        result.reason = QStringLiteral("цель ping не задана");
        return result;
    }

    QTcpSocket socket;
    QElapsedTimer timer;
    timer.start();
    socket.connectToHost(target, port);
    if (!socket.waitForConnected(std::clamp(timeoutMs, 50, 30'000))) {
        result.reason = socket.errorString().trimmed();
        if (result.reason.isEmpty()) {
            result.reason = QStringLiteral("TCP-подключение недоступно");
        }
        socket.abort();
        return result;
    }

    result.latencyMs = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
    result.quality = QStringLiteral("valid");
    socket.abort();
    return result;
}

PingWorker::PingWorker(
    QString target,
    const quint16 port,
    const int timeoutMs,
    const int intervalMs,
    QObject* parent)
    : QThread(parent)
    , target_(std::move(target).trimmed())
    , port_(port)
    , timeoutMs_(std::clamp(timeoutMs, 50, 30'000))
    , intervalMs_(std::clamp(intervalMs, 100, 60'000))
{
    if (target_.isEmpty()) {
        target_ = QStringLiteral("8.8.8.8");
    }
    qRegisterMetaType<PingTelemetry>();
}

void PingWorker::setTarget(const QString& suppliedTarget)
{
    QString target = suppliedTarget.trimmed();
    if (target.isEmpty()) {
        target = QStringLiteral("8.8.8.8");
    }
    {
        const QMutexLocker locker(&mutex_);
        if (target_ == target) {
            return;
        }
        target_ = target;
        ++targetGeneration_;
    }
    waitCondition_.wakeAll();
}

void PingWorker::setPaused(const bool paused)
{
    paused_.store(paused, std::memory_order_release);
    waitCondition_.wakeAll();
}

bool PingWorker::isPaused() const noexcept
{
    return paused_.load(std::memory_order_acquire);
}

void PingWorker::stop()
{
    requestInterruption();
    waitCondition_.wakeAll();
}

void PingWorker::run()
{
    while (!isInterruptionRequested()) {
        QString target;
        quint64 generation = 0;
        {
            QMutexLocker locker(&mutex_);
            while (isPaused() && !isInterruptionRequested()) {
                waitCondition_.wait(&mutex_, 250);
            }
            if (isInterruptionRequested()) {
                break;
            }
            target = target_;
            generation = targetGeneration_;
        }

        const auto sample = probeTcpEndpoint(target, port_, timeoutMs_);
        {
            const QMutexLocker locker(&mutex_);
            if (generation != targetGeneration_ || isPaused() || isInterruptionRequested()) {
                continue;
            }
        }
        emit sampleReady(sample);

        QMutexLocker locker(&mutex_);
        if (!isPaused() && !isInterruptionRequested()
            && generation == targetGeneration_) {
            waitCondition_.wait(&mutex_, intervalMs_);
        }
    }
}

} // namespace orion::app
