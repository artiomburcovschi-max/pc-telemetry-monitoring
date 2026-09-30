#include "ping_worker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstdlib>
#include <functional>
#include <iostream>

namespace {

[[nodiscard]] bool waitUntil(
    const std::function<bool()>& predicate,
    const int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return predicate();
}

void drainConnections(QTcpServer& server)
{
    while (server.hasPendingConnections()) {
        auto* socket = server.nextPendingConnection();
        socket->disconnectFromHost();
        socket->deleteLater();
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        std::cerr << "Could not create the local TCP ping fixture.\n";
        return EXIT_FAILURE;
    }

    const auto direct = orion::app::probeTcpEndpoint(
        QStringLiteral("127.0.0.1"), server.serverPort(), 500);
    if (!direct.usable() || direct.latencyMs < 0.0 || direct.observedMonotonicMs < 0
        || direct.quality != QStringLiteral("valid")
        || direct.source != QStringLiteral("tcp_connect:%1").arg(server.serverPort())) {
        std::cerr << "A reachable TCP endpoint did not produce valid ping telemetry.\n";
        return EXIT_FAILURE;
    }
    drainConnections(server);

    const auto unavailable = orion::app::probeTcpEndpoint({}, server.serverPort(), 100);
    if (unavailable.usable() || unavailable.latencyMs >= 0.0
        || unavailable.quality != QStringLiteral("unavailable")
        || unavailable.reason.isEmpty()) {
        std::cerr << "An unavailable ping target invented a measurement or lost its reason.\n";
        return EXIT_FAILURE;
    }

    orion::app::PingWorker worker(
        QStringLiteral("127.0.0.1"), server.serverPort(), 500, 150);
    int samples = 0;
    orion::app::PingTelemetry latest;
    QObject::connect(&worker, &orion::app::PingWorker::sampleReady,
        &application, [&](const orion::app::PingTelemetry& sample) {
            latest = sample;
            ++samples;
            drainConnections(server);
        });
    worker.start();
    if (!waitUntil([&] { return samples >= 1; }, 1500) || !latest.usable()) {
        worker.stop();
        worker.wait(1500);
        std::cerr << "The background ping worker did not publish its first valid sample.\n";
        return EXIT_FAILURE;
    }

    worker.setPaused(true);
    QThread::msleep(100);
    QCoreApplication::processEvents();
    const int samplesBeforePause = samples;
    QThread::msleep(450);
    QCoreApplication::processEvents();
    if (samples != samplesBeforePause) {
        worker.stop();
        worker.wait(1500);
        std::cerr << "The background ping worker emitted while paused.\n";
        return EXIT_FAILURE;
    }

    worker.setPaused(false);
    if (!waitUntil([&] { return samples > samplesBeforePause; }, 1500)) {
        worker.stop();
        worker.wait(1500);
        std::cerr << "The background ping worker did not resume.\n";
        return EXIT_FAILURE;
    }
    worker.stop();
    if (!worker.wait(1500)) {
        std::cerr << "The background ping worker did not stop within its bounded timeout.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Native TCP ping quality and pause/resume tests passed.\n";
    return EXIT_SUCCESS;
}
