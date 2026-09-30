#include "autostart_worker.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    orion::app::AutostartWorker worker;
    QEventLoop loop;
    bool delivered = false;
    QString source;
    QVector<orion::app::AutostartTelemetry> entries;
    QObject::connect(&worker, &orion::app::AutostartWorker::entriesReady,
        [&](const auto& result, const QString& collectorSource) {
            delivered = true;
            entries = result;
            source = collectorSource;
            loop.quit();
        });
    worker.scan();
    QTimer::singleShot(8000, &loop, &QEventLoop::quit);
    loop.exec();
    if (worker.isRunning()) {
        worker.stop();
        worker.wait(1000);
    }
    if (!delivered || source.isEmpty()) {
        std::cerr << "Autostart worker did not deliver a native snapshot.\n";
        return EXIT_FAILURE;
    }
    for (const auto& entry : entries) {
        if (entry.name.isEmpty() || entry.source.isEmpty()
            || (entry.category != QStringLiteral("user")
                && entry.category != QStringLiteral("system"))
            || entry.enabled < -1 || entry.enabled > 1) {
            std::cerr << "An autostart item violates the UI transfer contract.\n";
            return EXIT_FAILURE;
        }
    }
    std::cout << "Autostart worker contract passed with " << entries.size()
              << " entries.\n";
    return EXIT_SUCCESS;
}
