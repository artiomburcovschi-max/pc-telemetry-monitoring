#include "main_window.h"
#include "startup_dialog.h"

#include "orion/storage/app_paths.h"
#include "orion/storage/app_settings.h"
#include "orion/storage/logging.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication::setOrganizationName(QStringLiteral("ORION"));
    QApplication::setApplicationName(QStringLiteral("ORION"));
    QApplication application(argc, argv);

    orion::storage::AppPaths paths;
    QString startupError;
    if (!paths.ensureCreated(&startupError))
    {
        qWarning().noquote() << startupError;
    }
    if (!paths.migrateLegacyFile(
            QCoreApplication::applicationDirPath(), QStringLiteral("user_settings.json"), &startupError))
    {
        qWarning().noquote() << startupError;
    }
    if (!orion::storage::initializeFileLogging(paths.logFile(), &startupError))
    {
        qWarning().noquote() << startupError;
    }
    orion::storage::installCrashHandler();

    orion::storage::AppSettings settings;
    if (!orion::storage::AppSettings::load(paths.settingsFile(), settings, &startupError))
    {
        qWarning().noquote() << QStringLiteral("Настройки не загружены: %1").arg(startupError);
    }
    qInfo().noquote()
        << QStringLiteral("O.R.I.O.N. Native запущен; каталог данных: %1").arg(paths.dataRoot());

    const auto arguments = QCoreApplication::arguments();
    const bool allowStartupOnlineLookup = !arguments.contains(QStringLiteral("--smoke-test"))
        && !arguments.contains(QStringLiteral("--screenshot"))
        && !arguments.contains(QStringLiteral("--screenshot-startup"));
    int result = 0;
    {
        orion::app::StartupDialog splash;
        bool aborted = false;
        QObject::connect(&splash, &orion::app::StartupDialog::abortRequested, &application,
            [&]
            {
                aborted = true;
                application.quit();
            });
        splash.setProgress(5, QStringLiteral("Настройки загружены. Создание интерфейса…"));
        splash.appendLog(QStringLiteral("Настройки загружены. Создание нативного интерфейса."));
        splash.setDetailsVisible(arguments.contains(QStringLiteral("--startup-details")));
        splash.show();
        application.processEvents(QEventLoop::ExcludeUserInputEvents);
        if (!aborted)
        {
            // Startup owns the IP lookup; do not also schedule the legacy delayed call.
            orion::app::MainWindow window(settings, paths.settingsFile(), false);
            bool opened = false;
            bool captured = false;
            const int captureArgument = arguments.indexOf(QStringLiteral("--screenshot-startup"));
            const QString capturePath = captureArgument >= 0 && captureArgument + 1 < arguments.size()
                ? arguments.at(captureArgument + 1)
                : QString();
            const auto captureStartup = [&]
            {
                if (captured || capturePath.isEmpty())
                    return;
                captured = true;
                if (!splash.grab().save(capturePath))
                    application.exit(2);
            };
            const auto reveal = [&]
            {
                if (opened || aborted)
                    return;
                opened = true;
                captureStartup();
                window.show();
                splash.dismiss();
                qInfo() << "Startup: main window opened";
                const auto tabIndex = arguments.indexOf(QStringLiteral("--tab"));
                if (tabIndex >= 0 && tabIndex + 1 < arguments.size())
                {
                    bool valid = false;
                    const int requestedTab = arguments.at(tabIndex + 1).toInt(&valid);
                    if (valid)
                    {
                        if (auto* tabs = window.findChild<QTabWidget*>();
                            tabs != nullptr && requestedTab >= 0 && requestedTab < tabs->count())
                        {
                            tabs->setCurrentIndex(requestedTab);
                        }
                    }
                }
                const auto screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
                if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size())
                {
                    const auto screenshotPath = arguments.at(screenshotIndex + 1);
                    const int scrollArgument = arguments.indexOf(QStringLiteral("--screenshot-scroll"));
                    const int scrollPercent = scrollArgument >= 0 && scrollArgument + 1 < arguments.size()
                        ? qBound(0, arguments.at(scrollArgument + 1).toInt(), 100)
                        : 0;
                    const int delayArgument = arguments.indexOf(QStringLiteral("--screenshot-delay"));
                    const int minimumTicks = delayArgument >= 0 && delayArgument + 1 < arguments.size()
                        ? qMax(18, qBound(0, arguments.at(delayArgument + 1).toInt(), 60) * 10)
                        : 18;
                    auto* screenshotTimer = new QTimer(&application);
                    screenshotTimer->setInterval(100);
                    QObject::connect(screenshotTimer, &QTimer::timeout, &application,
                        [&application, &window, screenshotPath, screenshotTimer, scrollPercent, minimumTicks,
                            ticks = 0]() mutable
                        {
                            if (++ticks < minimumTicks)
                                return;
                            auto* tabs = window.findChild<QTabWidget*>();
                            auto* copy = window.findChild<QPushButton*>(QStringLiteral("HardwareCopyButton"));
                            if (ticks < 200 && tabs != nullptr && tabs->currentWidget() != nullptr
                                && tabs->currentWidget()->objectName() == QStringLiteral("HardwareScroll")
                                && copy != nullptr && !copy->isEnabled())
                                return;
                            screenshotTimer->stop();
                            if (scrollPercent > 0 && tabs != nullptr)
                            {
                                auto* scroll = qobject_cast<QScrollArea*>(tabs->currentWidget());
                                if (scroll == nullptr && tabs->currentWidget() != nullptr)
                                    scroll = tabs->currentWidget()->findChild<QScrollArea*>(
                                        QStringLiteral("OverviewScroll"));
                                if (scroll == nullptr && tabs->currentWidget() != nullptr)
                                    scroll = tabs->currentWidget()->findChild<QScrollArea*>(
                                        QStringLiteral("NetworkTrafficScroll"));
                                if (scroll == nullptr && tabs->currentWidget() != nullptr)
                                    scroll = tabs->currentWidget()->findChild<QScrollArea*>(
                                        QStringLiteral("DiagnosticsPanelScroll_diagnostics"));
                                if (scroll != nullptr)
                                {
                                    auto* bar = scroll->verticalScrollBar();
                                    bar->setValue(
                                        qRound(static_cast<double>(bar->maximum()) * scrollPercent / 100.0));
                                }
                            }
                            application.exit(window.grab().save(screenshotPath) ? 0 : 2);
                        });
                    screenshotTimer->start();
                }
                else if (arguments.contains(QStringLiteral("--smoke-test"))
                    || arguments.contains(QStringLiteral("--screenshot-startup")))
                {
                    QTimer::singleShot(1800, &application, &QCoreApplication::quit);
                }
            };
            QObject::connect(&splash, &orion::app::StartupDialog::skipRequested, &window,
                [&]
                {
                    splash.appendLog(QStringLiteral("Окно открыто; стартовый сбор продолжается в фоне."));
                    qInfo() << "Startup: skipped waiting; collection continues";
                    reveal();
                });
            QObject::connect(&splash, &orion::app::StartupDialog::abortRequested, &window,
                [&]
                {
                    window.cancelStartupScan();
                    // Closing startup is an explicit exit, including when tray mode is on.
                    qInfo() << "Startup: abort requested";
                    application.quit();
                });
            QObject::connect(&window, &orion::app::MainWindow::startupProgress, &splash,
                [&](int percent, const QString& stage)
                {
                    splash.setProgress(10 + qRound(percent * 0.90), stage);
                    if (!opened && percent >= 20)
                        captureStartup();
                });
            QObject::connect(&window, &orion::app::MainWindow::startupLog, &splash,
                [&](const QString& line)
                {
                    splash.appendLog(line);
                    qInfo().noquote() << "Startup:" << line;
                });
            QObject::connect(&window, &orion::app::MainWindow::startupFinished, &splash,
                [&](bool warnings)
                {
                    splash.setProgress(100,
                        warnings ? QStringLiteral("Готово с предупреждениями") : QStringLiteral("Готово"));
                    reveal();
                });
            splash.setProgress(10, QStringLiteral("Интерфейс готов. Подготовка данных…"));
            splash.appendLog(QStringLiteral("Интерфейс создан; фоновые потоки телеметрии запущены."));
            splash.startRevealTimeout();
            QTimer::singleShot(0, &window,
                [&]
                {
                    if (!aborted)
                        window.beginStartupScan(allowStartupOnlineLookup);
                });
            if (arguments.contains(QStringLiteral("--no-splash")))
                QTimer::singleShot(0, &splash, &orion::app::StartupDialog::requestSkip);
            result = application.exec();
            window.cancelStartupScan();
        }
    }
    // Keep logging alive until the window has stopped/retained its workers.
    qInfo() << "O.R.I.O.N. Native stopped";
    orion::storage::shutdownFileLogging();
    return result;
}
