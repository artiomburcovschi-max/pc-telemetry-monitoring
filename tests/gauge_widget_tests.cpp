#include "widgets/gauge_widget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QPixmap>
#include <QTimer>

#include <cmath>
#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    orion::app::GaugeWidget gauge(QStringLiteral("CPU"));
    gauge.resize(220, 220);
    gauge.setStyleSheet(QStringLiteral("background: #0B1622; color: #E4F6FF;"));
    gauge.setColors(QColor(QStringLiteral("#00E5FF")),
        QColor(QStringLiteral("#122A3A")), QColor(QStringLiteral("#E4F6FF")));

    if (gauge.objectName() != QStringLiteral("MetricGauge")
        || gauge.available() || gauge.value() != 0.0) {
        std::cerr << "Gauge initial contract is incomplete.\n";
        return EXIT_FAILURE;
    }

    gauge.show();
    gauge.setTargetValue(73.0);
    QEventLoop animationLoop;
    QTimer::singleShot(480, &animationLoop, &QEventLoop::quit);
    animationLoop.exec();
    if (!gauge.available() || std::abs(gauge.value() - 73.0) > 0.5
        || std::abs(gauge.targetValue() - 73.0) > 0.01) {
        std::cerr << "Gauge did not complete its bounded value animation.\n";
        return EXIT_FAILURE;
    }

    const QStringList arguments = QCoreApplication::arguments();
    const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        if (!gauge.grab().save(arguments.at(screenshotIndex + 1))) {
            std::cerr << "Could not save the gauge screenshot.\n";
            return EXIT_FAILURE;
        }
    }

    gauge.setUnavailable();
    if (gauge.available() || gauge.targetValue() != 0.0 || gauge.value() != 0.0) {
        std::cerr << "Gauge unavailable state is dishonest.\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
