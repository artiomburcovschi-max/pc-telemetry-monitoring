#include "stress_worker.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cerr << "Could not create stress-test directory.\n";
        return EXIT_FAILURE;
    }

    orion::app::StressWorker worker;
    orion::app::StressOptions options;
    options.runCpu = false;
    options.runGpu = false;
    options.runDisk = true;
    options.diskSizeMiB = 1;
    options.diskDirectory = directory.path();

    QJsonObject result;
    QEventLoop loop;
    QObject::connect(&worker, &orion::app::StressWorker::testCompleted,
        &loop, [&](const QJsonObject& report) {
            result = report;
            loop.quit();
        });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        worker.requestStop();
        loop.quit();
    });
    timeout.start(10000);
    worker.startTest(options);
    loop.exec();
    worker.wait(5000);

    const bool passed = !result.isEmpty()
        && result.value(QStringLiteral("run_disk")).toBool()
        && result.value(QStringLiteral("disk_data_verified")).toBool()
        && result.value(QStringLiteral("disk_fsync_performed")).toBool()
        && result.value(QStringLiteral("disk_actual_mb")).toDouble() == 1.0
        && QDir(directory.path()).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty();
    if (!passed) {
        std::cerr << "Native disk stress contract failed or left temporary data behind.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Native disk stress contract passed.\n";
    return EXIT_SUCCESS;
}
