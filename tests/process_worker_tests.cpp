#include "process_worker.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    orion::app::ProcessWorker worker;
    int samples = 0;
    int samplesWhileInactive = -1;
    int samplesBeforePause = -1;
    int samplesAfterPause = -1;
    int lastProcessCount = 0;

    QObject::connect(
        &worker,
        &orion::app::ProcessWorker::processesReady,
        [&](const QVector<orion::app::ProcessTelemetry>& processes,
            const int,
            const QString&) {
            ++samples;
            lastProcessCount = processes.size();
        });

    QEventLoop loop;
    worker.start();
    QTimer::singleShot(500, &application, [&] {
        samplesWhileInactive = samples;
        worker.setActive(true);
    });
    QTimer::singleShot(2300, &application, [&] {
        samplesBeforePause = samples;
        worker.setActive(false);
    });
    QTimer::singleShot(3100, &application, [&] {
        samplesAfterPause = samples;
        worker.setActive(true);
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    worker.stop();
    worker.wait(3000);

    if (samplesWhileInactive != 0) {
        std::cerr << "Process collection ran while its tab was inactive.\n";
        return EXIT_FAILURE;
    }
    if (samplesBeforePause < 1 || lastProcessCount < 1) {
        std::cerr << "No native process inventory arrived while active.\n";
        return EXIT_FAILURE;
    }
    if (samplesAfterPause != samplesBeforePause) {
        std::cerr << "Process collection emitted while paused/inactive.\n";
        return EXIT_FAILURE;
    }
    if (samples <= samplesAfterPause) {
        std::cerr << "Process collection did not resume.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Process worker lazy/pause lifecycle passed with "
              << samples << " samples and " << lastProcessCount << " processes.\n";
    return EXIT_SUCCESS;
}
