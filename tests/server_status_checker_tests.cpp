#include "server_status_checker.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTimer>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        std::cerr << "Could not create a local server-status fixture.\n";
        return EXIT_FAILURE;
    }

    orion::app::ServerStatusChecker checker(
        41, QStringLiteral("127.0.0.1"), server.serverPort(), 500);
    bool received = false;
    bool online = false;
    double latency = -1.0;
    qint64 profileId = -1;
    QEventLoop loop;
    QObject::connect(&checker, &orion::app::ServerStatusChecker::resultReady,
        &application, [&](const qint64 id, const bool isOnline,
                          const double latencyMs, const QString&) {
            received = true;
            online = isOnline;
            latency = latencyMs;
            profileId = id;
        });
    QObject::connect(&checker, &QThread::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(1500, &loop, &QEventLoop::quit);
    checker.start();
    loop.exec();
    if (!checker.wait(1000) || !received || !online || latency < 0.0 || profileId != 41) {
        std::cerr << "A reachable profile did not produce a real online result.\n";
        return EXIT_FAILURE;
    }

    orion::app::ServerStatusChecker cancelled(
        42, QStringLiteral("192.0.2.1"), 65000, 2000);
    bool cancelledEmitted = false;
    QObject::connect(&cancelled, &orion::app::ServerStatusChecker::resultReady,
        &application, [&](qint64, bool, double, const QString&) { cancelledEmitted = true; });
    cancelled.requestStop();
    cancelled.start();
    if (!cancelled.wait(1000) || cancelledEmitted) {
        std::cerr << "A cancelled profile check was not stopped cleanly.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Native server TCP status checks passed.\n";
    return EXIT_SUCCESS;
}
