#include "telemetry_worker.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    orion::app::TelemetryWorker worker;
    int samples = 0;
    int samplesBeforePause = -1;
    int samplesAfterPause = -1;
    bool networkTransferred = false;
    bool provenanceTransferred = false;
    bool firstIntervalEmpty = true;
    bool resumedIntervalEmpty = true;

    QObject::connect(
        &worker,
        &orion::app::TelemetryWorker::sampleReady,
        &application,
        [&](
            const QString&,
            const QString&,
            const QString&,
            const QString&,
            double,
            const QVector<double>&,
            const QVector<double>&,
            double,
            double,
            double,
            double,
            double,
            double,
            double,
            double,
            double,
            const orion::app::RuntimeTelemetry& runtime,
            const QVector<orion::app::DiskTelemetry>&,
            const QVector<orion::app::TemperatureTelemetry>&,
            const QVector<orion::app::FanTelemetry>&,
            double,
            double) {
            networkTransferred |= runtime.network.receivedBytes.usable();
            provenanceTransferred |= runtime.appMonitorSystemEnvelope.value("system_captured_monotonic_ms").isDouble()
                && !runtime.appMonitorSystemEnvelope.value("system_fields").toObject().isEmpty()
                && runtime.incidentSystemEnvelope.value("captured_monotonic_ms").isDouble()
                && runtime.incidentSystemEnvelope.value("fields").toObject().contains("cpu_temp_c");
            if (samples == 0) firstIntervalEmpty = !runtime.network.intervalErrors.hasValue()
                && !runtime.network.intervalDrops.hasValue();
            if (samplesAfterPause >= 0 && samples == samplesAfterPause)
                resumedIntervalEmpty = !runtime.network.intervalErrors.hasValue()
                    && !runtime.network.intervalDrops.hasValue();
            ++samples;
        });

    QEventLoop loop;
    worker.start();
    QTimer::singleShot(1400, &application, [&] { worker.setPaused(true); });
    QTimer::singleShot(1700, &application, [&] { samplesBeforePause = samples; });
    QTimer::singleShot(2900, &application, [&] {
        samplesAfterPause = samples;
        worker.setPaused(false);
    });
    QTimer::singleShot(4700, &loop, &QEventLoop::quit);
    loop.exec();
    worker.stop();
    worker.wait(3000);

    if (samplesBeforePause < 1) {
        std::cerr << "No telemetry sample arrived before pause.\n";
        return EXIT_FAILURE;
    }
    if (samplesAfterPause != samplesBeforePause) {
        std::cerr << "Telemetry emitted while globally paused.\n";
        return EXIT_FAILURE;
    }
    if (samples <= samplesAfterPause) {
        std::cerr << "Telemetry did not resume after pause.\n";
        return EXIT_FAILURE;
    }
    if (!networkTransferred || !provenanceTransferred || !firstIntervalEmpty || !resumedIntervalEmpty) {
        std::cerr << "Network counters were lost or a first/resumed interval was fabricated.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Telemetry pause/resume lifecycle test passed with "
              << samples << " samples.\n";
    return EXIT_SUCCESS;
}
