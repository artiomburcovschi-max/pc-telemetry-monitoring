#include "main_window.h"
#include "diagnostic_worker.h"
#include "hardware_inventory_worker.h"
#include "telemetry_worker.h"
#include "deep_scan_worker.h"
#include "deep_scan_dialog.h"
#include "full_scan_worker.h"
#include "full_scan_dialog.h"
#include "widgets/details_panel.h"
#include "widgets/cpu_core_chart_widget.h"
#include "widgets/network_traffic_chart.h"
#include "widgets/metric_card.h"
#include "widgets/sparkline_widget.h"

#include "orion/storage/app_settings.h"
#include "orion/diagnostics/report_contract.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDialog>
#include <QEventLoop>
#include <QFrame>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QLineEdit>
#include <QLayout>
#include <QPushButton>
#include <QProgressBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QTabWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QScrollBar>
#include <QImage>
#include <QTimer>
#include <QToolButton>
#include <QSplitter>
#include <QMessageBox>

#include <cstdlib>
#include <iostream>

bool verifyTableParity(orion::app::MainWindow& window);
bool verifyAppMonitorUi(orion::app::MainWindow& window);
bool verifyFloatingCards(orion::app::MainWindow& window);
bool verifyDiagnosticLayout(orion::app::MainWindow& window, const QString& settingsPath);

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid())
    {
        std::cerr << "Could not create temporary settings directory.\n";
        return EXIT_FAILURE;
    }

    orion::storage::AppSettings settings;
    settings.trayIconEnabled = false;
    settings.pingTarget = QStringLiteral("127.0.0.2"); // Loopback even before the settings-edit test.
    orion::app::MainWindow window(
        settings, temporaryDirectory.filePath(QStringLiteral("user_settings.json")), false);

    auto* tabs = window.findChild<QTabWidget*>();
    const QStringList expectedTabs {
        QStringLiteral("Обзор"),
        QStringLiteral("Детали"),
        QStringLiteral("Сети"),
        QStringLiteral("Диспетчер задач"),
        QStringLiteral("Железо"),
        QStringLiteral("Автозагрузка"),
        QStringLiteral("Диагностика и тесты"),
        QStringLiteral("Проблемное приложение"),
    };
    if (tabs == nullptr || tabs->count() != expectedTabs.size())
    {
        std::cerr << "The native main-window tab contract is incomplete.\n";
        return EXIT_FAILURE;
    }
    for (qsizetype index = 0; index < expectedTabs.size(); ++index)
    {
        if (tabs->tabText(static_cast<int>(index)) != expectedTabs[index])
        {
            std::cerr << "Unexpected tab order at index " << index << ".\n";
            return EXIT_FAILURE;
        }
        if (tabs->tabToolTip(static_cast<int>(index)) != expectedTabs[index])
        {
            std::cerr << "Main navigation must retain full names in its tooltips.\n";
            return EXIT_FAILURE;
        }
    }

    QSet<QString> detailSections;
    for (auto* frame : window.findChildren<QFrame*>(QStringLiteral("MetricCard")))
    {
        if (frame->property("detailSection").isValid()
            && frame->property("detailSection").toString() != QStringLiteral("network"))
            detailSections.insert(frame->property("detailSection").toString());
    }
    const QSet<QString> expectedSections { QStringLiteral("cpu"), QStringLiteral("gpu"), QStringLiteral("os"),
        QStringLiteral("ram"), QStringLiteral("sensors"), QStringLiteral("storage_empty") };
    auto* detailsScroll = window.findChild<QScrollArea*>(QStringLiteral("DetailsScroll"));
    auto* overviewScroll = window.findChild<QScrollArea*>(QStringLiteral("OverviewScroll"));
    if (detailSections != expectedSections || !detailsScroll || !detailsScroll->widgetResizable()
        || !overviewScroll || !overviewScroll->widgetResizable() || tabs->elideMode() != Qt::ElideNone
        || !tabs->usesScrollButtons())
    {
        std::cerr << "Details must provide the original CPU/GPU/OS/RAM/disk composition.\n";
        return EXIT_FAILURE;
    }
    // A detached/resized card must wrap long measurements, not force the entire dock wider.
    orion::app::MetricCard narrowCard({}, true);
    narrowCard.setStyleSheet(window.styleSheet());
    narrowCard.setTextValue(QStringLiteral("↓ 1234.5 КиБ/с  ↑ 9876.5 КиБ/с"),
        QStringLiteral("Длинная подпись источника для проверки переноса"));
    narrowCard.resize(300, 210);
    narrowCard.show();
    narrowCard.layout()->activate();
    // Deliver layout requests only: telemetry must remain queued for the startup-gating checks below.
    for (int pass = 0; pass < 3; ++pass)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    }
    narrowCard.layout()->setGeometry(narrowCard.rect());
    auto* narrowValue = narrowCard.findChild<QLabel*>(QStringLiteral("MetricCardValue"));
    if (!narrowValue || !narrowValue->wordWrap() || narrowCard.width() > 300
        || narrowValue->geometry().right() > narrowCard.width()
        || narrowValue->height() < narrowValue->heightForWidth(narrowValue->width()))
    {
        std::cerr << "Long card values must wrap inside a narrow dock. Card: " << narrowCard.width() << " x "
                  << narrowCard.height() << ", value: " << (narrowValue ? narrowValue->width() : 0) << " x "
                  << (narrowValue ? narrowValue->height() : 0)
                  << ", hfw: " << (narrowValue ? narrowValue->heightForWidth(narrowValue->width()) : 0)
                  << ", hint: " << (narrowValue ? narrowValue->sizeHint().height() : 0)
                  << ", policy: " << (narrowValue ? narrowValue->hasHeightForWidth() : false)
                  << ", card hfw: " << narrowCard.layout()->totalHeightForWidth(300) << ".\n";
        return EXIT_FAILURE;
    }
    auto* narrowDetail = narrowCard.findChild<QLabel*>(QStringLiteral("MetricCardDetail"));
    if (narrowDetail->height() < narrowDetail->heightForWidth(narrowDetail->width()))
    {
        std::cerr << "Wrapped descriptions must not be clipped by the sparkline.\n";
        return EXIT_FAILURE;
    }
    narrowCard.hide();

    // Empty history is transparent: theme switching must not leave the old blue/black rectangle.
    orion::app::SparklineWidget emptyHistory;
    if (emptyHistory.minimumHeight() != 30 || emptyHistory.maximumHeight() != 40)
    {
        std::cerr << "Overview sparkline size must match the Python 30-40 pixel contract.\n";
        return EXIT_FAILURE;
    }
    emptyHistory.resize(240, 64);
    QImage historyImage(emptyHistory.size(), QImage::Format_ARGB32_Premultiplied);
    historyImage.fill(Qt::transparent);
    emptyHistory.render(&historyImage, QPoint(), QRegion(), QWidget::DrawChildren);
    if (historyImage.pixelColor(30, 15).alpha() != 0)
    {
        std::cerr << "An empty sparkline must preserve the surrounding theme surface.\n";
        return EXIT_FAILURE;
    }

    auto* hardwareCopy = window.findChild<QPushButton*>(QStringLiteral("HardwareCopyButton"));
    auto* hardwareRefresh = window.findChild<QPushButton*>(QStringLiteral("HardwareRefreshButton"));
    auto* hardwareProgress = window.findChild<QProgressBar*>(QStringLiteral("HardwareProgress"));
    auto* hardwareStatus = window.findChild<QLabel*>(QStringLiteral("HardwareStatus"));
    auto* hardwareSections = window.findChild<QWidget*>(QStringLiteral("HardwareSections"));
    if (hardwareCopy == nullptr || hardwareRefresh == nullptr || hardwareProgress == nullptr
        || hardwareStatus == nullptr || hardwareSections == nullptr
        || hardwareCopy->text() != QStringLiteral("📋 Скопировать характеристики")
        || hardwareRefresh->text() != QStringLiteral("Обновить") || hardwareProgress->maximum() != 15
        || !hardwareProgress->isHidden() || hardwareCopy->isEnabled()
        || !hardwareStatus->text().contains(QStringLiteral("первом открытии")))
    {
        std::cerr << "The lazy native Hardware inventory UI contract is incomplete.\n";
        return EXIT_FAILURE;
    }

    auto* hardwareWorker = window.findChild<orion::app::HardwareInventoryWorker*>();
    int hardwareSteps = 0;
    bool hardwareProgressValid = true;
    QJsonObject hardwareReport;
    QObject::connect(hardwareWorker, &orion::app::HardwareInventoryWorker::progressChanged, &window,
        [&](int completed, int total, const QString&)
        { hardwareProgressValid &= completed == ++hardwareSteps && total == 15; });
    QObject::connect(hardwareWorker, &orion::app::HardwareInventoryWorker::reportReady, &window,
        [&](const QJsonObject& report) { hardwareReport = report; });
    tabs->setCurrentIndex(4);
    hardwareRefresh->click();
    if (hardwareWorker == nullptr || hardwareWorker->isRunning()
        || !hardwareStatus->text().contains(QStringLiteral("первого замера")))
    {
        std::cerr << "Hardware opened before telemetry must wait for its first complete seed.\n";
        return EXIT_FAILURE;
    }
    QEventLoop hardwareLoop;
    QObject::connect(hardwareWorker, &QThread::finished, &hardwareLoop, &QEventLoop::quit);
    QTimer::singleShot(10000, &hardwareLoop, &QEventLoop::quit);
    hardwareLoop.exec();
    int hardwareSectionCount = 0;
    for (auto* card : hardwareSections->findChildren<QFrame*>())
    {
        if (card->property("hardwareSection").isValid())
            ++hardwareSectionCount;
    }
    if (hardwareReport.isEmpty() || hardwareWorker->isRunning() || !hardwareProgressValid
        || hardwareSteps != 15 || hardwareSectionCount != 14 || !hardwareCopy->isEnabled()
        || !hardwareProgress->isHidden()
        || hardwareReport.value(QStringLiteral("cpu")).toObject().value(QStringLiteral("model")).toString()
            == QStringLiteral("н/д"))
    {
        std::cerr << "The complete lazy hardware report was not rendered after telemetry.\n";
        return EXIT_FAILURE;
    }
    tabs->setCurrentIndex(0);

    auto* storageDock = window.findChild<QDockWidget*>(QStringLiteral("OverviewDock_disk"));
    auto* storageBadge
        = storageDock ? storageDock->findChild<QPushButton*>(QStringLiteral("CardBadge")) : nullptr;
    if (!storageBadge || storageBadge->isHidden() || storageBadge->text().isEmpty())
    {
        std::cerr << "The storage type badge is missing after telemetry.\n";
        return EXIT_FAILURE;
    }
    storageBadge->click();
    if (tabs->currentWidget() != detailsScroll)
    {
        std::cerr << "The storage badge must open Details without selecting a hard-coded index.\n";
        return EXIT_FAILURE;
    }
    tabs->setCurrentIndex(0);
    tabs->setCurrentIndex(4);
    if (hardwareWorker->isRunning())
    {
        std::cerr << "Reopening Hardware must reuse its completed report.\n";
        return EXIT_FAILURE;
    }
    tabs->setCurrentIndex(0);

    // Optional local diagnostic artifact for real-machine inventory verification.
    const auto arguments = QCoreApplication::arguments();
    const int reportArgument = arguments.indexOf(QStringLiteral("--hardware-report"));
    if (reportArgument > 0 && reportArgument + 1 < arguments.size())
    {
        QSaveFile reportFile(arguments.at(reportArgument + 1));
        if (!reportFile.open(QIODevice::WriteOnly)
            || reportFile.write(QJsonDocument(hardwareReport).toJson()) < 0 || !reportFile.commit())
        {
            std::cerr << "Could not save the requested local hardware verification artifact.\n";
            return EXIT_FAILURE;
        }
    }
    QSet<QString> gpuIdentities;
    for (const auto& value : hardwareReport.value(QStringLiteral("gpu")).toArray())
    {
        const auto gpu = value.toObject();
        if (gpu.value(QStringLiteral("source")) != QStringLiteral("DXGI adapter inventory"))
            continue;
        const auto id = gpu.value(QStringLiteral("device_id")).toString();
        if (id.isEmpty() || gpuIdentities.contains(id))
        {
            std::cerr << "Native GPU inventory must retain a unique adapter identity.\n";
            return EXIT_FAILURE;
        }
        gpuIdentities.insert(id);
    }
    const auto disks = hardwareReport.value(QStringLiteral("disks")).toObject();
    if (!disks.value(QStringLiteral("groups")).isArray()
        || !disks.value(QStringLiteral("unmapped_volumes")).isArray())
    {
        std::cerr << "Disk inventory must explicitly separate known and unavailable mappings.\n";
        return EXIT_FAILURE;
    }
    for (const auto& value : hardwareReport.value(QStringLiteral("network_adapters")).toArray())
    {
        const auto adapter = value.toObject();
        if (!adapter.contains(QStringLiteral("receive_link_mbps"))
            || !adapter.contains(QStringLiteral("transmit_link_mbps")))
        {
            std::cerr << "Network inventory lost separate receive/transmit link rates.\n";
            return EXIT_FAILURE;
        }
    }

    auto* networkInterfaces = window.findChild<QComboBox*>(QStringLiteral("NetworkInterfaceCombo"));
    auto* networkScanButton = window.findChild<QPushButton*>(QStringLiteral("NetworkScanButton"));
    auto* networkScanProgress = window.findChild<QProgressBar*>(QStringLiteral("NetworkScanProgress"));
    auto* networkDeviceTable = window.findChild<QTableWidget*>(QStringLiteral("NetworkDeviceTable"));
    auto* networkPublicIp = window.findChild<QLabel*>(QStringLiteral("NetworkPublicIpValue"));
    auto* networkSpeedTest = window.findChild<QLabel*>(QStringLiteral("NetworkSpeedTestValue"));
    auto* publicIpRefresh = window.findChild<QPushButton*>(QStringLiteral("PublicIpRefreshButton"));
    auto* internetSpeedTest = window.findChild<QPushButton*>(QStringLiteral("InternetSpeedTestButton"));
    auto* fullScanButton = window.findChild<QPushButton*>(QStringLiteral("FullScanButton"));
    auto* internetProgress = window.findChild<QProgressBar*>(QStringLiteral("InternetToolsProgress"));
    if (networkInterfaces == nullptr || networkScanButton == nullptr || networkScanProgress == nullptr
        || networkDeviceTable == nullptr || networkPublicIp == nullptr || networkSpeedTest == nullptr
        || publicIpRefresh == nullptr || internetSpeedTest == nullptr || internetProgress == nullptr
        || fullScanButton == nullptr || fullScanButton->text() != QStringLiteral("Полная проверка")
        || !fullScanButton->toolTip().contains(QStringLiteral("5 МБ отдачи"))
        || publicIpRefresh->text() != QStringLiteral("Обновить IP / провайдера")
        || internetSpeedTest->text() != QStringLiteral("Тест скорости интернета")
        || !internetSpeedTest->toolTip().contains(QStringLiteral("10 МБ"))
        || networkDeviceTable->columnCount() != 5
        || networkDeviceTable->horizontalHeaderItem(0)->text() != QStringLiteral("IP")
        || networkDeviceTable->horizontalHeaderItem(1)->text() != QStringLiteral("MAC")
        || networkDeviceTable->horizontalHeaderItem(2)->text() != QStringLiteral("Имя")
        || QString(networkDeviceTable->horizontalHeaderItem(3)->text())
                .replace(QLatin1Char('\n'), QLatin1Char(' '))
            != QStringLiteral("Тип / производитель")
        || networkDeviceTable->horizontalHeaderItem(4)->text() != QStringLiteral("Задержка"))
    {
        std::cerr << "The native Network scanner UI contract is incomplete.\n";
        return EXIT_FAILURE;
    }

    auto* networkChart
        = window.findChild<orion::app::NetworkTrafficChart*>(QStringLiteral("NetworkTrafficChart"));
    if (networkChart == nullptr || networkChart->sampleCount() == 0
        || window.findChild<QWidget*>(QStringLiteral("NetworkTrafficScroll")) == nullptr)
    {
        std::cerr << "Network chart did not collect telemetry before opening its tab.\n";
        return EXIT_FAILURE;
    }
    auto* cpuChart = window.findChild<orion::app::CpuCoreChartWidget*>(QStringLiteral("CpuCoreChart"));
    auto* cpuChartMode = window.findChild<QComboBox*>(QStringLiteral("CpuCoreChartMode"));
    auto* cpuChartGroup = window.findChild<QComboBox*>(QStringLiteral("CpuCoreChartGroup"));
    auto* cpuChartPlot = window.findChild<QWidget*>(QStringLiteral("CpuCorePlot"));
    if (cpuChart == nullptr || cpuChartMode == nullptr || cpuChartGroup == nullptr || cpuChartPlot == nullptr
        || cpuChartMode->count() != 3 || cpuChartMode->itemData(0).toString() != QStringLiteral("profile")
        || cpuChartMode->itemData(1).toString() != QStringLiteral("bars")
        || cpuChartMode->itemData(2).toString() != QStringLiteral("history")
        || cpuChartMode->itemText(2) != QStringLiteral("История 60 секунд"))
    {
        std::cerr << "The native multimode CPU history contract is incomplete.\n";
        return EXIT_FAILURE;
    }
    cpuChart->setMode(QStringLiteral("history"));
    QCoreApplication::processEvents();
    orion::storage::AppSettings chartSettings;
    QString chartSettingsError;
    if (cpuChart->currentMode() != QStringLiteral("history") || cpuChartGroup->isHidden()
        || !orion::storage::AppSettings::load(
            temporaryDirectory.filePath(QStringLiteral("user_settings.json")), chartSettings,
            &chartSettingsError)
        || chartSettings.cpuCoreChartMode != QStringLiteral("history"))
    {
        std::cerr << "The selected CPU chart mode was not persisted immediately.\n";
        return EXIT_FAILURE;
    }
    cpuChart->setMode(QStringLiteral("profile"));

    if (window.findChildren<QDockWidget*>().size() != 8)
    {
        std::cerr << "Overview and diagnostics must expose five metric and three diagnostic docks.\n";
        return EXIT_FAILURE;
    }

    const auto buttons = window.findChildren<QPushButton*>();
    const auto hasButton = [&buttons](const QString& text)
    {
        for (const auto* button : buttons)
        {
            if (button->text() == text)
            {
                return true;
            }
        }
        return false;
    };
    if (!hasButton(QStringLiteral("⚙ Настройки")) || !hasButton(QStringLiteral("📊 Deep Telemetry"))
        || !hasButton(QStringLiteral("⏸ Пауза")) || !hasButton(QStringLiteral("⚡ Проблема сейчас")))
    {
        std::cerr << "The original toolbar action contract is incomplete.\n";
        return EXIT_FAILURE;
    }

    QPushButton* settingsButton = nullptr;
    for (auto* button : buttons)
    {
        if (button->text() == QStringLiteral("⚙ Настройки"))
        {
            settingsButton = button;
            break;
        }
    }
    settingsButton->click();
    QCoreApplication::processEvents();
    auto* settingsTabs = window.findChild<QTabWidget*>(QStringLiteral("SettingsTabs"));
    const QStringList expectedSettingsTabs { QStringLiteral("Оформление"), QStringLiteral("Окно"),
        QStringLiteral("Карточки и панели"), QStringLiteral("Дополнительно") };
    if (settingsTabs == nullptr || settingsTabs->count() != expectedSettingsTabs.size())
    {
        std::cerr << "The four-part native settings panel is incomplete.\n";
        return EXIT_FAILURE;
    }
    for (qsizetype index = 0; index < expectedSettingsTabs.size(); ++index)
    {
        if (settingsTabs->tabText(static_cast<int>(index)) != expectedSettingsTabs[index])
        {
            std::cerr << "Unexpected settings tab order.\n";
            return EXIT_FAILURE;
        }
    }
    auto* themeCombo = window.findChild<QComboBox*>(QStringLiteral("ThemeCombo"));
    auto* freeResize = window.findChild<QCheckBox*>(QStringLiteral("FreeFormResize"));
    auto* gpuVisible = window.findChild<QCheckBox*>(QStringLiteral("CardGpuVisible"));
    auto* pingTarget = window.findChild<QLineEdit*>(QStringLiteral("PingTarget"));
    auto* terminalEnabled = window.findChild<QCheckBox*>(QStringLiteral("TerminalEnabled"));
    auto* serverTabEnabled = window.findChild<QCheckBox*>(QStringLiteral("ServerTabEnabled"));
    auto* gamerMode = window.findChild<QCheckBox*>(QStringLiteral("GamerMode"));
    auto* networkPing = window.findChild<QLabel*>(QStringLiteral("NetworkPingValue"));
    auto* settingsApply = window.findChild<QPushButton*>(QStringLiteral("SettingsApplyButton"));
    auto* gpuDock = window.findChild<QDockWidget*>(QStringLiteral("OverviewDock_gpu"));
    if (themeCombo == nullptr || freeResize == nullptr || gpuVisible == nullptr || pingTarget == nullptr
        || !pingTarget->isEnabled() || terminalEnabled == nullptr || !terminalEnabled->isEnabled()
        || serverTabEnabled == nullptr || !serverTabEnabled->isEnabled() || gamerMode == nullptr
        || !gamerMode->isEnabled() || networkPing == nullptr || settingsApply == nullptr
        || gpuDock == nullptr)
    {
        std::cerr << "Settings controls are not addressable by the native UI contract.\n";
        return EXIT_FAILURE;
    }
    themeCombo->setCurrentIndex(themeCombo->findData(QStringLiteral("quantum_cyan")));
    freeResize->setChecked(true);
    gpuVisible->setChecked(false);
    pingTarget->setText(QStringLiteral("127.0.0.1"));
    terminalEnabled->setChecked(true);
    serverTabEnabled->setChecked(true);
    gamerMode->setChecked(true);
    settingsApply->click();
    QCoreApplication::processEvents();
    QWidget* gamerOverlay = nullptr;
    for (auto* widget : QApplication::topLevelWidgets())
    {
        if (widget->objectName() == QStringLiteral("GamerOverlay"))
        {
            gamerOverlay = widget;
            break;
        }
    }
    if (gamerOverlay == nullptr || !gamerOverlay->isVisible() || window.isVisible()
        || !gamerOverlay->windowFlags().testFlag(Qt::Tool)
        || !gamerOverlay->windowFlags().testFlag(Qt::FramelessWindowHint)
        || !gamerOverlay->windowFlags().testFlag(Qt::WindowStaysOnTopHint)
        || gamerOverlay->findChild<QLabel*>(QStringLiteral("GamerCpu")) == nullptr
        || gamerOverlay->findChild<QLabel*>(QStringLiteral("GamerGpu")) == nullptr
        || gamerOverlay->findChild<QLabel*>(QStringLiteral("GamerRam")) == nullptr
        || gamerOverlay->findChild<QLabel*>(QStringLiteral("GamerNetwork")) == nullptr
        || gamerOverlay->findChild<QLabel*>(QStringLiteral("GamerPing")) == nullptr
        || gamerOverlay->findChild<QLabel*>(QStringLiteral("GamerFps")) == nullptr)
    {
        std::cerr << "Native Gamer Mode overlay contract is incomplete.\n";
        return EXIT_FAILURE;
    }
    const int gamerNetworkSamplesBefore = networkChart->sampleCount();
    const int gamerCoreSamplesBefore = window.findChild<orion::app::CpuCoreChartWidget*>()->sampleCount();
    QEventLoop gamerHistoryLoop;
    QTimer::singleShot(1600, &gamerHistoryLoop, &QEventLoop::quit);
    gamerHistoryLoop.exec();
    if (networkChart->sampleCount() <= gamerNetworkSamplesBefore)
    {
        std::cerr << "Network history stopped during Gamer Mode.\n";
        return EXIT_FAILURE;
    }
    if (window.findChild<orion::app::CpuCoreChartWidget*>()->sampleCount() <= gamerCoreSamplesBefore)
    {
        std::cerr << "CPU history stopped during Gamer Mode.\n";
        return EXIT_FAILURE;
    }
    gamerOverlay->close();
    QCoreApplication::processEvents();
    if (!window.isVisible() || gamerMode->isChecked())
    {
        std::cerr << "Closing Gamer Mode did not restore the main window.\n";
        return EXIT_FAILURE;
    }
    int visibleGaugeCount = 0;
    for (auto* gauge : window.findChildren<QWidget*>(QStringLiteral("MetricGauge")))
    {
        if (!gauge->isHidden())
            ++visibleGaugeCount;
    }
    if (visibleGaugeCount != 3 || networkChart->isHidden()
        || window.findChild<QWidget*>(QStringLiteral("OverviewPage"))->property("themeLayoutMode").toString()
            != QStringLiteral("gauge"))
    {
        std::cerr << "Quantum Cyan did not activate three overview gauges.\n";
        return EXIT_FAILURE;
    }
    orion::storage::AppSettings persistedSettings;
    QString settingsError;
    if (!gpuDock->isHidden() || !window.styleSheet().contains(QStringLiteral("#00E5FF"))
        || !orion::storage::AppSettings::load(
            temporaryDirectory.filePath(QStringLiteral("user_settings.json")), persistedSettings,
            &settingsError)
        || persistedSettings.themeKey != QStringLiteral("quantum_cyan") || !persistedSettings.freeFormResize
        || persistedSettings.cardGpuVisible || persistedSettings.pingTarget != QStringLiteral("127.0.0.1")
        || !persistedSettings.terminalEnabled || !persistedSettings.serverTabEnabled)
    {
        std::cerr << "Hot settings application or persistence is incomplete.\n";
        return EXIT_FAILURE;
    }
    if (tabs->count() != expectedTabs.size() + 2 || tabs->tabText(3) != QStringLiteral("Серверы")
        || tabs->tabText(tabs->count() - 1) != QStringLiteral("Терминал")
        || window.findChild<QWidget*>(QStringLiteral("ServerMonitorWidget")) == nullptr
        || window.findChild<QWidget*>(QStringLiteral("TerminalWidget")) == nullptr)
    {
        std::cerr << "Optional Servers and Terminal tabs were not inserted live.\n";
        return EXIT_FAILURE;
    }
    terminalEnabled->setChecked(false);
    serverTabEnabled->setChecked(false);
    settingsApply->click();
    QCoreApplication::processEvents();
    if (tabs->count() != expectedTabs.size() || tabs->tabText(3) != QStringLiteral("Диспетчер задач"))
    {
        std::cerr << "Optional tabs were not removed live without shifting the fixed contract.\n";
        return EXIT_FAILURE;
    }
    if (!orion::storage::AppSettings::load(temporaryDirectory.filePath(QStringLiteral("user_settings.json")),
            persistedSettings, &settingsError)
        || persistedSettings.terminalEnabled || persistedSettings.serverTabEnabled)
    {
        std::cerr << "Disabled optional tabs were not persisted.\n";
        return EXIT_FAILURE;
    }

    QPushButton* deepTelemetryButton = nullptr;
    for (auto* button : buttons)
    {
        if (button->text() == QStringLiteral("📊 Deep Telemetry"))
        {
            deepTelemetryButton = button;
            break;
        }
    }
    deepTelemetryButton->click();
    QCoreApplication::processEvents();
    const auto* deepDialog = window.findChild<QDialog*>(QStringLiteral("DeepTelemetryDialog"));
    const auto* frequencyChart = window.findChild<QWidget*>(QStringLiteral("DeepTelemetryFrequencyChart"));
    const auto* loadChart = window.findChild<QWidget*>(QStringLiteral("DeepTelemetryLoadChart"));
    const auto* errorsOutput = window.findChild<QTextEdit*>(QStringLiteral("DeepTelemetryErrors"));
    const auto* deepPause = window.findChild<QPushButton*>(QStringLiteral("DeepTelemetryPauseButton"));
    if (deepDialog == nullptr || !deepDialog->isVisible() || frequencyChart == nullptr || loadChart == nullptr
        || errorsOutput == nullptr || deepPause == nullptr)
    {
        std::cerr << "The native Deep Telemetry window contract is incomplete.\n";
        return EXIT_FAILURE;
    }
    const_cast<QPushButton*>(deepPause)->click();
    const auto* deepStatus = window.findChild<QLabel*>(QStringLiteral("DeepTelemetryStatus"));
    if (deepStatus == nullptr || !deepStatus->text().contains(QStringLiteral("Локальная пауза")))
    {
        std::cerr << "The native Deep Telemetry local pause did not engage.\n";
        return EXIT_FAILURE;
    }
    const_cast<QPushButton*>(deepPause)->click();
    const_cast<QDialog*>(deepDialog)->close();
    QCoreApplication::processEvents();

    themeCombo->setCurrentIndex(themeCombo->findData(QStringLiteral("slate_minimal")));
    settingsApply->click();
    QCoreApplication::processEvents();
    for (auto* gauge : window.findChildren<QWidget*>(QStringLiteral("MetricGauge")))
    {
        if (!gauge->isHidden())
        {
            std::cerr << "Slate Minimal did not remove gauge decoration.\n";
            return EXIT_FAILURE;
        }
    }
    for (auto* sparkline : window.findChildren<QWidget*>(QStringLiteral("MetricSparkline")))
    {
        if (!sparkline->isHidden())
        {
            std::cerr << "Slate Minimal did not hide overview sparklines.\n";
            return EXIT_FAILURE;
        }
    }
    const auto overviewCards = window.findChildren<QFrame*>(QStringLiteral("MetricCard"));
    int denseOverviewCards = 0;
    for (auto* card : overviewCards)
    {
        if (card->property("metricKey").isValid()
            && card->property("presentationMode").toString() == QStringLiteral("dense"))
        {
            ++denseOverviewCards;
        }
    }
    if (denseOverviewCards != 5 || tabs->property("denseLayout").toBool() != true
        || !window.styleSheet().contains(QStringLiteral("border-radius: 0px"))
        || !orion::storage::AppSettings::load(
            temporaryDirectory.filePath(QStringLiteral("user_settings.json")), persistedSettings,
            &settingsError)
        || persistedSettings.themeKey != QStringLiteral("slate_minimal"))
    {
        std::cerr << "Slate Minimal dense-layout contract is incomplete.\n";
        return EXIT_FAILURE;
    }

    if (!networkChart->isHidden())
    {
        std::cerr << "Slate Minimal did not hide the Network graph.\n";
        return EXIT_FAILURE;
    }
    const int hiddenNetworkSamplesBefore = networkChart->sampleCount();
    const int hiddenCpuSamplesBefore = cpuChart->sampleCount();
    tabs->setCurrentIndex(3);
    QEventLoop processLoop;
    QTimer::singleShot(1800, &processLoop, &QEventLoop::quit);
    processLoop.exec();
    if (cpuChart->sampleCount() <= hiddenCpuSamplesBefore)
    {
        std::cerr << "CPU history stopped while the Details tab was hidden.\n";
        return EXIT_FAILURE;
    }
    if (networkChart->sampleCount() <= hiddenNetworkSamplesBefore)
    {
        std::cerr << "Hidden Network graph stopped collecting in Slate Minimal.\n";
        return EXIT_FAILURE;
    }
    const auto* processTable = window.findChild<QTableWidget*>(QStringLiteral("ProcessTable"));
    if (processTable == nullptr || processTable->columnCount() != 7 || processTable->rowCount() == 0
        || processTable->rowCount() > 15)
    {
        std::cerr << "The lazy native process table did not populate its TOP-15 view.\n";
        return EXIT_FAILURE;
    }
    auto* processSearch = window.findChild<QLineEdit*>(QStringLiteral("ProcessSearch"));
    if (processSearch == nullptr)
    {
        std::cerr << "The process search control is missing.\n";
        return EXIT_FAILURE;
    }
    processSearch->setText(QStringLiteral("orion_ui_contract_tests"));
    QCoreApplication::processEvents();
    if (processTable->rowCount() < 1)
    {
        std::cerr << "Process search did not query the complete native inventory.\n";
        return EXIT_FAILURE;
    }

    tabs->setCurrentIndex(5);
    QEventLoop autostartLoop;
    QTimer::singleShot(1500, &autostartLoop, &QEventLoop::quit);
    autostartLoop.exec();
    const auto* autostartTable = window.findChild<QTableWidget*>(QStringLiteral("AutostartTable"));
    const auto* autostartSearch = window.findChild<QLineEdit*>(QStringLiteral("AutostartSearch"));
    if (autostartTable == nullptr || autostartTable->columnCount() != 5 || autostartSearch == nullptr)
    {
        std::cerr << "The native read-only autostart screen contract is incomplete.\n";
        return EXIT_FAILURE;
    }

    if (!verifyTableParity(window)) return EXIT_FAILURE;

    tabs->setCurrentIndex(6);
    const auto* reportPreview = window.findChild<QTextEdit*>(QStringLiteral("DiagnosticReportPreview"));
    QEventLoop diagnosticLoop;
    QTimer diagnosticPoll;
    diagnosticPoll.setInterval(50);
    QObject::connect(&diagnosticPoll, &QTimer::timeout, &diagnosticLoop,
        [&]
        {
            if (reportPreview != nullptr
                && reportPreview->toPlainText().contains(QStringLiteral("O.R.I.O.N.")))
            {
                diagnosticLoop.quit();
            }
        });
    diagnosticPoll.start();
    QTimer::singleShot(5000, &diagnosticLoop, &QEventLoop::quit);
    diagnosticLoop.exec();
    diagnosticPoll.stop();
    const auto* diagnosticTable = window.findChild<QTableWidget*>(QStringLiteral("DiagnosticFindingsTable"));
    const auto* errorsBadge = window.findChild<QPushButton*>(QStringLiteral("ErrorsBadge"));
    if (diagnosticTable == nullptr || diagnosticTable->columnCount() != 4 || reportPreview == nullptr
        || errorsBadge == nullptr || errorsBadge->text().contains(QStringLiteral("ещё не проверены"))
        || window.findChild<QProgressBar*>(QStringLiteral("StressProgress")) == nullptr
        || !hasButton(QStringLiteral("Запустить выбранные тесты"))
        || !hasButton(QStringLiteral("Экстренная остановка"))
        || !reportPreview->toPlainText().contains(QStringLiteral("O.R.I.O.N.")))
    {
        std::cerr << "The native diagnostics/report hub did not produce a report.\n";
        return EXIT_FAILURE;
    }

    const auto diagnosticCard = [&window](const QString& key) -> orion::app::DetailCard*
    {
        for (auto* card : window.findChildren<orion::app::DetailCard*>())
        {
            if (card->property("diagnosticSection").toString() == key)
                return card;
        }
        return nullptr;
    };
    auto* incidentCard = diagnosticCard(QStringLiteral("incident"));
    auto* cpuTemperatureCard = diagnosticCard(QStringLiteral("cpu_temperature"));
    auto* gpuTemperatureCard = diagnosticCard(QStringLiteral("gpu_temperature"));
    auto* publicIpCard = diagnosticCard(QStringLiteral("public_ip"));
    auto* diagnosticLog = window.findChild<QTextEdit*>(QStringLiteral("DiagnosticLogErrorsText"));
    auto* diagnosticWorker = window.findChild<orion::app::DiagnosticWorker*>();
    auto* internetWorker = window.findChild<orion::app::InternetToolsWorker*>();
    if (!incidentCard || !cpuTemperatureCard || !gpuTemperatureCard || !publicIpCard || !diagnosticLog
        || !diagnosticWorker || !internetWorker || !hasButton(QStringLiteral("⚡ Проблема произошла сейчас"))
        || cpuTemperatureCard->fieldValue(QStringLiteral("state")).isEmpty()
        || gpuTemperatureCard->fieldValue(QStringLiteral("state")).isEmpty())
    {
        std::cerr << "The original passive diagnostic cards are missing.\n";
        return EXIT_FAILURE;
    }
    diagnosticWorker->reportReady(QJsonObject {
        { QStringLiteral("risk_assessment"),
            QJsonObject { { QStringLiteral("headline"), QStringLiteral("Fixture") } } },
        { QStringLiteral("coverage"),
            QJsonObject { { QStringLiteral("message"), QStringLiteral("Fixture coverage") } } },
        { QStringLiteral("diagnostics"),
            QJsonObject {
                { QStringLiteral("smart"),
                    QJsonObject {
                        { QStringLiteral("available"), true },
                        { QStringLiteral("source"), QStringLiteral("fixture smartctl") },
                        { QStringLiteral("disks"),
                            QJsonArray { QJsonObject {
                                { QStringLiteral("device"), QStringLiteral("/dev/fixture") },
                                { QStringLiteral("model"), QStringLiteral("Fixture NVMe") },
                                { QStringLiteral("health"), QStringLiteral("PASSED") },
                                { QStringLiteral("disk_type"), QStringLiteral("NVMe SSD") },
                                { QStringLiteral("is_nvme"), true },
                                { QStringLiteral("reallocated"), 0 },
                                { QStringLiteral("pending"), 0 },
                                { QStringLiteral("uncorrectable"), 0 },
                                { QStringLiteral("temperature_c"), 71.0 },
                                { QStringLiteral("nvme_critical_warning"), 0 },
                                { QStringLiteral("nvme_percentage_used"), 91 },
                                { QStringLiteral("nvme_media_errors"), 0 },
                                { QStringLiteral("level"), QStringLiteral("warn") },
                                { QStringLiteral("risk_reasons"),
                                    QJsonArray { QJsonObject { { QStringLiteral("text"),
                                        QStringLiteral("NVMe близок к расчётному ресурсу") } } } },
                            } } },
                    } },
                { QStringLiteral("log_errors"),
                    QJsonObject {
                        { QStringLiteral("errors"),
                            QJsonArray { QStringLiteral("Fixture display error"),
                                QStringLiteral("Fixture display error") } },
                        { QStringLiteral("groups"),
                            QJsonArray { QJsonObject { { QStringLiteral("count"), 2 },
                                { QStringLiteral("example"), QStringLiteral("Fixture display error") } } } },
                        { QStringLiteral("data_quality"), QStringLiteral("estimated") },
                        { QStringLiteral("note"), QStringLiteral("УЧЕБНЫЕ ДАННЫЕ: журнал прочитан не полностью, достигнут лимит объёма") },
                        { QStringLiteral("collection_limited"), true },
                        { QStringLiteral("source"), QStringLiteral("fixture log") },
                    } },
            } },
        { QStringLiteral("findings"), QJsonArray {} },
    });
    internetWorker->resultReady(orion::app::InternetOperation::PublicIpLookup,
        QJsonObject {
            { QStringLiteral("ok"), true },
            { QStringLiteral("public_ip"), QStringLiteral("198.51.100.42") },
            { QStringLiteral("provider"), QStringLiteral("Fixture ISP") },
            { QStringLiteral("location"), QStringLiteral("Bucharest, RO") },
            { QStringLiteral("source"), QStringLiteral("fixture") },
            { QStringLiteral("checked_at"), QStringLiteral("2026-09-14T12:00:00+03:00") },
        });
    auto* smartCard = diagnosticCard(QStringLiteral("smart"));
    if (!smartCard || smartCard->fieldValue(QStringLiteral("health")) != QStringLiteral("PASSED")
        || smartCard->property("statusLevel").toString() != QStringLiteral("warn")
        || !diagnosticLog->toPlainText().contains(QStringLiteral("[2×] Fixture display error"))
        || !diagnosticLog->toPlainText().startsWith(QStringLiteral("УЧЕБНЫЕ ДАННЫЕ: журнал прочитан не полностью"))
        || publicIpCard->fieldValue(QStringLiteral("public_ip")) != QStringLiteral("198.51.100.42")
        || publicIpCard->fieldValue(QStringLiteral("provider")) != QStringLiteral("Fixture ISP"))
    {
        std::cerr << "Passive diagnostic data was not rendered through the real signal connections.\n";
        return EXIT_FAILURE;
    }

    const auto logCapture = qEnvironmentVariable("ORION_LOG_LIMITS_UI_CAPTURE");
    if (!logCapture.isEmpty()) {
        QCoreApplication::processEvents();
        auto* logCard = diagnosticCard(QStringLiteral("system_errors"));
        if (!logCard || !logCard->grab().save(logCapture)) return EXIT_FAILURE;
    }

    const QJsonObject incidentFixture{{"incident_id", "fixture-only"}, {"status", "complete"},
        {"marker_timestamp", QStringLiteral("УЧЕБНЫЕ ДАННЫЕ · не результат проверки ПК")},
        {"pre_seconds", 60}, {"post_seconds", 15}, {"log_pre_seconds", 120},
        {"summary", QJsonObject{{"measurement_contract", "fresh_incident_observations_v1"},
            {"stale_metric_count", 2}, {"repeated_interval_count", 1},
            {"data_quality", "estimated"}, {"sample_count", 12},
            {"focus_sample_count", 8}, {"leading_gap_seconds", 25}, {"max_sample_gap_seconds", 4.5},
            {"trailing_gap_seconds", 5}, {"duplicate_sample_count", 2}, {"invalid_timestamp_count", 1},
            {"conflicting_timestamp_count", 1}, {"focus_net_error_count", QJsonValue::Null},
            {"focus_net_drop_count", 3}, {"net_drops_known_sample_count", 4},
            {"net_drops_data_quality", "partial"}}}};
    diagnosticWorker->reportReady(QJsonObject{{"incident", incidentFixture}});
    if (incidentCard->property("statusLevel").toString() == "ok"
        || !incidentCard->fieldValue("samples").contains(QStringLiteral("неполное окно"))
        || incidentCard->fieldValue("network_counts") != QStringLiteral("ошибки: н/д; потери: ≥3")
        || incidentCard->fieldValue("network_coverage") != QStringLiteral("ошибки 0/8; потери 4/8 замеров")
        || !incidentCard->fieldValue("source_freshness").contains(QStringLiteral("старых: 2"))
        || !incidentCard->fieldValue("coincidences").contains(QStringLiteral("RAM 0; CPU 0"))
        || !incidentCard->fieldValue("gaps").contains("25.0")
        || !incidentCard->fieldValue("log_window").contains("120")) {
        std::cerr << "Incident card confused completed capture with health or hid missing counters.\n";
        return EXIT_FAILURE;
    }
    const auto markerBefore = incidentCard->fieldValue("marked_at");
    QMetaObject::invokeMethod(&window, "finalizeIncident", Qt::DirectConnection,
        Q_ARG(QJsonObject, (QJsonObject{{"incident_id", "obsolete-request"}})));
    QMetaObject::invokeMethod(&window, "finalizeIncident", Qt::DirectConnection,
        Q_ARG(QJsonObject, QJsonObject{}));
    if (incidentCard->fieldValue("marked_at") != markerBefore) {
        std::cerr << "Late/unidentified incident result replaced the current presentation.\n";
        return EXIT_FAILURE;
    }
    const auto incidentCapture = qEnvironmentVariable("ORION_INCIDENT_UI_CAPTURE");
    if (!incidentCapture.isEmpty()) {
        QCoreApplication::processEvents();
        if (!incidentCard->grab().save(incidentCapture)) return EXIT_FAILURE;
    }

    // Synthetic partial log goes through the real report-to-UI connection.
    const QString partialWhea = QStringLiteral("2026-09-26T12:00:03Z WHEA Machine Check — УЧЕБНЫЕ ДАННЫЕ, не диагностика ПК");
    const auto findingsReport = orion::diagnostics::buildReport(QJsonObject{{"incident", QJsonObject{
        {"marker_timestamp", "2026-09-26T12:00:00Z"}, {"new_system_errors", QJsonArray{partialWhea}},
        {"recent_system_errors", QJsonArray{partialWhea}},
        {"recent_system_error_correlation", QJsonObject{{"data_quality", "estimated"}, {"collection_limited", true},
            {"comparison_supported", true}, {"comparison_quality", "estimated"}}},
        {"summary", QJsonObject{{"data_quality", "valid"}}}}}});
    diagnosticWorker->reportReady(findingsReport);
    bool partialConfidenceVisible = false;
    for (int row = 0; row < diagnosticTable->rowCount(); ++row)
        if (diagnosticTable->item(row, 2)->text().contains("WHEA"))
            partialConfidenceVisible = diagnosticTable->item(row, 3)->text().contains(QStringLiteral("средн"), Qt::CaseInsensitive);
    if (!partialConfidenceVisible || !reportPreview->toPlainText().contains(QStringLiteral("новизна не подтверждена"))
        || !reportPreview->toPlainText().contains(QStringLiteral("журнал около отметки проверен не полностью"))) {
        std::cerr << "Incident partial-log confidence/qualification disappeared from production UI.\n";
        return EXIT_FAILURE;
    }

    tabs->setCurrentIndex(7);
    if (window.findChild<QLineEdit*>(QStringLiteral("AppMonitorExecutable")) == nullptr
        || window.findChild<QProgressBar*>(QStringLiteral("AppMonitorProgress")) == nullptr
        || window.findChild<QTableWidget*>(QStringLiteral("AppMonitorSamples")) == nullptr
        || window.findChild<QTextEdit*>(QStringLiteral("AppMonitorReportPreview")) == nullptr
        || !hasButton(QStringLiteral("Запустить и наблюдать"))
        || !hasButton(QStringLiteral("Остановить наблюдение")))
    {
        std::cerr << "The native problematic-application monitor contract is incomplete.\n";
        return EXIT_FAILURE;
    }

    if (!verifyAppMonitorUi(window)) return EXIT_FAILURE;
    QMetaObject::invokeMethod(&window, "togglePaused", Qt::DirectConnection);
    const int pausedNetworkSamples = networkChart->sampleCount();
    QEventLoop pauseHistoryLoop;
    QTimer::singleShot(1300, &pauseHistoryLoop, &QEventLoop::quit);
    pauseHistoryLoop.exec();
    if (!networkChart->isMonitoringPaused() || networkChart->sampleCount() != pausedNetworkSamples)
    {
        std::cerr << "Global pause failed to freeze Network history.\n";
        return EXIT_FAILURE;
    }
    QMetaObject::invokeMethod(&window, "togglePaused", Qt::DirectConnection);
    QEventLoop resumeHistoryLoop;
    QTimer::singleShot(1600, &resumeHistoryLoop, &QEventLoop::quit);
    resumeHistoryLoop.exec();
    if (networkChart->isMonitoringPaused() || networkChart->sampleCount() <= pausedNetworkSamples
        || !networkChart->samples().at(pausedNetworkSamples).breakBefore)
    {
        std::cerr << "Resuming Network history failed to leave a pause gap.\n";
        return EXIT_FAILURE;
    }
    bool startupFinished = false;
    int startupFinishes = 0;
    int extraHardwareScans = 0;
    QObject::connect(hardwareWorker, &orion::app::HardwareInventoryWorker::scanStarted, &window,
        [&] { ++extraHardwareScans; });
    QEventLoop startupLoop;
    QObject::connect(&window, &orion::app::MainWindow::startupFinished, &startupLoop,
        [&](bool)
        {
            startupFinished = true;
            ++startupFinishes;
            startupLoop.quit();
        });
    window.beginStartupScan(false);
    window.beginStartupScan(false);
    QTimer::singleShot(2000, &startupLoop, &QEventLoop::quit);
    startupLoop.exec();
    auto* startupSequence = window.findChild<orion::app::StartupSequence*>();
    if (!startupFinished || startupFinishes != 1 || extraHardwareScans != 0 || startupSequence == nullptr
        || startupSequence->progress() != 100 || startupSequence->isActive())
    {
        std::cerr << "Startup integration did not reuse prepared reports or started duplicate work.\n";
        return EXIT_FAILURE;
    }
    // Deterministic network presentation through the real telemetry connection.
    // Stop only this fixture's collector after the lifecycle checks have finished.
    auto* telemetryWorker = window.findChild<orion::app::TelemetryWorker*>();
    telemetryWorker->stop();
    if (!telemetryWorker->wait(3000))
        return EXIT_FAILURE;
    QCoreApplication::processEvents();
    const auto counterLabel
        = [&window](const char* name) { return window.findChild<QLabel*>(QString::fromLatin1(name)); };
    auto* received = counterLabel("NetworkReceivedValue");
    auto* sent = counterLabel("NetworkSentValue");
    auto* errors = counterLabel("NetworkErrorsValue");
    auto* drops = counterLabel("NetworkDropsValue");
    auto* resetPanel = window.findChild<QToolButton*>(QStringLiteral("NetworkPanelReset"));
    auto* networkSplitter = window.findChild<QSplitter*>(QStringLiteral("NetworkSplitter"));
    if (!received || !sent || !errors || !drops || !resetPanel || !networkSplitter)
    {
        std::cerr << "Original network counters or local panel reset missing.\n";
        return EXIT_FAILURE;
    }
    using Counter = orion::core::Metric<std::uint64_t>;
    orion::app::RuntimeTelemetry networkFixture;
    networkFixture.network.receivedBytes = Counter::valid(2ULL * 1024 * 1024 * 1024, "fixture");
    networkFixture.network.sentBytes = Counter::valid(0, "fixture");
    networkFixture.network.errors = Counter::valid(9007199254740993ULL, "fixture");
    networkFixture.network.intervalErrors = Counter::valid(7, "fixture");
    networkFixture.network.drops = Counter::valid(15, "fixture");
    networkFixture.network.intervalDrops = Counter::valid(0, "fixture");
    const auto publishNetwork = [&]
    {
        telemetryWorker->sampleReady("fixture", "Fixture OS", "CPU", "GPU", 20, { 20 }, { 3000 }, 3000, -1,
            20, 40, 8, 1, 12.5, 50, 32, networkFixture, {}, {}, {}, 1024, 2048);
    };
    publishNetwork();
    if (received->text() != QStringLiteral("2.00 ГБ") || sent->text() != QStringLiteral("0.00 ГБ")
        || errors->text() != QStringLiteral("7 (всего: 9007199254740993)")
        || drops->text() != QStringLiteral("0 (всего: 15)"))
    {
        std::cerr << "Network counters did not reach the original display fields precisely.\n";
        return EXIT_FAILURE;
    }
    QMetaObject::invokeMethod(&window, "togglePaused", Qt::DirectConnection);
    networkFixture.network.receivedBytes = {};
    publishNetwork();
    if (received->text() != QStringLiteral("2.00 ГБ"))
    {
        std::cerr << "Queued telemetry changed paused network counters.\n";
        return EXIT_FAILURE;
    }
    QMetaObject::invokeMethod(&window, "togglePaused", Qt::DirectConnection);
    networkFixture.network.errors.quality = orion::core::DataQuality::Stale;
    networkFixture.network.intervalErrors = {};
    publishNetwork();
    if (received->text() != QStringLiteral("н/д") || errors->text() != QStringLiteral("н/д (всего: н/д)"))
    {
        std::cerr << "Missing/stale counters became valid zeroes.\n";
        return EXIT_FAILURE;
    }
    tabs->setCurrentIndex(2);
    QCoreApplication::processEvents();
    networkSplitter->setSizes({ 210, 620 });
    const auto narrowSizes = networkSplitter->sizes();
    const auto beforeResetGeometry = window.geometry();
    const auto overviewState = window.findChild<QMainWindow*>(QStringLiteral("OverviewDockHost"));
    const QByteArray beforeResetDocks = overviewState ? overviewState->saveState() : QByteArray {};
    resetPanel->click();
    if (networkSplitter->sizes().front() <= narrowSizes.front() || window.geometry() != beforeResetGeometry
        || (overviewState && overviewState->saveState() != beforeResetDocks))
    {
        std::cerr << "Network reset did not stay local to its splitter.\n";
        return EXIT_FAILURE;
    }
    auto* deepScan = window.findChild<QPushButton*>(QStringLiteral("DeepScanButton"));
    auto* deepWorker = window.findChild<orion::app::DeepScanWorker*>();
    if (!deepScan || !deepWorker || deepWorker->isRunning())
        return EXIT_FAILURE;
    bool confirmationSeen = false;
    bool defaultNo = false;
    QTimer::singleShot(0, &window,
        [&]
        {
            for (auto* widget : QApplication::topLevelWidgets())
            {
                if (auto* box = qobject_cast<QMessageBox*>(widget))
                {
                    confirmationSeen = box->objectName() == QStringLiteral("DeepScanConfirmation");
                    defaultNo = box->defaultButton() == box->button(QMessageBox::No);
                    box->done(QMessageBox::No);
                }
            }
        });
    deepScan->click();
    if (!confirmationSeen || !defaultNo || deepWorker->isRunning()
        || window.findChild<orion::app::DeepScanDialog*>() != nullptr)
    {
        std::cerr << "Deep scan ignored its default-No confirmation boundary.\n";
        return EXIT_FAILURE;
    }
    auto* fullWorker = window.findChild<orion::app::FullScanWorker*>();
    if (!fullWorker || fullWorker->isRunning())
        return EXIT_FAILURE;
    confirmationSeen = false;
    defaultNo = false;
    QTimer::singleShot(0, &window,
        [&]
        {
            for (auto* widget : QApplication::topLevelWidgets())
            {
                if (auto* box = qobject_cast<QMessageBox*>(widget))
                {
                    confirmationSeen = box->objectName() == QStringLiteral("FullScanConfirmation")
                        && box->text().contains(QStringLiteral("Cloudflare"))
                        && box->text().contains(QStringLiteral("Центра безопасности Windows"));
                    defaultNo = box->defaultButton() == box->button(QMessageBox::No);
                    box->done(QMessageBox::No);
                }
            }
        });
    fullScanButton->click();
    if (!confirmationSeen || !defaultNo || fullWorker->isRunning()
        || window.findChild<orion::app::FullScanDialog*>() != nullptr)
    {
        std::cerr << "Full scan ignored its explicit default-No load/network confirmation.\n";
        return EXIT_FAILURE;
    }
    fullWorker->reportReady(QJsonObject{
        {QStringLiteral("scan"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("full")},
                                              {QStringLiteral("state"), QStringLiteral("cancelled")}}},
        {QStringLiteral("risk_assessment"),
            QJsonObject{{QStringLiteral("headline"), QStringLiteral("Fixture full partial")}}},
        {QStringLiteral("pc_rating"), QJsonObject{{QStringLiteral("label"), QStringLiteral("обычный")}}},
        {QStringLiteral("internet_rating"), QJsonObject{{QStringLiteral("label"), QStringLiteral("н/д")}}},
    });
    if (!window.findChild<QLabel*>(QStringLiteral("DiagnosticStatus"))->text()
             .contains(QStringLiteral("Полная проверка неполная"))
        || !window.findChild<QTextEdit*>(QStringLiteral("DiagnosticReportPreview"))->toPlainText()
                .contains(QStringLiteral("ПОЛНАЯ ПРОВЕРКА")))
    {
        std::cerr << "Main page lost the incomplete full-scan state.\n";
        return EXIT_FAILURE;
    }
    // Deliver a partial report through the real connection without starting a scan.
    deepWorker->reportReady(
        QJsonObject { { "scan", QJsonObject { { "kind", "deep_local" }, { "state", "cancelled" } } },
            { "risk_assessment", QJsonObject { { "headline", "Fixture partial result" } } } });
    if (!window.findChild<QLabel*>(QStringLiteral("DiagnosticStatus"))
            ->text()
            .contains(QStringLiteral("частичный отчёт"))
        || !window.findChild<QTextEdit*>(QStringLiteral("DiagnosticReportPreview"))
            ->toPlainText()
            .contains(QStringLiteral("остановлено, частичный отчёт")))
    {
        std::cerr << "Main page lost the incomplete deep-scan state.\n";
        return EXIT_FAILURE;
    }
    window.setFixedSize(730, 800);
    tabs->setCurrentIndex(6);
    QCoreApplication::processEvents();
    for (const auto* key : { "diagnostics", "stress_test", "report" })
    {
        auto* scroll = window.findChild<QScrollArea*>(QStringLiteral("DiagnosticsPanelScroll_%1").arg(key));
        auto* dock = window.findChild<QDockWidget*>(QStringLiteral("DiagnosticsHubDock_%1").arg(key));
        if (!scroll || !dock || !scroll->widgetResizable()
            || !dock->parentWidget()->rect().contains(dock->geometry())
            || (scroll->widget()->width() > scroll->viewport()->width()
                && scroll->horizontalScrollBar()->maximum() <= 0))
        {
            std::cerr << "Narrow diagnostic dock clips controls without scrolling.\n";
            return EXIT_FAILURE;
        }
    }
    if (!verifyDiagnosticLayout(window, temporaryDirectory.filePath(QStringLiteral("user_settings.json"))))
        return EXIT_FAILURE;
    if (!verifyFloatingCards(window)) return EXIT_FAILURE;
    std::cout << "Native main-window UI contract passed.\n";
    return EXIT_SUCCESS;
}
