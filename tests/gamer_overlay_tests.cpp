#include "overlay_fps_counter.h"
#include "widgets/gamer_overlay.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QLabel>
#include <QPixmap>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

[[nodiscard]] bool hasText(
    const orion::app::GamerOverlay& overlay,
    const QString& objectName,
    const QString& fragment)
{
    const auto* label = overlay.findChild<QLabel*>(objectName);
    return label != nullptr && label->text().contains(fragment);
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    orion::app::UiFpsCounter counter(6);
    for (int index = 0; index < 8; ++index) {
        QThread::msleep(10);
        (void)counter.tick();
    }
    if (!std::isfinite(counter.fps()) || counter.fps() < 20.0 || counter.fps() > 500.0) {
        std::cerr << "UI FPS counter did not report a plausible rolling rate.\n";
        return EXIT_FAILURE;
    }

    orion::app::GamerOverlay overlay(QStringLiteral("127.0.0.1"));
    if (overlay.objectName() != QStringLiteral("GamerOverlay")
        || overlay.size() != QSize(280, 250)
        || std::abs(overlay.windowOpacity() - 0.88) > 0.01
        || !overlay.windowFlags().testFlag(Qt::Tool)
        || !overlay.windowFlags().testFlag(Qt::FramelessWindowHint)
        || !overlay.windowFlags().testFlag(Qt::WindowStaysOnTopHint)) {
        std::cerr << "Gamer overlay window contract is incomplete.\n";
        return EXIT_FAILURE;
    }

    overlay.applyTheme(
        QStringLiteral("#0B1622"), QStringLiteral("#E4F6FF"),
        QStringLiteral("#00E5FF"), QStringLiteral("#FF9F40"),
        QStringLiteral("#122A3A"), QStringLiteral("'Consolas', monospace"));
    overlay.updateTelemetry(42.0, 57.0, 68.0, 5.5 * 1024.0 * 1024.0,
        1.25 * 1024.0 * 1024.0);
    if (!hasText(overlay, QStringLiteral("GamerCpu"), QStringLiteral("42%"))
        || !hasText(overlay, QStringLiteral("GamerGpu"), QStringLiteral("57%"))
        || !hasText(overlay, QStringLiteral("GamerRam"), QStringLiteral("68%"))
        || !hasText(overlay, QStringLiteral("GamerNetwork"), QStringLiteral("↓5.5"))
        || !hasText(overlay, QStringLiteral("GamerNetwork"), QStringLiteral("↑1.3"))) {
        std::cerr << "Gamer overlay did not render the supplied telemetry.\n";
        return EXIT_FAILURE;
    }

    overlay.setCriticalMetrics(true, false, true);
    const auto* cpu = overlay.findChild<QLabel*>(QStringLiteral("GamerCpu"));
    const auto* gpu = overlay.findChild<QLabel*>(QStringLiteral("GamerGpu"));
    if (cpu == nullptr || gpu == nullptr
        || !cpu->styleSheet().contains(QStringLiteral("#FF3B30"))
        || gpu->styleSheet().contains(QStringLiteral("#FF3B30"))) {
        std::cerr << "Per-metric critical highlighting is incomplete.\n";
        return EXIT_FAILURE;
    }

    overlay.setAlarmState(true);
    if (!overlay.alarmActive()) {
        std::cerr << "Alarm pulse state was not retained.\n";
        return EXIT_FAILURE;
    }
    overlay.setMonitoringPaused(true);
    if (!overlay.monitoringPaused()
        || !hasText(overlay, QStringLiteral("GamerFps"), QStringLiteral("пауза"))
        || !hasText(overlay, QStringLiteral("GamerPing"), QStringLiteral("пауза"))) {
        std::cerr << "Global monitoring pause did not reach the Gamer overlay.\n";
        return EXIT_FAILURE;
    }
    overlay.setMonitoringPaused(false);

    bool closedByUser = false;
    QObject::connect(&overlay, &orion::app::GamerOverlay::closedByUser,
        [&closedByUser] { closedByUser = true; });
    overlay.show();
    QEventLoop renderLoop;
    QTimer::singleShot(650, &renderLoop, &QEventLoop::quit);
    renderLoop.exec();
    if (!overlay.isVisible() || !overlay.pingWorkerRunning()
        || !hasText(overlay, QStringLiteral("GamerFps"), QStringLiteral("(UI)"))) {
        std::cerr << "Visible Gamer overlay runtime did not start.\n";
        return EXIT_FAILURE;
    }

    const QStringList arguments = QCoreApplication::arguments();
    const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        if (!overlay.grab().save(arguments.at(screenshotIndex + 1))) {
            std::cerr << "Could not save Gamer overlay screenshot.\n";
            return EXIT_FAILURE;
        }
    }

    overlay.close();
    QCoreApplication::processEvents();
    if (!closedByUser || overlay.pingWorkerRunning()) {
        std::cerr << "Gamer overlay did not stop its background ping on close.\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
