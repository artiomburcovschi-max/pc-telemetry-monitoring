#include "main_window.h"

#include "autostart_worker.h"
#include "app_monitor_worker.h"
#include "diagnostic_worker.h"
#include "diagnostic_dock_layout.h"
#include "deep_scan_worker.h"
#include "deep_scan_dialog.h"
#include "deep_telemetry_dialog.h"
#include "full_scan_dialog.h"
#include "full_scan_worker.h"
#include "hardware_inventory_worker.h"
#include "incident_worker.h"
#include "internet_tools_worker.h"
#include "network_scanner_worker.h"
#include "process_worker.h"
#include "process_action_worker.h"
#include "stress_worker.h"
#include "telemetry_worker.h"
#include "widgets/metric_card.h"
#include "widgets/independent_dock_widget.h"
#include "widgets/details_panel.h"
#include "widgets/gamer_overlay.h"
#include "widgets/cpu_core_chart_widget.h"
#include "widgets/network_traffic_chart.h"
#include "widgets/app_monitor_chart.h"
#include "widgets/server_monitor_widget.h"
#include "widgets/sparkline_widget.h"
#include "widgets/terminal_widget.h"

#include "orion/core/thresholds.h"
#include "orion/diagnostics/report_contract.h"
#include "orion/diagnostics/diagnostic_engine.h"
#include "orion/diagnostics/diagnostic_schema.h"
#include "orion/diagnostics/runtime_diagnostics.h"
#include "orion/diagnostics/system_diagnostics_collector.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFormLayout>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QSysInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QVariant>
#include <QVBoxLayout>

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <utility>

namespace orion::app
{
namespace
{

    QString diagnosticReportText(const QJsonObject& report)
    {
        const QString kind = report.value(QStringLiteral("scan")).toObject()
                                 .value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("deep_local"))
            return DeepScanWorker::reportText(report);
        if (kind == QStringLiteral("full"))
            return FullScanWorker::reportText(report);
        return orion::diagnostics::reportToText(report);
    }

    struct ThemePalette
    {
        QString background;
        QString panel;
        QString text;
        QString muted;
        QString accent;
        QString secondary;
        QString border;
        QString graph;
        QString font;
    };

    [[nodiscard]] QString customColor(const QJsonObject& colors, const QString& key, const QString& fallback)
    {
        const QColor candidate(colors.value(key).toString());
        return candidate.isValid() ? candidate.name(QColor::HexRgb).toUpper() : fallback;
    }

    [[nodiscard]] ThemePalette paletteFor(const orion::storage::AppSettings& settings)
    {
        if (settings.themeKey == QStringLiteral("quantum_cyan"))
        {
            return { QStringLiteral("#050B14"), QStringLiteral("#0B1622"), QStringLiteral("#E4F6FF"),
                QStringLiteral("#5C8AA0"), QStringLiteral("#00E5FF"), QStringLiteral("#FF9F40"),
                QStringLiteral("#122A3A"), QStringLiteral("#00E5FF"),
                QStringLiteral("'Orbitron', 'Consolas', monospace") };
        }
        if (settings.themeKey == QStringLiteral("matrix_terminal"))
        {
            return { QStringLiteral("#000000"), QStringLiteral("#050505"), QStringLiteral("#00FF41"),
                QStringLiteral("#008F11"), QStringLiteral("#00FF41"), QStringLiteral("#00CC33"),
                QStringLiteral("#003B00"), QStringLiteral("#00FF41"),
                QStringLiteral("'Courier New', monospace") };
        }
        if (settings.themeKey == QStringLiteral("slate_minimal"))
        {
            return { QStringLiteral("#161616"), QStringLiteral("#1E1E1E"), QStringLiteral("#D0D0D0"),
                QStringLiteral("#7A7A7A"), QStringLiteral("#B0B0B0"), QStringLiteral("#8A8A8A"),
                QStringLiteral("#333333"), QStringLiteral("#B0B0B0"),
                QStringLiteral("'Consolas', monospace") };
        }
        if (settings.themeKey == QStringLiteral("custom"))
        {
            const QString background
                = customColor(settings.customThemeColors, QStringLiteral("bg"), QStringLiteral("#121212"));
            const QString text
                = customColor(settings.customThemeColors, QStringLiteral("text"), QStringLiteral("#F2F2F7"));
            const QString accent = customColor(
                settings.customThemeColors, QStringLiteral("accent"), QStringLiteral("#FF9F0A"));
            const auto mix = [&background, &text](double foregroundAmount)
            {
                const QColor bg(background);
                const QColor fg(text);
                return QColor::fromRgbF(bg.redF() + (fg.redF() - bg.redF()) * foregroundAmount,
                    bg.greenF() + (fg.greenF() - bg.greenF()) * foregroundAmount,
                    bg.blueF() + (fg.blueF() - bg.blueF()) * foregroundAmount)
                    .name();
            };
            return { background, mix(0.05), text, mix(0.65), accent, QColor(accent).lighter(115).name(),
                mix(0.18), customColor(settings.customThemeColors, QStringLiteral("graph"), accent),
                QStringLiteral("'Consolas', monospace") };
        }
        return { QStringLiteral("#0A0A0C"), QStringLiteral("#141417"), QStringLiteral("#E8E8EC"),
            QStringLiteral("#6E6E76"), QStringLiteral("#F5C518"), QStringLiteral("#8A8F98"),
            QStringLiteral("#2A2A2E"), QStringLiteral("#F5C518"), QStringLiteral("'Consolas', monospace") };
    }

    void displayColor(QPushButton* button, const QString& color)
    {
        const QColor parsed(color);
        const QString normalized
            = parsed.isValid() ? parsed.name(QColor::HexRgb).toUpper() : QStringLiteral("#000000");
        button->setProperty("selectedColor", normalized);
        button->setText(normalized);
        button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; min-width: 105px; }"
                                             "QPushButton:hover { border-color: white; }")
                .arg(normalized,
                    parsed.lightness() < 128 ? QStringLiteral("#FFFFFF") : QStringLiteral("#000000")));
    }

    [[nodiscard]] QString formatRate(const double bytesPerSecond)
    {
        if (bytesPerSecond < 0.0)
        {
            return QStringLiteral("н/д");
        }
        if (bytesPerSecond >= 1024.0 * 1024.0)
        {
            return QStringLiteral("%1 МБ/с").arg(bytesPerSecond / (1024.0 * 1024.0), 0, 'f', 2);
        }
        return QStringLiteral("%1 КиБ/с").arg(bytesPerSecond / 1024.0, 0, 'f', 1);
    }

    [[nodiscard]] QLabel* makeValueLabel(const QString& text, QWidget* parent)
    {
        auto* label = new QLabel(text, parent);
        label->setObjectName(QStringLiteral("FieldValue"));
        label->setWordWrap(true);
        return label;
    }

    [[nodiscard]] orion::core::StatusLevel statusLevel(const QString& value)
    {
        if (value == QStringLiteral("critical"))
            return orion::core::StatusLevel::Critical;
        if (value == QStringLiteral("warn") || value == QStringLiteral("warning"))
            return orion::core::StatusLevel::Warning;
        if (value == QStringLiteral("ok") || value == QStringLiteral("pass"))
            return orion::core::StatusLevel::Ok;
        return orion::core::StatusLevel::Unknown;
    }

    [[nodiscard]] QString jsonNumber(
        const QJsonValue& value, const QString& suffix = {}, const int precision = 0)
    {
        if (!value.isDouble())
            return QStringLiteral("н/д");
        return QStringLiteral("%1%2").arg(QString::number(value.toDouble(), 'f', precision), suffix);
    }

    [[nodiscard]] QString durationText(const qint64 milliseconds)
    {
        const qint64 seconds = qMax<qint64>(0, milliseconds / 1000);
        if (seconds < 60)
            return QStringLiteral("%1 сек").arg(seconds);
        const qint64 minutes = seconds / 60;
        const qint64 remainder = seconds % 60;
        if (minutes < 60)
        {
            return remainder == 0 ? QStringLiteral("%1 мин").arg(minutes)
                                  : QStringLiteral("%1 мин %2 сек").arg(minutes).arg(remainder);
        }
        const qint64 hours = minutes / 60;
        const qint64 remainderMinutes = minutes % 60;
        return remainderMinutes == 0 ? QStringLiteral("%1 ч").arg(hours)
                                     : QStringLiteral("%1 ч %2 мин").arg(hours).arg(remainderMinutes);
    }

    void markDiagnosticCard(DetailCard* card, const QString& key)
    {
        // DetailCard is shared with Details, whose tests enumerate detailSection.
        // Diagnostics uses its own property so the two page contracts stay distinct.
        card->setProperty("detailSection", QVariant {});
        card->setProperty("diagnosticSection", key);
    }

} // namespace

MainWindow::MainWindow(const orion::storage::AppSettings& settings, QString settingsPath,
    const bool startOnlineLookup, QWidget* parent)
    : QMainWindow(parent)
    , settings_(settings)
    , settingsPath_(std::move(settingsPath))
{
    setWindowTitle(QStringLiteral("O.R.I.O.N. Monitoring"));
    applyWindowConstraints();
    resize(qBound(settings_.minimumWidth, 1100, settings_.maximumWidth),
        qBound(settings_.minimumHeight, 820, settings_.maximumHeight));
    setWindowOpacity(settings_.windowOpacity);
    if (settings_.alwaysOnTop)
    {
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
    }

    auto* toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);

    auto* settingsButton = new QPushButton(QStringLiteral("⚙ Настройки"), toolbar);
    settingsButton->setObjectName(QStringLiteral("ToolbarButton"));
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    toolbar->addWidget(settingsButton);

    auto* deepTelemetryButton = new QPushButton(QStringLiteral("📊 Deep Telemetry"), toolbar);
    deepTelemetryButton->setObjectName(QStringLiteral("ToolbarButton"));
    deepTelemetryButton->setToolTip(QStringLiteral("Отдельное окно детальной телеметрии"));
    connect(deepTelemetryButton, &QPushButton::clicked, this, &MainWindow::openDeepTelemetry);
    toolbar->addWidget(deepTelemetryButton);

    pauseButton_ = new QPushButton(QStringLiteral("⏸ Пауза"), toolbar);
    pauseButton_->setObjectName(QStringLiteral("ToolbarButton"));
    pauseButton_->setCheckable(true);
    pauseButton_->setToolTip(QStringLiteral("Приостановить всю фоновую телеметрию"));
    connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::togglePaused);
    toolbar->addWidget(pauseButton_);

    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    collectionStatus_ = new QLabel(QStringLiteral("Подготовка нативной телеметрии…"), toolbar);
    collectionStatus_->setObjectName(QStringLiteral("CollectionStatus"));
    toolbar->addWidget(collectionStatus_);
    toolbar->addSeparator();

    problemButton_ = new QPushButton(QStringLiteral("⚡ Проблема сейчас"), toolbar);
    problemButton_->setObjectName(QStringLiteral("DangerButton"));
    problemButton_->setToolTip(QStringLiteral("Сохранить 60 секунд до метки и 15 секунд после неё"));
    connect(problemButton_, &QPushButton::clicked, this, &MainWindow::markProblemNow);
    toolbar->addWidget(problemButton_);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    alarmBanner_ = new QLabel(central);
    alarmBanner_->setObjectName(QStringLiteral("AlarmBanner"));
    alarmBanner_->setAlignment(Qt::AlignCenter);
    alarmBanner_->hide();
    alarmClearTimer_ = new QTimer(this);
    alarmClearTimer_->setSingleShot(true);
    alarmClearTimer_->setInterval(5000);
    connect(alarmClearTimer_, &QTimer::timeout, alarmBanner_, &QLabel::hide);
    pauseBanner_ = new QLabel(
        QStringLiteral("⏸ Мониторинг приостановлен — фоновые измерения и тревоги не выполняются"), central);
    pauseBanner_->setObjectName(QStringLiteral("PauseBanner"));
    pauseBanner_->setAlignment(Qt::AlignCenter);
    pauseBanner_->hide();
    centralLayout->addWidget(alarmBanner_);
    centralLayout->addWidget(pauseBanner_);

    tabWidget_ = new QTabWidget(central);
    tabWidget_->setObjectName(QStringLiteral("MainTabs"));
    tabWidget_->setDocumentMode(true);
    tabWidget_->setUsesScrollButtons(true);
    tabWidget_->setElideMode(Qt::ElideNone);
    tabWidget_->tabBar()->setExpanding(false);
    overviewPage_ = createOverviewPage();
    detailsPage_ = createDetailsPage();
    networkPage_ = createNetworkPage();
    taskManagerPage_ = createTaskManagerPage();
    hardwarePage_ = createHardwarePage();
    autostartPage_ = createAutostartPage();
    diagnosticsPage_ = createDiagnosticsPage();
    appMonitorPage_ = createAppMonitorPage();
    tabWidget_->addTab(overviewPage_, QStringLiteral("Обзор"));
    tabWidget_->addTab(detailsPage_, QStringLiteral("Детали"));
    tabWidget_->addTab(networkPage_, QStringLiteral("Сети"));
    tabWidget_->addTab(taskManagerPage_, QStringLiteral("Диспетчер задач"));
    tabWidget_->addTab(hardwarePage_, QStringLiteral("Железо"));
    tabWidget_->addTab(autostartPage_, QStringLiteral("Автозагрузка"));
    tabWidget_->addTab(diagnosticsPage_, QStringLiteral("Диагностика и тесты"));
    tabWidget_->addTab(appMonitorPage_, QStringLiteral("Проблемное приложение"));
    for (int index = 0; index < tabWidget_->count(); ++index)
    {
        tabWidget_->setTabToolTip(index, tabWidget_->tabText(index));
    }
    centralLayout->addWidget(tabWidget_, 1);
    setCentralWidget(central);

    trayIcon_ = new QSystemTrayIcon(style()->standardIcon(QStyle::SP_ComputerIcon), this);
    trayIcon_->setToolTip(QStringLiteral("O.R.I.O.N. Monitoring"));
    auto* trayMenu = new QMenu(this);
    auto* showAction = trayMenu->addAction(QStringLiteral("Открыть O.R.I.O.N."));
    trayPauseAction_ = trayMenu->addAction(QStringLiteral("⏸ Пауза"));
    trayMenu->addSeparator();
    auto* quitAction = trayMenu->addAction(QStringLiteral("Выход"));
    trayIcon_->setContextMenu(trayMenu);
    connect(showAction, &QAction::triggered, this, &MainWindow::showFromTray);
    connect(trayPauseAction_, &QAction::triggered, this, &MainWindow::togglePaused);
    connect(quitAction, &QAction::triggered, this, &MainWindow::requestQuit);
    connect(trayIcon_, &QSystemTrayIcon::activated, this,
        [this](const auto reason)
        {
            if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
            {
                showFromTray();
            }
        });
    updateTrayVisibility();

    applyStyle();
    syncOptionalTabs();
    sessionTimer_.start();
    telemetryWorker_ = new TelemetryWorker(this);
    connect(telemetryWorker_, &TelemetryWorker::sampleReady, this, &MainWindow::applyTelemetry);
    telemetryWorker_->start();
    latestPing_.target = settings_.pingTarget;
    latestPing_.source = QStringLiteral("tcp_connect:53");
    latestPing_.reason = QStringLiteral("ожидание первой сетевой пробы");
    pingWorker_ = new PingWorker(settings_.pingTarget, 53, 1000, 2000, this);
    connect(pingWorker_, &PingWorker::sampleReady, this, &MainWindow::applyPingTelemetry);
    pingWorker_->start();
    networkScannerWorker_ = new NetworkScannerWorker(this);
    connect(networkScannerWorker_, &NetworkScannerWorker::progressChanged, this,
        [this](const int percent, const QString& status)
        {
            networkScanProgress_->setValue(percent);
            networkScanStatus_->setText(status);
        });
    connect(
        networkScannerWorker_, &NetworkScannerWorker::deviceFound, this, &MainWindow::upsertNetworkDevice);
    connect(
        networkScannerWorker_, &NetworkScannerWorker::scanCompleted, this, &MainWindow::finishNetworkScan);
    connect(networkScannerWorker_, &NetworkScannerWorker::scanFailed, this, &MainWindow::failNetworkScan);
    connect(networkScannerWorker_, &QThread::finished, this, &MainWindow::restoreNetworkScanControls);
    refreshNetworkInterfaces();
    internetToolsWorker_ = new InternetToolsWorker(this);
    connect(internetToolsWorker_, &InternetToolsWorker::progressChanged, this,
        [this](const InternetOperation operation, const int percent, const QString& status)
        {
            internetToolsProgress_->setValue(percent);
            internetToolsStatus_->setText(status);
            if (operation == InternetOperation::PublicIpLookup)
            {
                networkPublicIpValue_->setText(status);
                if (diagnosticPublicIpCard_ != nullptr)
                    diagnosticPublicIpCard_->setSubtitle(status);
            }
            else
            {
                networkSpeedTestValue_->setText(status);
            }
        });
    connect(internetToolsWorker_, &InternetToolsWorker::resultReady, this, &MainWindow::applyInternetResult);
    connect(internetToolsWorker_, &QThread::finished, this, &MainWindow::restoreInternetControls);
    if (startOnlineLookup)
    {
        QTimer::singleShot(1200, this,
            [this]
            {
                if (!paused_)
                    startPublicIpLookup();
            });
    }
    processWorker_ = new ProcessWorker(this);
    connect(processWorker_, &ProcessWorker::processesReady, this, &MainWindow::applyProcesses);
    processActionWorker_ = new ProcessActionWorker(this);
    connect(processActionWorker_, &ProcessActionWorker::resultReady, this,
        [this](const QJsonObject& report) {
            processActionStatus_->setText(QStringLiteral("PID %1: %2")
                .arg(report.value("pid").toInteger()).arg(report.value("message").toString()));
        });
    connect(processActionWorker_, &QThread::finished, this, [this] {
        processTerminationPending_ = false;
        updateProcessActions();
        if (!paused_ && processWorker_ != nullptr)
            processWorker_->refresh();
    });
    autostartWorker_ = new AutostartWorker(this);
    connect(autostartWorker_, &AutostartWorker::scanStarted, this,
        [this]
        {
            autostartRefreshButton_->setEnabled(false);
            autostartCountValue_->setText(QStringLiteral("Сканирование…"));
        });
    connect(autostartWorker_, &AutostartWorker::entriesReady, this, &MainWindow::applyAutostartEntries);
    diagnosticWorker_ = new DiagnosticWorker(this);
    deepScanWorker_ = new DeepScanWorker(this);
    fullScanWorker_ = new FullScanWorker(this);
    connect(deepScanWorker_, &DeepScanWorker::reportReady, this,
        [this](const QJsonObject& report)
        {
            latestStressResult_ = report.value(QStringLiteral("stress_test")).toObject();
            if (!latestStressResult_.isEmpty())
                renderStressResult(latestStressResult_);
            applyDiagnosticReport(report);
        });
    connect(deepScanWorker_, &QThread::finished, this,
        [this]
        {
            deepScanButton_->setEnabled(!paused_);
            fullScanButton_->setEnabled(!paused_);
            diagnosticScanButton_->setEnabled(!paused_ && !diagnosticWorker_->isRunning());
            stressStartButton_->setEnabled(!paused_ && !stressWorker_->isRunning());
        });
    connect(fullScanWorker_, &FullScanWorker::reportReady, this,
        [this](const QJsonObject& report)
        {
            latestStressResult_ = report.value(QStringLiteral("stress_test")).toObject();
            latestSpeedTest_ = report.value(QStringLiteral("speed_test")).toObject();
            latestPublicIp_ = report.value(QStringLiteral("public_ip")).toObject();
            if (!latestStressResult_.isEmpty())
                renderStressResult(latestStressResult_);
            if (!latestPublicIp_.isEmpty())
                renderPublicIpCard(latestPublicIp_);
            applyDiagnosticReport(report);
        });
    connect(fullScanWorker_, &QThread::finished, this,
        [this]
        {
            fullScanButton_->setEnabled(!paused_);
            deepScanButton_->setEnabled(!paused_);
            diagnosticScanButton_->setEnabled(!paused_ && !diagnosticWorker_->isRunning());
            stressStartButton_->setEnabled(!paused_ && !stressWorker_->isRunning());
            updateAppMonitorControls();
            restoreInternetControls();
        });
    connect(diagnosticWorker_, &DiagnosticWorker::scanStarted, this,
        [this]
        {
            diagnosticScanButton_->setEnabled(false);
            diagnosticStatusValue_->setText(QStringLiteral("Анализ текущего снимка…"));
            if (diagnosticSmartStatus_ != nullptr)
                diagnosticSmartStatus_->setText(QStringLiteral("⏳ Опрос SMART и журнала ошибок…"));
        });
    connect(diagnosticWorker_, &DiagnosticWorker::reportReady, this, &MainWindow::applyDiagnosticReport);
    hardwareWorker_ = new HardwareInventoryWorker(this);
    connect(hardwareWorker_, &HardwareInventoryWorker::scanStarted, this,
        [this] { hardwareStatus_->setText(QStringLiteral("Сбор характеристик…")); });
    connect(hardwareWorker_, &HardwareInventoryWorker::progressChanged, this,
        [this](const int completed, const int total, const QString& label)
        {
            hardwareProgress_->setRange(0, total);
            hardwareProgress_->setValue(completed);
            hardwareStatus_->setText(QStringLiteral("%1 из %2 · %3").arg(completed).arg(total).arg(label));
        });
    connect(hardwareWorker_, &HardwareInventoryWorker::reportReady, this, &MainWindow::applyHardwareReport);
    connect(hardwareWorker_, &QThread::finished, this,
        [this]
        {
            if (!hardwareScanCompleted_)
            {
                hardwareProgress_->hide();
                hardwareStatus_->setText(paused_
                        ? QStringLiteral("Сбор остановлен: мониторинг приостановлен.")
                        : QStringLiteral("Сбор не завершён. Нажмите «Обновить», чтобы повторить."));
            }
            hardwareRefreshButton_->setEnabled(!paused_);
            hardwareCopyButton_->setEnabled(hardwareLoaded_);
            if (!paused_ && hardwareScanPending_)
                startHardwareScan();
        });
    stressWorker_ = new StressWorker(this);
    connect(stressWorker_, &StressWorker::progressChanged, this,
        [this](const int percent, const QString& status)
        {
            stressProgress_->setValue(percent);
            stressStatus_->setText(status);
        });
    connect(stressWorker_, &StressWorker::testCompleted, this, &MainWindow::applyStressReport);
    appMonitorWorker_ = new AppMonitorWorker(this);
    connect(appMonitorWorker_, &AppMonitorWorker::phaseChanged, this,
        [this](const QString& phase)
        {
            if (appStatusValue_ != nullptr && !paused_)
                appStatusValue_->setText(phase);
        });
    connect(appMonitorWorker_, &AppMonitorWorker::progressChanged, appProgress_, &QProgressBar::setValue);
    connect(appMonitorWorker_, &AppMonitorWorker::sampleReady, this, &MainWindow::applyAppMonitorSample);
    connect(appMonitorWorker_, &AppMonitorWorker::reportReady, this, &MainWindow::applyAppMonitorReport);
    connect(appMonitorWorker_, &QThread::finished, this, &MainWindow::updateAppMonitorControls);
    incidentWorker_ = new IncidentWorker(this);
    connect(incidentWorker_, &IncidentWorker::phaseChanged, this,
        [this](const QString& phase)
        {
            collectionStatus_->setText(phase);
            if (incidentStatusValue_ != nullptr)
                incidentStatusValue_->setText(phase);
            if (diagnosticIncidentCard_ != nullptr)
            {
                diagnosticIncidentCard_->setStatus(orion::core::StatusLevel::Warning);
                diagnosticIncidentCard_->setSubtitle(phase);
            }
        });
    connect(incidentWorker_, &IncidentWorker::captureReady, this, &MainWindow::finalizeIncident);
    connect(incidentWorker_, &QThread::finished, this, [this] {
        const bool enabled = !paused_ && !incidentWorker_->isRunning();
        problemButton_->setEnabled(enabled);
        if (diagnosticIncidentButton_) diagnosticIncidentButton_->setEnabled(enabled);
    });
    connect(tabWidget_, &QTabWidget::currentChanged, this, [this](const int) { handleCurrentTabChanged(); });
    processWorker_->start();
    handleCurrentTabChanged();
}

MainWindow::~MainWindow()
{
    if (startupSequence_ != nullptr)
        startupSequence_->cancel();
    // Startup makes these read-only workers active before the user opens tabs.
    // Never destroy a running thread if a system/provider call outlives its wait.
    const auto waitOrRetain = [](QThread* worker, unsigned long timeout)
    {
        if (worker->wait(timeout))
            return;
        worker->disconnect();
        worker->setParent(nullptr);
        QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    };
    if (fullScanWorker_ != nullptr && fullScanWorker_->isRunning())
    {
        fullScanWorker_->requestStop();
        waitOrRetain(fullScanWorker_, 5000);
    }
    if (deepScanWorker_ != nullptr && deepScanWorker_->isRunning())
    {
        deepScanWorker_->requestStop();
        waitOrRetain(deepScanWorker_, 5000);
    }
    if (gamerOverlay_ != nullptr)
    {
        gamerOverlay_->shutdown();
        delete gamerOverlay_;
        gamerOverlay_ = nullptr;
    }
    if (serverPanel_ != nullptr)
    {
        serverPanel_->shutdown();
    }
    if (terminalPanel_ != nullptr)
    {
        terminalPanel_->terminateShell();
    }
    if (pingWorker_ != nullptr)
    {
        pingWorker_->stop();
        pingWorker_->wait(2000);
    }
    if (networkScannerWorker_ != nullptr && networkScannerWorker_->isRunning())
    {
        networkScannerWorker_->requestStop();
        networkScannerWorker_->wait(3000);
    }
    if (internetToolsWorker_ != nullptr && internetToolsWorker_->isRunning())
    {
        internetToolsWorker_->requestStop();
        waitOrRetain(internetToolsWorker_, 3000);
    }
    if (telemetryWorker_ != nullptr)
    {
        telemetryWorker_->stop();
        waitOrRetain(telemetryWorker_, 3000);
    }
    if (processWorker_ != nullptr)
    {
        processWorker_->stop();
        processWorker_->wait(3000);
    }
    if (processActionWorker_ != nullptr && processActionWorker_->isRunning())
        waitOrRetain(processActionWorker_, 3500);
    if (autostartWorker_ != nullptr && autostartWorker_->isRunning())
    {
        autostartWorker_->stop();
        waitOrRetain(autostartWorker_, 3000);
    }
    if (diagnosticWorker_ != nullptr && diagnosticWorker_->isRunning())
    {
        diagnosticWorker_->stop();
        waitOrRetain(diagnosticWorker_, 3000);
    }
    if (hardwareWorker_ != nullptr && hardwareWorker_->isRunning())
    {
        hardwareWorker_->stop();
        if (!hardwareWorker_->wait(5000))
        {
            // A vendor WMI provider can remain blocked inside COM. Preserve the
            // thread object until it exits instead of destroying a running QThread.
            hardwareWorker_->disconnect(this);
            hardwareWorker_->setParent(nullptr);
            connect(hardwareWorker_, &QThread::finished, hardwareWorker_, &QObject::deleteLater);
            hardwareWorker_ = nullptr;
        }
    }
    if (stressWorker_ != nullptr && stressWorker_->isRunning())
    {
        stressWorker_->requestStop();
        stressWorker_->wait(5000);
    }
    if (appMonitorWorker_ != nullptr && appMonitorWorker_->isRunning())
    {
        appMonitorWorker_->stopObservation();
        waitOrRetain(appMonitorWorker_, 12000);
    }
    if (incidentWorker_ != nullptr && incidentWorker_->isRunning())
    {
        incidentWorker_->cancel();
        waitOrRetain(incidentWorker_, 500);
    }
}

void MainWindow::beginStartupScan(bool includeOnlineLookup)
{
    if (startupSequence_ != nullptr)
        return;
    if (!includeOnlineLookup && latestPublicIp_.isEmpty())
        networkPublicIpValue_->setText(QStringLiteral("не проверялся в этом запуске"));
    startupSequence_ = new StartupSequence(this);
    connect(startupSequence_, &StartupSequence::progressChanged, this, &MainWindow::startupProgress);
    connect(startupSequence_, &StartupSequence::logLine, this, &MainWindow::startupLog);
    connect(startupSequence_, &StartupSequence::finished, this, &MainWindow::startupFinished);
    connect(startupSequence_, &StartupSequence::phaseRequested, this, &MainWindow::requestStartupPhase);
    connect(telemetryWorker_, &TelemetryWorker::sampleReady, this,
        [this]
        {
            if (!paused_)
                startupSequence_->completePhase(StartupSequence::Phase::Telemetry,
                    latestCpuPercent_ < 0 || latestRamPercent_ < 0, QStringLiteral("первый снимок получен"));
        });
    connect(hardwareWorker_, &HardwareInventoryWorker::progressChanged, startupSequence_,
        &StartupSequence::hardwareProgress);
    connect(hardwareWorker_, &HardwareInventoryWorker::reportReady, this,
        [this](const QJsonObject& report)
        {
            if (!paused_)
                startupSequence_->completePhase(StartupSequence::Phase::Hardware,
                    report.value(QStringLiteral("collection_partial")).toBool(), hardwareStatus_->text());
        });
    connect(autostartWorker_, &AutostartWorker::entriesReady, this,
        [this](const QVector<AutostartTelemetry>& entries, const QString& source)
        {
            startupSequence_->completePhase(StartupSequence::Phase::Autostart, source.isEmpty(),
                QStringLiteral("получено записей: %1").arg(entries.size()));
        });
    connect(diagnosticWorker_, &DiagnosticWorker::reportReady, this,
        [this](const QJsonObject& report)
        {
            const auto diagnostics = report.value(QStringLiteral("diagnostics")).toObject();
            const bool partial = !diagnostics.value(QStringLiteral("smart"))
                                      .toObject()
                                      .value(QStringLiteral("available"))
                                      .toBool()
                || diagnostics.value(QStringLiteral("log_errors"))
                        .toObject()
                        .value(QStringLiteral("data_quality"))
                        .toString()
                    != QStringLiteral("valid");
            startupSequence_->completePhase(StartupSequence::Phase::Diagnostics, partial,
                partial ? QStringLiteral("часть системных источников недоступна; подробности в отчёте")
                        : QStringLiteral("отчёт подготовлен; результаты доступны в диагностике"));
        });
    connect(internetToolsWorker_, &InternetToolsWorker::resultReady, this,
        [this](InternetOperation operation, const QJsonObject& result)
        {
            if (operation != InternetOperation::PublicIpLookup)
                return;
            if (paused_ && result.value(QStringLiteral("cancelled")).toBool())
                return;
            startupSequence_->completePhase(StartupSequence::Phase::PublicIp,
                !result.value(QStringLiteral("ok")).toBool(),
                result.value(QStringLiteral("ok")).toBool()
                    ? QStringLiteral("результат сохранён")
                    : result.value(QStringLiteral("error")).toString(QStringLiteral("источник недоступен")));
        });
    // A collector may stop without delivering a report. Advance with an honest
    // warning, unless Pause is holding that phase for a later retry.
    const auto watch = [this](QThread* worker, StartupSequence::Phase phase)
    {
        connect(worker, &QThread::finished, this,
            [this, worker, phase]
            {
                if (!paused_ && !worker->isRunning())
                    startupSequence_->completePhase(
                        phase, true, QStringLiteral("сборщик завершился без результата"));
            });
    };
    watch(hardwareWorker_, StartupSequence::Phase::Hardware);
    watch(autostartWorker_, StartupSequence::Phase::Autostart);
    watch(diagnosticWorker_, StartupSequence::Phase::Diagnostics);
    watch(internetToolsWorker_, StartupSequence::Phase::PublicIp);
    startupSequence_->setPaused(paused_);
    startupSequence_->start(includeOnlineLookup);
}

void MainWindow::requestStartupPhase(StartupSequence::Phase phase)
{
    if (paused_ || quitting_)
        return;
    switch (phase)
    {
    case StartupSequence::Phase::Telemetry:
        if (telemetryReceived_)
            startupSequence_->completePhase(phase, latestCpuPercent_ < 0 || latestRamPercent_ < 0);
        break;
    case StartupSequence::Phase::Hardware:
        if (hardwareLoaded_)
            startupSequence_->completePhase(phase,
                latestHardwareReport_.value(QStringLiteral("collection_partial")).toBool(),
                QStringLiteral("использован готовый отчёт"));
        else
            startHardwareScan();
        break;
    case StartupSequence::Phase::Autostart:
        if (autostartLoaded_)
            startupSequence_->completePhase(phase, false, QStringLiteral("использован готовый список"));
        else
            autostartWorker_->scan();
        break;
    case StartupSequence::Phase::Diagnostics:
        if (diagnosticLoaded_)
        {
            const auto cached = latestDiagnosticReport_.value(QStringLiteral("diagnostics")).toObject();
            const bool partial = !cached.value(QStringLiteral("smart"))
                                      .toObject()
                                      .value(QStringLiteral("available"))
                                      .toBool()
                || cached.value(QStringLiteral("log_errors"))
                        .toObject()
                        .value(QStringLiteral("data_quality"))
                        .toString()
                    != QStringLiteral("valid");
            startupSequence_->completePhase(phase, partial, QStringLiteral("использован готовый отчёт"));
        }
        else
            startDiagnosticScan();
        break;
    case StartupSequence::Phase::PublicIp:
        if (!latestPublicIp_.isEmpty() && !latestPublicIp_.value(QStringLiteral("cancelled")).toBool())
            startupSequence_->completePhase(phase, !latestPublicIp_.value(QStringLiteral("ok")).toBool(),
                QStringLiteral("использован сохранённый результат"));
        else
            startPublicIpLookup();
        break;
    case StartupSequence::Phase::Complete:
        break;
    }
}

void MainWindow::cancelStartupScan()
{
    if (startupSequence_ == nullptr || !startupSequence_->isActive())
        return;
    startupSequence_->cancel();
    hardwareScanPending_ = false;
    hardwareWorker_->stop();
    autostartWorker_->stop();
    diagnosticWorker_->stop();
    internetToolsWorker_->requestStop();
}

QWidget* MainWindow::createOverviewPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("OverviewPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("Обзор системы"), page);
    title->setObjectName(QStringLiteral("Header"));
    errorsBadge_ = new QPushButton(QStringLiteral("Ошибки ОС: ещё не проверены"), page);
    errorsBadge_->setObjectName(QStringLiteral("ErrorsBadge"));
    connect(errorsBadge_, &QPushButton::clicked, this, [this] { openDeepTelemetry(); });
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(errorsBadge_);
    layout->addLayout(header);

    overviewDockHost_ = new QMainWindow(page);
    overviewDockHost_->setObjectName(QStringLiteral("OverviewDockHost"));
    overviewDockHost_->setWindowFlags(Qt::Widget);
    overviewDockHost_->setDockNestingEnabled(true);
    overviewDockHost_->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
    auto* centralStub = new QWidget(overviewDockHost_);
    centralStub->setMaximumSize(0, 0);
    overviewDockHost_->setCentralWidget(centralStub);

    cpuCard_ = new MetricCard({}, false, overviewDockHost_);
    gpuCard_ = new MetricCard({}, false, overviewDockHost_);
    ramCard_ = new MetricCard({}, false, overviewDockHost_);
    diskCard_ = new MetricCard({}, true, overviewDockHost_);
    connect(
        diskCard_, &MetricCard::badgeClicked, this, [this] { tabWidget_->setCurrentWidget(detailsPage_); });
    networkCard_ = new MetricCard({}, true, overviewDockHost_);
    cpuCard_->setGaugeLabel(QStringLiteral("CPU"));
    gpuCard_->setGaugeLabel(QStringLiteral("GPU"));
    ramCard_->setGaugeLabel(QStringLiteral("RAM"));
    cpuCard_->setProperty("metricKey", QStringLiteral("cpu"));
    gpuCard_->setProperty("metricKey", QStringLiteral("gpu"));
    ramCard_->setProperty("metricKey", QStringLiteral("ram"));
    diskCard_->setProperty("metricKey", QStringLiteral("disk"));
    networkCard_->setProperty("metricKey", QStringLiteral("net"));
    gpuCard_->setUnavailable(QStringLiteral("Ожидание первого замера GPU"));
    diskCard_->setUnavailable(QStringLiteral("Ожидание первого замера накопителей"));

    const auto addDock
        = [this](const QString& key, const QString& title, MetricCard* card, const bool visible)
    {
        auto* dock = new IndependentDockWidget(title, overviewDockHost_);
        dock->setObjectName(QStringLiteral("OverviewDock_%1").arg(key));
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        dock->setFeatures(settings_.cardDragDropEnabled
                ? QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                : QDockWidget::NoDockWidgetFeatures);
        dock->setWidget(card);
        dock->setVisible(visible);
        overviewDocks_.append(dock);
        return dock;
    };

    auto* cpuDock = addDock(QStringLiteral("cpu"), QStringLiteral("CPU"), cpuCard_, settings_.cardCpuVisible);
    auto* gpuDock = addDock(QStringLiteral("gpu"), QStringLiteral("GPU"), gpuCard_, settings_.cardGpuVisible);
    auto* ramDock = addDock(QStringLiteral("ram"), QStringLiteral("RAM"), ramCard_, settings_.cardRamVisible);
    auto* diskDock
        = addDock(QStringLiteral("disk"), QStringLiteral("ХРАНИЛИЩЕ"), diskCard_, settings_.cardDiskVisible);
    auto* networkDock
        = addDock(QStringLiteral("net"), QStringLiteral("СЕТЬ"), networkCard_, settings_.cardNetVisible);

    overviewDockHost_->addDockWidget(Qt::TopDockWidgetArea, cpuDock);
    overviewDockHost_->splitDockWidget(cpuDock, networkDock, Qt::Vertical);
    overviewDockHost_->splitDockWidget(cpuDock, gpuDock, Qt::Horizontal);
    overviewDockHost_->splitDockWidget(cpuDock, ramDock, Qt::Vertical);
    overviewDockHost_->splitDockWidget(gpuDock, diskDock, Qt::Vertical);
    if (!settings_.cardDockState.isEmpty())
    {
        overviewDockHost_->restoreState(QByteArray::fromBase64(settings_.cardDockState.toLatin1()), 1);
    }
    auto* cardScroll = new QScrollArea(page);
    cardScroll->setObjectName(QStringLiteral("OverviewScroll"));
    cardScroll->setFrameShape(QFrame::NoFrame);
    cardScroll->setWidgetResizable(true);
    cardScroll->setWidget(overviewDockHost_);
    layout->addWidget(cardScroll, 1);
    return page;
}

QWidget* MainWindow::createDetailsPage()
{
    detailsPanel_ = new DetailsPanel(this);
    cpuCoreChart_ = detailsPanel_->coreChart();
    cpuCoreChart_->setMode(settings_.cpuCoreChartMode);
    connect(cpuCoreChart_, &CpuCoreChartWidget::modeChanged, this,
        [this](const QString& mode)
        {
            settings_.cpuCoreChartMode = mode;
            saveSettings();
        });
    return detailsPanel_;
}

QWidget* MainWindow::createNetworkPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("NetworkPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* splitter = new QSplitter(Qt::Horizontal, page);
    networkSplitter_ = splitter;
    splitter->setObjectName(QStringLiteral("NetworkSplitter"));
    splitter->setChildrenCollapsible(false);

    auto* leftScroll = new QScrollArea(splitter);
    leftScroll->setObjectName(QStringLiteral("NetworkTrafficScroll"));
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    auto* leftPanel = new QWidget(leftScroll);
    leftScroll->setWidget(leftPanel);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(5, 5, 5, 5);
    auto* title = new QLabel(QStringLiteral("NET"), leftPanel);
    title->setObjectName(QStringLiteral("Header"));
    auto* card
        = new DetailCard(QStringLiteral("Текущие показатели"), QStringLiteral("network"), false, leftPanel);
    networkStateCard_ = card;
    card->setSubtitle(QStringLiteral("Трафик и счётчики — по системе"));
    card->setToolTip(
        QStringLiteral("Сумма сетевых интерфейсов сборщика ОС, не только выбранного адаптера. "
                       "Выбор справа меняет адреса и область сканирования, но не фильтрует трафик."));
    const auto field
        = [card](const QString& key, const QString& caption, const QString& value, const QString& objectName)
    {
        auto* label = card->setField(key, caption, value);
        label->setObjectName(objectName);
        label->setProperty("networkValue", true);
        return label;
    };
    networkDownloadValue_ = field(QStringLiteral("download"), QStringLiteral("Скорость загрузки"),
        QStringLiteral("н/д"), QStringLiteral("NetworkDownloadValue"));
    networkUploadValue_ = field(QStringLiteral("upload"), QStringLiteral("Скорость отдачи"),
        QStringLiteral("н/д"), QStringLiteral("NetworkUploadValue"));
    networkReceivedValue_ = field(QStringLiteral("total_recv"), QStringLiteral("Всего получено"),
        QStringLiteral("н/д"), QStringLiteral("NetworkReceivedValue"));
    networkSentValue_ = field(QStringLiteral("total_sent"), QStringLiteral("Всего отправлено"),
        QStringLiteral("н/д"), QStringLiteral("NetworkSentValue"));
    networkPingValue_ = field(QStringLiteral("ping"), QStringLiteral("Пинг"), QStringLiteral("н/д"),
        QStringLiteral("NetworkPingValue"));
    networkErrorsValue_ = field(QStringLiteral("errors"), QStringLiteral("Ошибки"), QStringLiteral("н/д"),
        QStringLiteral("NetworkErrorsValue"));
    networkDropsValue_ = field(QStringLiteral("drops"), QStringLiteral("Потери пакетов"),
        QStringLiteral("н/д"), QStringLiteral("NetworkDropsValue"));
    networkLocalIpValue_ = field(QStringLiteral("local_ip"), QStringLiteral("Локальный IP"),
        QStringLiteral("определяется…"), QStringLiteral("NetworkLocalIpValue"));
    networkMacValue_ = field(QStringLiteral("mac"), QStringLiteral("MAC-адрес"),
        QStringLiteral("определяется…"), QStringLiteral("NetworkMacValue"));
    networkPublicIpValue_ = field(QStringLiteral("public_ip"), QStringLiteral("Публичный IP / провайдер"),
        QStringLiteral("проверяется при запуске…"), QStringLiteral("NetworkPublicIpValue"));
    networkSpeedTestValue_ = field(QStringLiteral("speed_test"), QStringLiteral("Тест скорости интернета"),
        QStringLiteral("не запускался"), QStringLiteral("NetworkSpeedTestValue"));
    leftLayout->addWidget(title);
    networkTrafficChart_ = new NetworkTrafficChart(leftPanel);
    leftLayout->addWidget(networkTrafficChart_, 1);
    leftLayout->addWidget(card);
    leftLayout->addStretch(0);

    auto* scannerPanel = new QFrame(splitter);
    scannerPanel->setObjectName(QStringLiteral("NetworkScannerPanel"));
    auto* scannerLayout = new QVBoxLayout(scannerPanel);
    scannerLayout->setContentsMargins(5, 5, 5, 5);
    scannerLayout->setSpacing(8);

    auto* scannerHeader = new QHBoxLayout;
    auto* scannerTitle = new QLabel(QStringLiteral("Устройства в сети"), scannerPanel);
    scannerTitle->setObjectName(QStringLiteral("Header"));
    scannerTitle->setWordWrap(true);
    auto* resetPanel = new QToolButton(scannerPanel);
    resetPanel->setObjectName(QStringLiteral("NetworkPanelReset"));
    resetPanel->setText(QStringLiteral("✕"));
    resetPanel->setToolTip(QStringLiteral("Сбросить размер этой панели"));
    resetPanel->setFixedSize(22, 22);
    connect(resetPanel, &QToolButton::clicked, splitter, [splitter] { splitter->setSizes({ 360, 470 }); });
    networkRefreshButton_ = new QToolButton(scannerPanel);
    networkRefreshButton_->setObjectName(QStringLiteral("NetworkInterfaceRefresh"));
    networkRefreshButton_->setText(QStringLiteral("↻"));
    networkRefreshButton_->setToolTip(QStringLiteral("Обновить список активных IPv4-адаптеров"));
    networkRefreshButton_->setFixedSize(28, 28);
    scannerHeader->addWidget(scannerTitle);
    scannerHeader->addStretch(1);
    scannerHeader->addWidget(resetPanel);
    scannerLayout->addLayout(scannerHeader);

    auto* interfaceRow = new QHBoxLayout;
    auto* interfaceLabel = new QLabel(QStringLiteral("Адаптер:"), scannerPanel);
    interfaceLabel->setObjectName(QStringLiteral("FieldLabel"));
    networkInterfaceCombo_ = new QComboBox(scannerPanel);
    networkInterfaceCombo_->setObjectName(QStringLiteral("NetworkInterfaceCombo"));
    networkInterfaceCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    networkInterfaceCombo_->setMinimumContentsLength(8);
    interfaceRow->addWidget(interfaceLabel);
    interfaceRow->addWidget(networkInterfaceCombo_, 1);
    interfaceRow->addWidget(networkRefreshButton_);
    scannerLayout->addLayout(interfaceRow);
    networkScopeValue_ = new QLabel(QStringLiteral("Подсеть: определяется…"), scannerPanel);
    networkScopeValue_->setObjectName(QStringLiteral("FieldLabel"));
    networkScopeValue_->setWordWrap(true);
    scannerLayout->addWidget(networkScopeValue_);

    networkScanButton_ = new QPushButton(QStringLiteral("Сканировать сеть"), scannerPanel);
    networkScanButton_->setObjectName(QStringLiteral("NetworkScanButton"));
    networkScanProgress_ = new QProgressBar(scannerPanel);
    networkScanProgress_->setObjectName(QStringLiteral("NetworkScanProgress"));
    networkScanProgress_->setRange(0, 100);
    networkScanProgress_->setValue(0);
    networkScanProgress_->hide();
    networkScanStatus_ = new QLabel(QStringLiteral("Поиск активного IPv4-адаптера…"), scannerPanel);
    networkScanStatus_->setObjectName(QStringLiteral("NetworkScanStatus"));
    networkScanStatus_->setWordWrap(true);
    scannerLayout->addWidget(networkScanButton_);
    scannerLayout->addWidget(networkScanProgress_);
    scannerLayout->addWidget(networkScanStatus_);

    networkDeviceTable_ = new QTableWidget(scannerPanel);
    networkDeviceTable_->setObjectName(QStringLiteral("NetworkDeviceTable"));
    networkDeviceTable_->setColumnCount(5);
    networkDeviceTable_->setHorizontalHeaderLabels({ QStringLiteral("IP"), QStringLiteral("MAC"),
        QStringLiteral("Имя"), QStringLiteral("Тип /\nпроизводитель"), QStringLiteral("Задержка") });
    networkDeviceTable_->horizontalHeaderItem(3)->setToolTip(QStringLiteral("Тип / производитель"));
    networkDeviceTable_->verticalHeader()->hide();
    networkDeviceTable_->verticalHeader()->setDefaultSectionSize(26);
    networkDeviceTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    networkDeviceTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    networkDeviceTable_->setAlternatingRowColors(true);
    networkDeviceTable_->setWordWrap(false);
    networkDeviceTable_->setFrameShape(QFrame::NoFrame);
    networkDeviceTable_->horizontalHeader()->setMinimumSectionSize(70);
    networkDeviceTable_->horizontalHeader()->setFixedHeight(35);
    networkDeviceTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    networkDeviceTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    networkDeviceTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    networkDeviceTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    networkDeviceTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    scannerLayout->addWidget(networkDeviceTable_, 1);

    connect(networkRefreshButton_, &QToolButton::clicked, this, &MainWindow::refreshNetworkInterfaces);
    connect(networkInterfaceCombo_, &QComboBox::currentIndexChanged, this,
        [this](const int) { updateSelectedNetworkInterface(); });
    connect(networkScanButton_, &QPushButton::clicked, this, &MainWindow::toggleNetworkScan);

    splitter->addWidget(leftScroll);
    splitter->addWidget(scannerPanel);
    leftScroll->setMinimumWidth(210);
    scannerPanel->setMinimumWidth(420);
    splitter->setSizes({ 360, 470 });
    layout->addWidget(splitter, 1);
    return page;
}

QWidget* MainWindow::createTaskManagerPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 12, 10, 10);
    layout->setSpacing(10);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("Диспетчер задач"), page);
    title->setObjectName(QStringLiteral("Header"));
    processCountValue_ = makeValueLabel(QStringLiteral("Откройте вкладку для запуска сбора"), page);
    processCountValue_->setWordWrap(true);
    processShowAllButton_ = new QPushButton(QStringLiteral("Показать все процессы"), page);
    processShowAllButton_->setToolTip(
        QStringLiteral("По умолчанию отображаются 15 процессов с наибольшей нагрузкой CPU."));
    processRefreshButton_ = new QPushButton(QStringLiteral("Обновить сейчас"), page);
    processRefreshButton_->setObjectName(QStringLiteral("ProcessRefresh"));
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(processShowAllButton_);
    header->addWidget(processRefreshButton_);

    auto* filter = new QHBoxLayout;
    auto* searchLabel = new QLabel(QStringLiteral("Поиск:"), page);
    searchLabel->setObjectName(QStringLiteral("FieldLabel"));
    processSearch_ = new QLineEdit(page);
    processSearch_->setObjectName(QStringLiteral("ProcessSearch"));
    processSearch_->setPlaceholderText(QStringLiteral("Имя или PID процесса…"));
    processSearch_->setClearButtonEnabled(true);
    processSourceValue_ = makeValueLabel(QStringLiteral("Источник: ожидание"), page);
    processSourceValue_->setWordWrap(true);
    processTerminateButton_ = new QPushButton(QStringLiteral("Завершить процесс"), page);
    processTerminateButton_->setObjectName(QStringLiteral("ProcessTerminate"));
    processTerminateButton_->setEnabled(false);
    processTerminateButton_->setToolTip(QStringLiteral(
        "Выберите процесс. Потребуется подтверждение; несохранённые данные могут быть потеряны."));
    processActionStatus_ = new QLabel(page);
    processActionStatus_->setObjectName(QStringLiteral("ProcessActionStatus"));
    processActionStatus_->setWordWrap(true);
    filter->addWidget(searchLabel);
    filter->addWidget(processSearch_, 1);
    filter->addWidget(processTerminateButton_);

    processTable_ = new QTableWidget(0, 7, page);
    processTable_->setObjectName(QStringLiteral("ProcessTable"));
    processTable_->setHorizontalHeaderLabels({
        QStringLiteral("Процесс"),
        QStringLiteral("PID"),
        QStringLiteral("CPU %"),
        QStringLiteral("RAM (МБ)"),
        QStringLiteral("Private (МБ)"),
        QStringLiteral("Диск (МБ)"),
        QStringLiteral("Потоки"),
    });
    processTable_->verticalHeader()->hide();
    processTable_->verticalHeader()->setDefaultSectionSize(26);
    processTable_->horizontalHeaderItem(5)->setToolTip(
        QStringLiteral("Суммарные байты ввода-вывода за время жизни процесса, в МБ. Это не скорость МБ/с."));
    processTable_->horizontalHeader()->setFixedHeight(34);
    processTable_->horizontalHeader()->setMinimumSectionSize(82);
    processTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < processTable_->columnCount(); ++column)
    {
        processTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    processTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    processTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    processTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    processTable_->setAlternatingRowColors(true);
    processTable_->setSortingEnabled(true);
    processTable_->sortItems(2, Qt::DescendingOrder);

    connect(processShowAllButton_, &QPushButton::clicked, this,
        [this]
        {
            processShowAll_ = !processShowAll_;
            processShowAllButton_->setText(processShowAll_ ? QStringLiteral("Показывать TOP-15")
                                                           : QStringLiteral("Показать все процессы"));
            renderProcessTable();
        });
    connect(processRefreshButton_, &QPushButton::clicked, this,
        [this]
        {
            if (!paused_ && processWorker_ != nullptr)
            {
                processWorker_->refresh();
            }
        });
    connect(processSearch_, &QLineEdit::textChanged, this, [this] { renderProcessTable(); });
    connect(processTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateProcessActions);
    connect(processTerminateButton_, &QPushButton::clicked, this, &MainWindow::terminateSelectedProcess);

    layout->addLayout(header);
    layout->addLayout(filter);
    layout->addWidget(processCountValue_);
    layout->addWidget(processSourceValue_);
    layout->addWidget(processActionStatus_);
    layout->addWidget(processTable_, 1);
    return page;
}

QWidget* MainWindow::createAutostartPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 12, 10, 10);
    layout->setSpacing(10);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("Автозагрузка"), page);
    title->setObjectName(QStringLiteral("Header"));
    autostartCountValue_ = makeValueLabel(QStringLiteral("Откройте вкладку для чтения списка"), page);
    autostartCountValue_->setWordWrap(true);
    autostartRefreshButton_ = new QPushButton(QStringLiteral("Обновить список"), page);
    autostartRefreshButton_->setObjectName(QStringLiteral("AutostartRefresh"));
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(autostartRefreshButton_);

    auto* note
        = new QLabel(QStringLiteral("Только просмотр. O.R.I.O.N. ничего не отключает и не удаляет. "
                                    "«Система» — компоненты ОС и рабочего стола; «Пользователь» — записи, "
                                    "которые стоит проверить при долгом запуске системы."),
            page);
    note->setObjectName(QStringLiteral("CardDetail"));
    note->setWordWrap(true);

    auto* filters = new QHBoxLayout;
    auto* searchLabel = new QLabel(QStringLiteral("Поиск:"), page);
    searchLabel->setObjectName(QStringLiteral("FieldLabel"));
    autostartSearch_ = new QLineEdit(page);
    autostartSearch_->setObjectName(QStringLiteral("AutostartSearch"));
    autostartSearch_->setPlaceholderText(QStringLiteral("Имя, команда или источник…"));
    autostartSearch_->setClearButtonEnabled(true);
    autostartCategory_ = new QComboBox(page);
    autostartCategory_->setObjectName(QStringLiteral("AutostartCategory"));
    autostartCategory_->addItem(QStringLiteral("Все источники"), QStringLiteral("all"));
    autostartCategory_->addItem(QStringLiteral("Пользователь"), QStringLiteral("user"));
    autostartCategory_->addItem(QStringLiteral("Система"), QStringLiteral("system"));
    autostartSourceValue_ = makeValueLabel(QStringLiteral("Источник: ожидание"), page);
    autostartSourceValue_->setWordWrap(true);
    filters->addWidget(searchLabel);
    filters->addWidget(autostartSearch_, 1);
    filters->addWidget(autostartCategory_);

    autostartTable_ = new QTableWidget(0, 5, page);
    autostartTable_->setObjectName(QStringLiteral("AutostartTable"));
    autostartTable_->setHorizontalHeaderLabels({
        QStringLiteral("Имя"),
        QStringLiteral("Источник"),
        QStringLiteral("Категория"),
        QStringLiteral("Статус"),
        QStringLiteral("Команда / путь"),
    });
    autostartTable_->verticalHeader()->hide();
    autostartTable_->verticalHeader()->setDefaultSectionSize(26);
    autostartTable_->horizontalHeader()->setFixedHeight(34);
    autostartTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    autostartTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    autostartTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    autostartTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    autostartTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    autostartTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    autostartTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    autostartTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    autostartTable_->setAlternatingRowColors(true);
    autostartTable_->setSortingEnabled(true);
    autostartTable_->sortItems(0, Qt::AscendingOrder);

    connect(autostartRefreshButton_, &QPushButton::clicked, this,
        [this]
        {
            if (!paused_ && autostartWorker_ != nullptr)
            {
                autostartWorker_->scan();
            }
        });
    connect(autostartSearch_, &QLineEdit::textChanged, this, &MainWindow::renderAutostartTable);
    connect(autostartCategory_, &QComboBox::currentIndexChanged, this, &MainWindow::renderAutostartTable);

    layout->addLayout(header);
    layout->addWidget(note);
    layout->addLayout(filters);
    layout->addWidget(autostartCountValue_);
    layout->addWidget(autostartSourceValue_);
    layout->addWidget(autostartTable_, 1);
    return page;
}

QWidget* MainWindow::createDiagnosticsPage()
{
    diagnosticDockHost_ = new QMainWindow(this);
    diagnosticDockHost_->setObjectName(QStringLiteral("DiagnosticsHubHost"));
    diagnosticDockHost_->setWindowFlags(Qt::Widget);
    diagnosticDockHost_->setDockNestingEnabled(true);
    diagnosticDockHost_->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
    auto* centralStub = new QWidget(diagnosticDockHost_);
    centralStub->setMaximumSize(0, 0);
    diagnosticDockHost_->setCentralWidget(centralStub);

    auto* diagnosticsPanel = new QFrame(diagnosticDockHost_);
    diagnosticsPanel->setObjectName(QStringLiteral("MetricCard"));
    auto* diagnosticsLayout = new QVBoxLayout(diagnosticsPanel);
    diagnosticsLayout->setContentsMargins(10, 10, 10, 10);
    auto* diagnosticsHeader = new QHBoxLayout;
    auto* diagnosticsTitle
        = new QLabel(QStringLiteral("Диагностика: обнаруженные проблемы"), diagnosticsPanel);
    diagnosticsTitle->setObjectName(QStringLiteral("Header"));
    diagnosticsTitle->setWordWrap(true);
    diagnosticScanButton_ = new QPushButton(QStringLiteral("Собрать диагностику"), diagnosticsPanel);
    diagnosticScanButton_->setObjectName(QStringLiteral("DiagnosticScanButton"));
    diagnosticScanButton_->setToolTip(QStringLiteral(
        "Обновить SMART, журнал ОС и анализ текущих данных без нагрузки и интернет-запросов."));
    diagnosticsHeader->addWidget(diagnosticsTitle, 1);
    auto* internetActions = new QGridLayout;
    deepScanButton_ = new QPushButton(QStringLiteral("Глубокая диагностика"), diagnosticsPanel);
    deepScanButton_->setObjectName(QStringLiteral("DeepScanButton"));
    deepScanButton_->setToolTip(QStringLiteral(
        "Свежие характеристики ПК, SMART, журнал и автозагрузка; "
        "после подтверждения — CPU и GPU по 30 секунд, затем временный файл диска 200 МБ. Без интернета."));
    internetActions->addWidget(deepScanButton_, 0, 0);
    fullScanButton_ = new QPushButton(QStringLiteral("Полная проверка"), diagnosticsPanel);
    fullScanButton_->setObjectName(QStringLiteral("FullScanButton"));
    fullScanButton_->setToolTip(QStringLiteral(
        "Глубокая локальная диагностика плюс антивирус, ручной сетевой тест 10 МБ загрузки / "
        "5 МБ отдачи, публичный IP и ориентировочные оценки ПК и интернета."));
    internetActions->addWidget(fullScanButton_, 0, 1);
    internetActions->addWidget(diagnosticScanButton_, 1, 0, 1, 2);
    internetActions->setColumnStretch(0, 1);
    internetActions->setColumnStretch(1, 1);
    publicIpRefreshButton_ = new QPushButton(QStringLiteral("Обновить IP / провайдера"), diagnosticsPanel);
    publicIpRefreshButton_->setObjectName(QStringLiteral("PublicIpRefreshButton"));
    publicIpRefreshButton_->setToolTip(
        QStringLiteral("Разовый запрос к ipinfo.io; при ошибке используется ip-api.com. "
                       "Результат также показывается на вкладке «Сети»."));
    speedTestButton_ = new QPushButton(QStringLiteral("Тест скорости интернета"), diagnosticsPanel);
    speedTestButton_->setObjectName(QStringLiteral("InternetSpeedTestButton"));
    speedTestButton_->setToolTip(
        QStringLiteral("Только ручной однопоточный замер через Cloudflare Edge: "
                       "примерно 10 МБ загрузки и 5 МБ отдачи. Результат не равен тарифной гарантии."));
    internetActions->addWidget(publicIpRefreshButton_, 2, 1);
    internetActions->addWidget(speedTestButton_, 2, 0);
    diagnosticIncidentButton_
        = new QPushButton(QStringLiteral("⚡ Проблема произошла сейчас"), diagnosticsPanel);
    diagnosticIncidentButton_->setObjectName(QStringLiteral("DangerButton"));
    diagnosticIncidentButton_->setToolTip(
        QStringLiteral("Нажмите сразу после фриза, вылета, просадки FPS или сетевого сбоя. "
                       "O.R.I.O.N. сохранит 60 секунд телеметрии до метки и ещё 15 секунд после."));
    internetActions->addWidget(diagnosticIncidentButton_, 3, 0, 1, 2);
    internetToolsProgress_ = new QProgressBar(diagnosticsPanel);
    internetToolsProgress_->setObjectName(QStringLiteral("InternetToolsProgress"));
    internetToolsProgress_->setRange(0, 100);
    internetToolsProgress_->hide();
    internetToolsStatus_
        = new QLabel(QStringLiteral("Интернет-проверки: тест скорости не запускался"), diagnosticsPanel);
    internetToolsStatus_->setObjectName(QStringLiteral("InternetToolsStatus"));
    internetToolsStatus_->setWordWrap(true);
    diagnosticStatusValue_
        = makeValueLabel(QStringLiteral("Откройте вкладку для первого анализа"), diagnosticsPanel);
    diagnosticStatusValue_->setObjectName(QStringLiteral("DiagnosticStatus"));
    diagnosticCoverageValue_
        = new QLabel(QStringLiteral("UNKNOWN не считается нормой: пробелы данных показываются отдельно."),
            diagnosticsPanel);
    diagnosticCoverageValue_->setObjectName(QStringLiteral("CardDetail"));
    diagnosticCoverageValue_->setWordWrap(true);

    const auto sectionHeading = [diagnosticsPanel](const QString& text, const QString& name)
    {
        auto* label = new QLabel(text, diagnosticsPanel);
        label->setObjectName(QStringLiteral("SectionHeading"));
        label->setProperty("diagnosticHeading", name);
        label->setWordWrap(true);
        return label;
    };

    diagnosticIncidentCard_ = new DetailCard(
        QStringLiteral("Runtime-корреляция"), QStringLiteral("incident"), true, diagnosticsPanel);
    markDiagnosticCard(diagnosticIncidentCard_, QStringLiteral("incident"));
    diagnosticIncidentCard_->setStatus(orion::core::StatusLevel::Unknown);
    diagnosticIncidentCard_->setSubtitle(
        QStringLiteral("После фриза, вылета или сетевого сбоя нажмите кнопку выше: будет сопоставлена "
                       "накопленная телеметрия до события и короткое окно после него."));
    incidentStatusValue_ = diagnosticIncidentCard_->setField(
        QStringLiteral("state"), QStringLiteral("Состояние"), QStringLiteral("событие ещё не отмечалось"));

    diagnosticCpuTemperatureCard_ = new DetailCard(
        QStringLiteral("CPU — температура"), QStringLiteral("cpu_temperature"), true, diagnosticsPanel);
    markDiagnosticCard(diagnosticCpuTemperatureCard_, QStringLiteral("cpu_temperature"));
    diagnosticGpuTemperatureCard_ = new DetailCard(
        QStringLiteral("GPU — температура"), QStringLiteral("gpu_temperature"), true, diagnosticsPanel);
    markDiagnosticCard(diagnosticGpuTemperatureCard_, QStringLiteral("gpu_temperature"));
    updateDiagnosticTemperatureCards(-1.0, -1.0);

    diagnosticSmartStatus_ = new QLabel(QStringLiteral("SMART ещё не проверен."), diagnosticsPanel);
    diagnosticSmartStatus_->setObjectName(QStringLiteral("DiagnosticSmartStatus"));
    diagnosticSmartStatus_->setWordWrap(true);
    diagnosticSmartCardsHost_ = new QWidget(diagnosticsPanel);
    diagnosticSmartCardsHost_->setObjectName(QStringLiteral("DiagnosticSmartCardsHost"));
    diagnosticSmartCardsLayout_ = new QVBoxLayout(diagnosticSmartCardsHost_);
    diagnosticSmartCardsLayout_->setContentsMargins(0, 0, 0, 0);
    diagnosticSmartCardsLayout_->setSpacing(8);

    diagnosticLogErrorsCard_ = new DetailCard(
        QStringLiteral("Журнал ошибок ОС"), QStringLiteral("system_errors"), false, diagnosticsPanel);
    markDiagnosticCard(diagnosticLogErrorsCard_, QStringLiteral("system_errors"));
    diagnosticLogErrorsCard_->setSubtitle(QStringLiteral("Журнал ещё не проверен."));
    diagnosticLogErrorsText_ = new QTextEdit(diagnosticLogErrorsCard_);
    diagnosticLogErrorsText_->setObjectName(QStringLiteral("DiagnosticLogErrorsText"));
    diagnosticLogErrorsText_->setReadOnly(true);
    diagnosticLogErrorsText_->setAcceptRichText(false);
    diagnosticLogErrorsText_->setLineWrapMode(QTextEdit::WidgetWidth);
    diagnosticLogErrorsText_->setMinimumHeight(145);
    diagnosticLogErrorsText_->setMaximumHeight(190);
    diagnosticLogErrorsText_->setPlainText(QStringLiteral("Ожидание первого анализа…"));
    diagnosticLogErrorsCard_->addContent(diagnosticLogErrorsText_);

    diagnosticPublicIpCard_ = new DetailCard(
        QStringLiteral("Результат последней проверки"), QStringLiteral("public_ip"), false, diagnosticsPanel);
    markDiagnosticCard(diagnosticPublicIpCard_, QStringLiteral("public_ip"));
    diagnosticPublicIpCard_->setSubtitle(QStringLiteral("Проверяется при запуске приложения…"));
    diagnosticPublicIpCard_->setField(
        QStringLiteral("public_ip"), QStringLiteral("Публичный IP"), QStringLiteral("н/д"));
    diagnosticPublicIpCard_->setField(
        QStringLiteral("provider"), QStringLiteral("Провайдер"), QStringLiteral("н/д"));
    diagnosticPublicIpCard_->setField(
        QStringLiteral("location"), QStringLiteral("Примерное местоположение"), QStringLiteral("н/д"));

    diagnosticTable_ = new QTableWidget(0, 4, diagnosticsPanel);
    diagnosticTable_->setObjectName(QStringLiteral("DiagnosticFindingsTable"));
    diagnosticTable_->setHorizontalHeaderLabels({
        QStringLiteral("Уровень"),
        QStringLiteral("Область"),
        QStringLiteral("Находка"),
        QStringLiteral("Уверенность"),
    });
    diagnosticTable_->verticalHeader()->hide();
    diagnosticTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    diagnosticTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    diagnosticTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    diagnosticTable_->setColumnWidth(2, 220);
    diagnosticTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    diagnosticTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    diagnosticTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    diagnosticTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    diagnosticTable_->setAlternatingRowColors(true);
    diagnosticDetails_ = new QTextEdit(diagnosticsPanel);
    diagnosticDetails_->setObjectName(QStringLiteral("DiagnosticDetails"));
    diagnosticDetails_->setReadOnly(true);
    diagnosticDetails_->setMinimumHeight(115);
    diagnosticDetails_->setPlaceholderText(
        QStringLiteral("Выберите находку, чтобы увидеть доказательства и действия."));
    diagnosticsLayout->addLayout(diagnosticsHeader);
    diagnosticsLayout->addLayout(internetActions);
    diagnosticsLayout->addWidget(internetToolsProgress_);
    diagnosticsLayout->addWidget(internetToolsStatus_);
    diagnosticsLayout->addWidget(diagnosticStatusValue_);
    diagnosticsLayout->addWidget(diagnosticCoverageValue_);

    diagnosticsLayout->addWidget(
        sectionHeading(QStringLiteral("Момент проблемы"), QStringLiteral("incident")));
    diagnosticsLayout->addWidget(diagnosticIncidentCard_);
    diagnosticsLayout->addWidget(
        sectionHeading(QStringLiteral("Температурные аномалии"), QStringLiteral("temperature")));
    diagnosticsLayout->addWidget(diagnosticCpuTemperatureCard_);
    diagnosticsLayout->addWidget(diagnosticGpuTemperatureCard_);
    diagnosticsLayout->addWidget(sectionHeading(QStringLiteral("SMART диска"), QStringLiteral("smart")));
    diagnosticsLayout->addWidget(diagnosticSmartStatus_);
    diagnosticsLayout->addWidget(diagnosticSmartCardsHost_);
    diagnosticsLayout->addWidget(sectionHeading(
        QStringLiteral("Ошибки в логах (с момента загрузки)"), QStringLiteral("system_errors")));
    diagnosticsLayout->addWidget(diagnosticLogErrorsCard_);
    diagnosticsLayout->addWidget(
        sectionHeading(QStringLiteral("IP и провайдер"), QStringLiteral("public_ip")));
    diagnosticsLayout->addWidget(diagnosticPublicIpCard_);
    diagnosticsLayout->addWidget(
        sectionHeading(QStringLiteral("Сводка находок"), QStringLiteral("findings")));
    diagnosticsLayout->addWidget(diagnosticTable_, 1);
    diagnosticsLayout->addWidget(diagnosticDetails_);

    auto* stressPanel = new QFrame(diagnosticDockHost_);
    stressPanel->setObjectName(QStringLiteral("MetricCard"));
    auto* stressLayout = new QVBoxLayout(stressPanel);
    stressLayout->setContentsMargins(10, 10, 10, 10);
    auto* stressTitle = new QLabel(QStringLiteral("Стресс-тест"), stressPanel);
    stressTitle->setObjectName(QStringLiteral("Header"));
    auto* stressText = new QLabel(
        QStringLiteral(
            "⚠ Тест активно нагружает выбранные компоненты последовательно. "
            "CPU использует все логические ядра; GPU выполняет аппаратный Direct3D 11 compute shader; "
            "диск записывает, синхронизирует, читает и удаляет временный файл."),
        stressPanel);
    stressText->setObjectName(QStringLiteral("CardDetail"));
    stressText->setWordWrap(true);
    auto* stressOptions = new QGridLayout;
    stressCpuCheck_ = new QCheckBox(QStringLiteral("CPU"), stressPanel);
    stressCpuCheck_->setChecked(true);
    stressGpuCheck_ = new QCheckBox(QStringLiteral("GPU"), stressPanel);
    stressDiskCheck_ = new QCheckBox(QStringLiteral("Диск"), stressPanel);
    stressDuration_ = new QComboBox(stressPanel);
    for (const int seconds : { 30, 60, 120, 180 })
    {
        stressDuration_->addItem(QStringLiteral("%1 сек").arg(seconds), seconds);
    }
    stressDiskSize_ = new QComboBox(stressPanel);
    for (const int size : { 50, 100, 200, 500 })
    {
        stressDiskSize_->addItem(QStringLiteral("%1 МБ").arg(size), size);
    }
    stressDiskSize_->setCurrentIndex(2);
    stressOptions->addWidget(stressCpuCheck_, 0, 0);
    stressOptions->addWidget(new QLabel(QStringLiteral("Длительность:"), stressPanel), 0, 1);
    stressOptions->addWidget(stressDuration_, 0, 2);
    stressOptions->addWidget(stressGpuCheck_, 1, 0);
    stressOptions->addWidget(new QLabel(QStringLiteral("D3D11, после CPU"), stressPanel), 1, 1, 1, 2);
    stressOptions->addWidget(stressDiskCheck_, 2, 0);
    stressOptions->addWidget(new QLabel(QStringLiteral("Объём:"), stressPanel), 2, 1);
    stressOptions->addWidget(stressDiskSize_, 2, 2);
    stressGpuConfirm_ = new QCheckBox(QStringLiteral("Разрешаю интенсивную нагрузку на GPU"), stressPanel);
    stressDiskConfirm_ = new QCheckBox(QStringLiteral("Разрешаю запись временного файла"), stressPanel);
    stressGpuConfirm_->hide();
    stressDiskConfirm_->hide();
    auto* stressButtons = new QGridLayout;
    stressStartButton_ = new QPushButton(QStringLiteral("Запустить выбранные тесты"), stressPanel);
    stressStartButton_->setObjectName(QStringLiteral("PrimaryButton"));
    stressStopButton_ = new QPushButton(QStringLiteral("Экстренная остановка"), stressPanel);
    stressStopButton_->setObjectName(QStringLiteral("DangerButton"));
    stressStopButton_->setEnabled(false);
    stressButtons->addWidget(stressStartButton_, 0, 0);
    stressButtons->addWidget(stressStopButton_, 0, 1);
    stressProgress_ = new QProgressBar(stressPanel);
    stressProgress_->setObjectName(QStringLiteral("StressProgress"));
    stressProgress_->setRange(0, 100);
    stressProgress_->hide();
    stressStatus_ = makeValueLabel(QStringLiteral("Тест ещё не запускался"), stressPanel);
    stressResult_ = new QTextEdit(stressPanel);
    stressResult_->setObjectName(QStringLiteral("StressResult"));
    stressResult_->setReadOnly(true);
    stressResult_->setMaximumHeight(155);
    stressResult_->hide();
    stressLayout->addWidget(stressTitle);
    stressLayout->addWidget(stressText);
    stressLayout->addLayout(stressOptions);
    stressLayout->addWidget(stressGpuConfirm_);
    stressLayout->addWidget(stressDiskConfirm_);
    stressLayout->addLayout(stressButtons);
    stressLayout->addWidget(stressProgress_);
    stressLayout->addWidget(stressStatus_);
    stressLayout->addWidget(stressResult_);
    stressLayout->addStretch(1);

    connect(stressGpuCheck_, &QCheckBox::toggled, this,
        [this](const bool checked)
        {
            stressGpuConfirm_->setVisible(checked);
            if (!checked)
                stressGpuConfirm_->setChecked(false);
        });
    connect(stressDiskCheck_, &QCheckBox::toggled, this,
        [this](const bool checked)
        {
            stressDiskConfirm_->setVisible(checked);
            if (!checked)
                stressDiskConfirm_->setChecked(false);
        });
    connect(stressStartButton_, &QPushButton::clicked, this, &MainWindow::startStressTest);
    connect(stressStopButton_, &QPushButton::clicked, this,
        [this]
        {
            if (stressWorker_ != nullptr && stressWorker_->isRunning())
            {
                stressStopButton_->setEnabled(false);
                stressStatus_->setText(QStringLiteral("Останавливаю тест…"));
                stressWorker_->requestStop();
            }
        });

    auto* reportPanel = new QFrame(diagnosticDockHost_);
    reportPanel->setObjectName(QStringLiteral("MetricCard"));
    auto* reportLayout = new QVBoxLayout(reportPanel);
    reportLayout->setContentsMargins(10, 10, 10, 10);
    auto* reportTitle = new QLabel(QStringLiteral("Полный отчёт · schema v3"), reportPanel);
    reportTitle->setObjectName(QStringLiteral("Header"));
    auto* reportButtons = new QGridLayout;
    diagnosticSaveJsonButton_ = new QPushButton(QStringLiteral("Сохранить JSON"), reportPanel);
    diagnosticSaveTextButton_ = new QPushButton(QStringLiteral("Сохранить TXT"), reportPanel);
    diagnosticCopyButton_ = new QPushButton(QStringLiteral("📋 Скопировать"), reportPanel);
    diagnosticSaveJsonButton_->setEnabled(false);
    diagnosticSaveTextButton_->setEnabled(false);
    diagnosticCopyButton_->setEnabled(false);
    reportButtons->addWidget(diagnosticSaveJsonButton_, 0, 0);
    reportButtons->addWidget(diagnosticSaveTextButton_, 0, 1);
    reportButtons->addWidget(diagnosticCopyButton_, 1, 0, 1, 2);
    diagnosticReportPreview_ = new QTextEdit(reportPanel);
    diagnosticReportPreview_->setObjectName(QStringLiteral("DiagnosticReportPreview"));
    diagnosticReportPreview_->setReadOnly(true);
    diagnosticReportPreview_->setAcceptRichText(false);
    diagnosticReportPreview_->setLineWrapMode(QTextEdit::WidgetWidth);
    diagnosticReportPreview_->setToolTip(QStringLiteral(
        "Полная версия отчёта, совпадающая с экспортом TXT. UNKNOWN означает отсутствие надёжных данных."));
    diagnosticReportPreview_->setFontFamily(QStringLiteral("Consolas"));
    diagnosticReportPreview_->setPlainText(QStringLiteral("(пока нет данных)"));
    reportLayout->addWidget(reportTitle);
    reportLayout->addLayout(reportButtons);
    reportLayout->addWidget(diagnosticReportPreview_, 1);

    const auto addDock = [this](const QString& key, const QString& title, QWidget* widget)
    {
        auto* dock = new IndependentDockWidget(title, diagnosticDockHost_);
        dock->setObjectName(QStringLiteral("DiagnosticsHubDock_%1").arg(key));
        auto* scroll = new QScrollArea(dock);
        scroll->setObjectName(QStringLiteral("DiagnosticsPanelScroll_%1").arg(key));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setWidget(widget);
        dock->setWidget(scroll);
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        dock->setMinimumSize(250, 170);
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        diagnosticDocks_.append(dock);
        return dock;
    };
    auto* diagnosticsDock
        = addDock(QStringLiteral("diagnostics"), QStringLiteral("Диагностика"), diagnosticsPanel);
    auto* stressDock = addDock(QStringLiteral("stress_test"), QStringLiteral("Стресс-тест"), stressPanel);
    auto* reportDock = addDock(QStringLiteral("report"), QStringLiteral("Отчёт"), reportPanel);
    QList<QRect> availableScreens;
    for (const auto* screen : QGuiApplication::screens()) availableScreens.append(screen->availableGeometry());
    const auto restored = restoreDiagnosticDockLayout(*diagnosticDockHost_,
        {diagnosticsDock, stressDock, reportDock}, settings_.diagnosticHubDockState, availableScreens);
    if (!settings_.diagnosticHubDockState.isEmpty() && !restored.restored)
        qWarning() << "Invalid diagnostic dock layout; using defaults";
    if (restored.redockedPanels > 0)
        qInfo() << "Recovered off-screen diagnostic panels:" << restored.redockedPanels;
    // Normalize legacy versions and any hidden/off-screen panels for the next settings write.
    settings_.diagnosticHubDockState = QString::fromLatin1(diagnosticDockHost_->saveState(1).toBase64());

    connect(diagnosticScanButton_, &QPushButton::clicked, this, &MainWindow::startDiagnosticScan);
    connect(deepScanButton_, &QPushButton::clicked, this, &MainWindow::startDeepScan);
    connect(fullScanButton_, &QPushButton::clicked, this, &MainWindow::startFullScan);
    connect(diagnosticIncidentButton_, &QPushButton::clicked, this, &MainWindow::markProblemNow);
    connect(publicIpRefreshButton_, &QPushButton::clicked, this, &MainWindow::startPublicIpLookup);
    connect(speedTestButton_, &QPushButton::clicked, this, &MainWindow::startInternetSpeedTest);
    connect(
        diagnosticTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::renderDiagnosticDetails);
    connect(diagnosticSaveJsonButton_, &QPushButton::clicked, this, [this] { saveDiagnosticReport(true); });
    connect(diagnosticSaveTextButton_, &QPushButton::clicked, this, [this] { saveDiagnosticReport(false); });
    connect(diagnosticCopyButton_, &QPushButton::clicked, this,
        [this]
        {
            QApplication::clipboard()->setText(diagnosticReportText(latestDiagnosticReport_));
            diagnosticStatusValue_->setText(
                QStringLiteral("Отчёт скопирован — его можно вставить в чат для разбора"));
        });
    return diagnosticDockHost_;
}

QWidget* MainWindow::createHardwarePage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("HardwareScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* page = new QWidget(scroll);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("Спецификация железа"), page);
    title->setObjectName(QStringLiteral("Header"));
    hardwareCopyButton_ = new QPushButton(QStringLiteral("📋 Скопировать характеристики"), page);
    hardwareCopyButton_->setObjectName(QStringLiteral("HardwareCopyButton"));
    hardwareCopyButton_->setEnabled(false);
    hardwareRefreshButton_ = new QPushButton(QStringLiteral("Обновить"), page);
    hardwareRefreshButton_->setObjectName(QStringLiteral("HardwareRefreshButton"));
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(hardwareCopyButton_);
    header->addWidget(hardwareRefreshButton_);
    layout->addLayout(header);

    hardwareStatus_ = new QLabel(QStringLiteral("Данные будут собраны при первом открытии вкладки."), page);
    hardwareStatus_->setObjectName(QStringLiteral("HardwareStatus"));
    hardwareStatus_->setWordWrap(true);
    hardwareProgress_ = new QProgressBar(page);
    hardwareProgress_->setObjectName(QStringLiteral("HardwareProgress"));
    hardwareProgress_->setRange(0, 15);
    hardwareProgress_->setValue(0);
    hardwareProgress_->hide();
    layout->addWidget(hardwareStatus_);
    layout->addWidget(hardwareProgress_);

    hardwareSectionsHost_ = new QWidget(page);
    hardwareSectionsHost_->setObjectName(QStringLiteral("HardwareSections"));
    hardwareSectionsLayout_ = new QVBoxLayout(hardwareSectionsHost_);
    hardwareSectionsLayout_->setContentsMargins(0, 0, 0, 0);
    hardwareSectionsLayout_->setSpacing(10);
    layout->addWidget(hardwareSectionsHost_);
    layout->addStretch(1);
    scroll->setWidget(page);

    connect(hardwareRefreshButton_, &QPushButton::clicked, this, &MainWindow::startHardwareScan);
    connect(hardwareCopyButton_, &QPushButton::clicked, this,
        [this]
        {
            QApplication::clipboard()->setText(hardwareCopyText_);
            hardwareStatus_->setText(QStringLiteral("✓ Скопировано в буфер обмена"));
        });
    return scroll;
}

QJsonObject MainWindow::buildHardwareSeed() const
{
    QJsonObject seed;
    seed.insert(QStringLiteral("os"),
        QJsonObject {
            { QStringLiteral("system"), QSysInfo::productType() },
            { QStringLiteral("release"), QSysInfo::productVersion() },
            { QStringLiteral("version"),
                latestOperatingSystem_.isEmpty() ? QSysInfo::prettyProductName() : latestOperatingSystem_ },
            { QStringLiteral("machine"), QSysInfo::currentCpuArchitecture() },
        });
    seed.insert(QStringLiteral("cpu"),
        QJsonObject {
            { QStringLiteral("model"), latestCpuName_.isEmpty() ? QStringLiteral("н/д") : latestCpuName_ },
            { QStringLiteral("frequency_mhz"),
                latestCpuFrequencyMhz_ >= 0.0 ? QJsonValue(latestCpuFrequencyMhz_)
                                              : QJsonValue(QJsonValue::Null) },
            { QStringLiteral("logical_threads"),
                latestCpuCores_.isEmpty() ? QJsonValue(QJsonValue::Null)
                                          : QJsonValue(latestCpuCores_.size()) },
        });
    seed.insert(QStringLiteral("ram"),
        QJsonObject {
            { QStringLiteral("total_gb"),
                latestRamTotalGiB_ >= 0.0 ? QJsonValue(latestRamTotalGiB_) : QJsonValue(QJsonValue::Null) },
        });
    seed.insert(QStringLiteral("runtime_memory"),
        QJsonObject {
            { QStringLiteral("memory_pressure"), latestRuntime_.memoryPressure },
            { QStringLiteral("paging_activity"), latestRuntime_.pagingActivity },
        });

    QJsonArray gpus;
    if (!latestGpuName_.trimmed().isEmpty() && latestGpuName_ != QStringLiteral("н/д"))
    {
        gpus.append(QJsonObject {
            { QStringLiteral("model"), latestGpuName_ },
            { QStringLiteral("memory_mb"),
                latestGpuMemoryTotalGiB_ >= 0.0 ? QJsonValue(latestGpuMemoryTotalGiB_ * 1024.0)
                                                : QJsonValue(QJsonValue::Null) },
            { QStringLiteral("temperature_c"),
                latestGpuTemperatureC_ >= 0.0 ? QJsonValue(latestGpuTemperatureC_)
                                              : QJsonValue(QJsonValue::Null) },
        });
    }
    seed.insert(QStringLiteral("gpu"), gpus);

    QJsonArray disks;
    for (const auto& disk : latestDisks_)
    {
        disks.append(QJsonObject {
            { QStringLiteral("name"), disk.name },
            { QStringLiteral("mountpoint"), disk.mountPoint },
            { QStringLiteral("fstype"), disk.fileSystem },
            { QStringLiteral("type"), disk.storageType },
            { QStringLiteral("total_gb"), disk.totalGiB },
            { QStringLiteral("used_gb"), disk.usedGiB },
            { QStringLiteral("free_gb"), disk.freeGiB },
            { QStringLiteral("used_percent"), disk.usedPercent },
        });
    }
    seed.insert(QStringLiteral("disks"), disks);

    QJsonArray temperatures;
    for (const auto& sensor : latestTemperatures_)
    {
        temperatures.append(QJsonObject {
            { QStringLiteral("component"), sensor.component },
            { QStringLiteral("label"), sensor.label },
            { QStringLiteral("current_c"), sensor.valueC },
            { QStringLiteral("critical_c"),
                sensor.criticalC >= 0.0 ? QJsonValue(sensor.criticalC) : QJsonValue(QJsonValue::Null) },
            { QStringLiteral("source"), sensor.source },
        });
    }
    seed.insert(QStringLiteral("sensors"), temperatures);
    QJsonArray fans;
    for (const auto& fan : latestFans_)
    {
        fans.append(QJsonObject {
            { QStringLiteral("component"), fan.component },
            { QStringLiteral("label"), fan.label },
            { QStringLiteral("rpm"), fan.rpm >= 0.0 ? QJsonValue(fan.rpm) : QJsonValue(QJsonValue::Null) },
            { QStringLiteral("percent"),
                fan.percent >= 0.0 ? QJsonValue(fan.percent) : QJsonValue(QJsonValue::Null) },
            { QStringLiteral("source"), fan.source },
        });
    }
    seed.insert(QStringLiteral("fans"), fans);
    QJsonObject summary;
    for (const auto& sensor : latestTemperatures_)
    {
        if (sensor.component == QStringLiteral("cpu"))
        {
            summary.insert(QStringLiteral("cpu_temp_c"), sensor.valueC);
            summary.insert(QStringLiteral("cpu_temp_label"), sensor.label);
            summary.insert(QStringLiteral("cpu_temp_source"), sensor.source);
            break;
        }
    }
    seed.insert(QStringLiteral("sensor_snapshot"),
        QJsonObject {
            { QStringLiteral("summary"), summary },
            { QStringLiteral("temperature_count"), temperatures.size() },
            { QStringLiteral("fan_count"), fans.size() },
        });

    QJsonArray displays;
    for (auto* screen : QGuiApplication::screens())
    {
        if (screen == nullptr)
            continue;
        const QSize size = screen->size();
        displays.append(QJsonObject {
            { QStringLiteral("name"), screen->name().isEmpty() ? QStringLiteral("н/д") : screen->name() },
            { QStringLiteral("resolution"), QStringLiteral("%1x%2").arg(size.width()).arg(size.height()) },
            { QStringLiteral("refresh_rate_hz"), screen->refreshRate() },
            { QStringLiteral("scale"), screen->devicePixelRatio() },
            { QStringLiteral("manufacturer"), screen->manufacturer() },
            { QStringLiteral("model"), screen->model() },
            { QStringLiteral("serial"), screen->serialNumber() },
            { QStringLiteral("is_approximate"), true },
        });
    }
    seed.insert(QStringLiteral("displays"), displays);

    QJsonArray adapters;
    for (const auto& adapter : QNetworkInterface::allInterfaces())
    {
        const auto flags = adapter.flags();
        if (flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        QStringList ipv4;
        for (const auto& address : adapter.addressEntries())
        {
            if (address.ip().protocol() == QAbstractSocket::IPv4Protocol)
            {
                ipv4.append(address.ip().toString());
            }
        }
        adapters.append(QJsonObject {
            { QStringLiteral("name"),
                adapter.humanReadableName().isEmpty() ? adapter.name() : adapter.humanReadableName() },
            { QStringLiteral("system_name"), adapter.name() },
            { QStringLiteral("interface_index"), adapter.index() },
            { QStringLiteral("mac"),
                adapter.hardwareAddress().isEmpty() ? QStringLiteral("н/д")
                                                    : adapter.hardwareAddress().toUpper() },
            { QStringLiteral("is_up"), flags.testFlag(QNetworkInterface::IsUp) },
            { QStringLiteral("mtu"),
                adapter.maximumTransmissionUnit() > 0 ? QJsonValue(adapter.maximumTransmissionUnit())
                                                      : QJsonValue(QJsonValue::Null) },
            { QStringLiteral("ipv4"), ipv4.join(QStringLiteral(", ")) },
        });
    }
    seed.insert(QStringLiteral("network_adapters"), adapters);
    return seed;
}

void MainWindow::startHardwareScan()
{
    if (paused_ || hardwareWorker_ == nullptr || hardwareWorker_->isRunning())
        return;
    if (!telemetryReceived_)
    {
        hardwareScanPending_ = true;
        hardwareStatus_->setText(QStringLiteral("Ожидание первого замера оборудования…"));
        return;
    }
    hardwareScanPending_ = false;
    hardwareScanCompleted_ = false;
    hardwareProgress_->setValue(0);
    hardwareProgress_->show();
    hardwareStatus_->setText(QStringLiteral("Сбор характеристик…"));
    hardwareRefreshButton_->setEnabled(false);
    hardwareCopyButton_->setEnabled(false);
    hardwareWorker_->scan(buildHardwareSeed());
}

void MainWindow::applyHardwareReport(const QJsonObject& report)
{
    if (paused_)
        return;
    hardwareScanCompleted_ = true;
    latestHardwareReport_ = report;
    hardwareLoaded_ = true;
    hardwareProgress_->setValue(15);
    hardwareProgress_->hide();
    hardwareRefreshButton_->setEnabled(!paused_);
    hardwareCopyButton_->setEnabled(true);
    renderHardwareReport();
    hardwareStatus_->setText(report.value(QStringLiteral("collection_partial")).toBool()
            ? QStringLiteral(
                  "Отчёт частичный: некоторые системные источники не ответили. Можно повторить сбор.")
            : QStringLiteral("Характеристики собраны. Отсутствующие данные отмечены как «н/д»."));
}

void MainWindow::addHardwareSection(const QString& title, const QList<QPair<QString, QString>>& rows)
{
    auto* card = new QFrame(hardwareSectionsHost_);
    card->setObjectName(QStringLiteral("MetricCard"));
    card->setProperty("hardwareSection", title);
    auto* layout = new QVBoxLayout(card);
    auto* caption = new QLabel(title, card);
    caption->setObjectName(QStringLiteral("CardTitle"));
    layout->addWidget(caption);
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for (const auto& [label, suppliedValue] : rows)
    {
        const QString value = suppliedValue.trimmed().isEmpty() ? QStringLiteral("н/д") : suppliedValue;
        form->addRow(label, makeValueLabel(value, card));
    }
    layout->addLayout(form);
    hardwareSectionsLayout_->addWidget(card);

    hardwareCopyText_ += title + u'\n' + QString(title.size(), u'-') + u'\n';
    for (const auto& [label, suppliedValue] : rows)
    {
        hardwareCopyText_ += label + QStringLiteral(": ")
            + (suppliedValue.trimmed().isEmpty() ? QStringLiteral("н/д") : suppliedValue) + u'\n';
    }
    hardwareCopyText_ += u'\n';
}

void MainWindow::renderHardwareReport()
{
    while (auto* item = hardwareSectionsLayout_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    hardwareCopyText_ = QStringLiteral("O.R.I.O.N. — СПЕЦИФИКАЦИЯ ПК\n================================\n\n");
    const auto text = [](const QJsonValue& value, const int decimals = 0)
    {
        if (value.isNull() || value.isUndefined())
            return QStringLiteral("н/д");
        if (value.isDouble())
            return QString::number(value.toDouble(), 'f', decimals);
        const QString result = value.toString().trimmed();
        return result.isEmpty() ? QStringLiteral("н/д") : result;
    };

    const auto rating = latestHardwareReport_.value(QStringLiteral("rating")).toObject();
    QStringList ratingDetails;
    for (const auto& value : rating.value(QStringLiteral("reasons")).toArray())
    {
        ratingDetails.append(QStringLiteral("• %1").arg(value.toString()));
    }
    addHardwareSection(QStringLiteral("Оценка ПК"),
        {
            { QStringLiteral("Класс"), text(rating.value(QStringLiteral("label"))) },
            { QStringLiteral("Почему"),
                ratingDetails.isEmpty() ? QStringLiteral("н/д") : ratingDetails.join(u'\n') },
        });

    const auto os = latestHardwareReport_.value(QStringLiteral("os")).toObject();
    addHardwareSection(QStringLiteral("Операционная система"),
        {
            { QStringLiteral("Система"), text(os.value(QStringLiteral("version"))) },
            { QStringLiteral("Выпуск"), text(os.value(QStringLiteral("release"))) },
            { QStringLiteral("Архитектура"), text(os.value(QStringLiteral("machine"))) },
        });
    const auto board = latestHardwareReport_.value(QStringLiteral("motherboard")).toObject();
    addHardwareSection(QStringLiteral("Материнская плата"),
        {
            { QStringLiteral("Производитель"), text(board.value(QStringLiteral("vendor"))) },
            { QStringLiteral("Модель"), text(board.value(QStringLiteral("model"))) },
        });
    const auto bios = latestHardwareReport_.value(QStringLiteral("bios")).toObject();
    addHardwareSection(QStringLiteral("BIOS / UEFI"),
        {
            { QStringLiteral("Производитель"), text(bios.value(QStringLiteral("vendor"))) },
            { QStringLiteral("Версия прошивки"), text(bios.value(QStringLiteral("version"))) },
            { QStringLiteral("Дата релиза"), text(bios.value(QStringLiteral("date"))) },
        });

    const auto cpu = latestHardwareReport_.value(QStringLiteral("cpu")).toObject();
    const auto sensorSummary = latestHardwareReport_.value(QStringLiteral("sensor_snapshot"))
                                   .toObject()
                                   .value(QStringLiteral("summary"))
                                   .toObject();
    QString cpuTemperature = QStringLiteral("недоступна — точный CPU Package не опубликован");
    if (sensorSummary.value(QStringLiteral("cpu_temp_c")).isDouble())
    {
        cpuTemperature = QStringLiteral("%1°C — %2 (%3)")
                             .arg(sensorSummary.value(QStringLiteral("cpu_temp_c")).toDouble(), 0, 'f', 1)
                             .arg(text(sensorSummary.value(QStringLiteral("cpu_temp_label"))))
                             .arg(text(sensorSummary.value(QStringLiteral("cpu_temp_source"))));
    }
    const double frequency = cpu.value(QStringLiteral("max_frequency_mhz")).isDouble()
        ? cpu.value(QStringLiteral("max_frequency_mhz")).toDouble()
        : cpu.value(QStringLiteral("frequency_mhz")).toDouble(-1.0);
    addHardwareSection(QStringLiteral("Процессор"),
        {
            { QStringLiteral("Модель"), text(cpu.value(QStringLiteral("model"))) },
            { QStringLiteral("Архитектура"), text(cpu.value(QStringLiteral("architecture"))) },
            { QStringLiteral("Частота"),
                frequency > 0.0 ? QStringLiteral("%1 МГц").arg(frequency, 0, 'f', 0)
                                : QStringLiteral("н/д") },
            { QStringLiteral("Физические ядра"), text(cpu.value(QStringLiteral("physical_cores"))) },
            { QStringLiteral("Логические потоки"), text(cpu.value(QStringLiteral("logical_threads"))) },
            { QStringLiteral("Тип сокета (Socket Designation)"), text(cpu.value(QStringLiteral("socket"))) },
            { QStringLiteral("Кол-во физических сокетов"), text(cpu.value(QStringLiteral("socket_count"))) },
            { QStringLiteral("Температура CPU"), cpuTemperature },
        });

    const auto ram = latestHardwareReport_.value(QStringLiteral("ram")).toObject();
    QList<QPair<QString, QString>> ramRows {
        { QStringLiteral("Общий объём"),
            ram.value(QStringLiteral("total_gb")).isDouble()
                ? QStringLiteral("%1 ГБ").arg(ram.value(QStringLiteral("total_gb")).toDouble(), 0, 'f', 1)
                : QStringLiteral("н/д") },
        { QStringLiteral("Тип / частота"), text(ram.value(QStringLiteral("type_and_speed"))) },
    };
    int moduleIndex = 0;
    for (const auto& value : ram.value(QStringLiteral("modules")).toArray())
    {
        const auto module = value.toObject();
        ++moduleIndex;
        const QString locator = text(module.value(QStringLiteral("DeviceLocator")));
        const QString size = module.value(QStringLiteral("capacity_gb")).isDouble()
            ? QStringLiteral("%1 ГБ").arg(module.value(QStringLiteral("capacity_gb")).toDouble(), 0, 'f', 1)
            : QStringLiteral("н/д");
        const QString speed = module.value(QStringLiteral("speed_mhz")).isDouble()
            ? QStringLiteral("%1 МГц").arg(module.value(QStringLiteral("speed_mhz")).toInt())
            : QStringLiteral("н/д");
        ramRows.append({ QStringLiteral("Модуль #%1").arg(moduleIndex),
            QStringLiteral("%1 · %2 · %3 · %4 · part %5 · serial %6")
                .arg(locator, size, text(module.value(QStringLiteral("type"))), speed,
                    text(module.value(QStringLiteral("PartNumber"))),
                    text(module.value(QStringLiteral("SerialNumber")))) });
    }
    if (moduleIndex == 0)
        ramRows.append({ QStringLiteral("Модули"), QStringLiteral("детализация SPD/SMBIOS недоступна") });
    addHardwareSection(QStringLiteral("Оперативная память"), ramRows);

    QList<QPair<QString, QString>> gpuRows;
    int gpuIndex = 0;
    for (const auto& value : latestHardwareReport_.value(QStringLiteral("gpu")).toArray())
    {
        const auto gpu = value.toObject();
        ++gpuIndex;
        gpuRows.append(
            { QStringLiteral("Видеокарта #%1").arg(gpuIndex), text(gpu.value(QStringLiteral("model"))) });
        gpuRows.append({ QStringLiteral("Выделенная видеопамять #%1").arg(gpuIndex),
            gpu.value(QStringLiteral("memory_mb")).isDouble()
                ? QStringLiteral("%1 МБ").arg(gpu.value(QStringLiteral("memory_mb")).toDouble(), 0, 'f', 0)
                : QStringLiteral("н/д") });
        if (!gpu.value(QStringLiteral("source")).toString().isEmpty())
        {
            gpuRows.append({ QStringLiteral("Источник сведений #%1").arg(gpuIndex),
                text(gpu.value(QStringLiteral("source"))) });
        }
        if (gpu.value(QStringLiteral("shared_memory_mb")).toDouble() > 0)
        {
            gpuRows.append({ QStringLiteral("Общая RAM для GPU #%1").arg(gpuIndex),
                QStringLiteral("до %1 МБ (системная память, не VRAM)")
                    .arg(gpu.value(QStringLiteral("shared_memory_mb")).toDouble(), 0, 'f', 0) });
        }
    }
    if (gpuRows.isEmpty())
        gpuRows.append({ QStringLiteral("Статус"), QStringLiteral("видеокарта не обнаружена") });
    gpuRows.append({ QStringLiteral("Проверка под нагрузкой"),
        QStringLiteral("доступна во вкладке «Диагностика и тесты»") });
    addHardwareSection(QStringLiteral("Видеокарта"), gpuRows);

    const auto npu = latestHardwareReport_.value(QStringLiteral("npu")).toObject();
    QList<QPair<QString, QString>> npuRows;
    int npuIndex = 0;
    for (const auto& value : npu.value(QStringLiteral("devices")).toArray())
    {
        const auto device = value.toObject();
        ++npuIndex;
        npuRows.append({ npuIndex == 1 ? QStringLiteral("Устройство")
                                       : QStringLiteral("Устройство #%1").arg(npuIndex),
            QStringLiteral("%1 — %2; статус: %3")
                .arg(text(device.value(QStringLiteral("name"))), text(device.value(QStringLiteral("vendor"))),
                    text(device.value(QStringLiteral("status")))) });
    }
    if (npuRows.isEmpty())
        npuRows.append({ QStringLiteral("Статус"),
            npu.value(QStringLiteral("data_quality")).toString() == QStringLiteral("collector_error")
                ? QStringLiteral("н/д — обнаружение NPU не завершено")
                : QStringLiteral("выделенный NPU не обнаружен") });
    npuRows.append({ QStringLiteral("Тест"),
        QStringLiteral("не запускается без подтверждённого NPU execution provider") });
    npuRows.append({ QStringLiteral("Примечание"), text(npu.value(QStringLiteral("note"))) });
    addHardwareSection(QStringLiteral("NPU / нейропроцессор"), npuRows);

    const auto diskReport = latestHardwareReport_.value(QStringLiteral("disks")).toObject();
    QList<QPair<QString, QString>> diskRows;
    const auto appendVolume = [&diskRows, &text](const QJsonObject& volume, const QString& prefix)
    {
        QString role
            = volume.value(QStringLiteral("is_system")).toBool() ? QStringLiteral(" · системный") : QString();
        if (volume.value(QStringLiteral("spanned")).toBool())
        {
            role += QStringLiteral(" · составной том на нескольких дисках");
        }
        if (volume.value(QStringLiteral("partition_number")).isDouble())
        {
            role
                += QStringLiteral(" · раздел %1").arg(text(volume.value(QStringLiteral("partition_number"))));
        }
        diskRows.append(
            { QStringLiteral("%1Том %2").arg(prefix, text(volume.value(QStringLiteral("mountpoint")))),
                QStringLiteral("%1; %2 ГБ; занято %3 ГБ (%4%); свободно %5 ГБ%6")
                    .arg(text(volume.value(QStringLiteral("fstype"))))
                    .arg(volume.value(QStringLiteral("total_gb")).toDouble(), 0, 'f', 1)
                    .arg(volume.value(QStringLiteral("used_gb")).toDouble(), 0, 'f', 1)
                    .arg(volume.value(QStringLiteral("used_percent")).toDouble(), 0, 'f', 1)
                    .arg(volume.value(QStringLiteral("free_gb")).toDouble(), 0, 'f', 1)
                    .arg(role) });
        if (!volume.value(QStringLiteral("name")).toString().isEmpty())
        {
            diskRows.append({ QStringLiteral("Метка тома"), text(volume.value(QStringLiteral("name"))) });
        }
    };
    for (const auto& value : diskReport.value(QStringLiteral("groups")).toArray())
    {
        const auto group = value.toObject();
        const auto disk = group.value(QStringLiteral("physical")).toObject();
        const auto size = disk.value(QStringLiteral("Size"));
        const double bytes = size.isDouble() ? size.toDouble() : size.toString().toDouble();
        const QString capacity = bytes > 0.0
            ? QStringLiteral("%1 ГБ").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1)
            : QStringLiteral("н/д");
        const QString diskLabel = QStringLiteral("Диск %1").arg(text(disk.value(QStringLiteral("Index"))));
        diskRows.append({ diskLabel,
            QStringLiteral("%1 · %2 · %3 · %4")
                .arg(text(disk.value(QStringLiteral("Model"))), capacity,
                    text(disk.value(QStringLiteral("bus_type"))),
                    text(disk.value(QStringLiteral("storage_type")))) });
        diskRows.append({ QStringLiteral("Серийный номер / прошивка"),
            QStringLiteral("%1 / %2").arg(text(disk.value(QStringLiteral("SerialNumber"))),
                text(disk.value(QStringLiteral("FirmwareRevision")))) });
        diskRows.append(
            { QStringLiteral("Таблица разделов"), text(disk.value(QStringLiteral("partition_style"))) });
        if (!disk.value(QStringLiteral("Status")).toString().isEmpty())
        {
            diskRows.append(
                { QStringLiteral("Статус Windows (не SMART)"), text(disk.value(QStringLiteral("Status"))) });
        }
        const auto volumes = group.value(QStringLiteral("volumes")).toArray();
        for (const auto& volume : volumes)
            appendVolume(volume.toObject(), diskLabel + QStringLiteral(" — "));
        if (volumes.isEmpty())
            diskRows.append(
                { QStringLiteral("Тома"), QStringLiteral("доступные смонтированные тома не сопоставлены") });
    }
    const auto unmapped = diskReport.value(QStringLiteral("unmapped_volumes")).toArray();
    if (!unmapped.isEmpty())
    {
        diskRows.append({ QStringLiteral("Несопоставленные тома"),
            QStringLiteral("Физический диск не подтверждён системным источником") });
        for (const auto& volume : unmapped)
            appendVolume(volume.toObject(), QString());
    }
    if (diskRows.isEmpty())
        diskRows.append({ QStringLiteral("Статус"), QStringLiteral("накопители не обнаружены") });
    addHardwareSection(QStringLiteral("Накопители"), diskRows);

    QList<QPair<QString, QString>> displayRows;
    int displayIndex = 0;
    bool approximateDisplay = false;
    for (const auto& value : latestHardwareReport_.value(QStringLiteral("displays")).toArray())
    {
        const auto display = value.toObject();
        ++displayIndex;
        approximateDisplay |= display.value(QStringLiteral("is_approximate")).toBool();
        displayRows.append(
            { displayIndex == 1 ? QStringLiteral("Монитор") : QStringLiteral("Монитор #%1").arg(displayIndex),
                QStringLiteral("%1 @ %2%3 Гц · scale %4 (%5)")
                    .arg(text(display.value(QStringLiteral("resolution"))),
                        display.value(QStringLiteral("is_approximate")).toBool() ? QStringLiteral("≈")
                                                                                 : QString(),
                        QString::number(display.value(QStringLiteral("refresh_rate_hz")).toDouble(), 'f', 1),
                        QString::number(display.value(QStringLiteral("scale")).toDouble(1.0), 'f', 2),
                        text(display.value(QStringLiteral("name")))) });
    }
    if (displayRows.isEmpty())
        displayRows.append({ QStringLiteral("Статус"), QStringLiteral("монитор не обнаружен") });
    if (approximateDisplay)
        displayRows.append({ QStringLiteral("Примечание"),
            QStringLiteral("≈ — частота получена через Qt и может быть приблизительной") });
    addHardwareSection(QStringLiteral("Дисплей / монитор"), displayRows);

    QList<QPair<QString, QString>> fanRows;
    for (const auto& value : latestHardwareReport_.value(QStringLiteral("fans")).toArray())
    {
        const auto fan = value.toObject();
        const QString amount = fan.value(QStringLiteral("rpm")).isDouble()
            ? QStringLiteral("%1 об/мин").arg(fan.value(QStringLiteral("rpm")).toDouble(), 0, 'f', 0)
            : fan.value(QStringLiteral("percent")).isDouble()
            ? QStringLiteral("%1%").arg(fan.value(QStringLiteral("percent")).toDouble(), 0, 'f', 1)
            : QStringLiteral("н/д");
        fanRows.append({ text(fan.value(QStringLiteral("label"))),
            QStringLiteral("%1 (%2; %3)")
                .arg(amount, text(fan.value(QStringLiteral("component"))).toUpper(),
                    text(fan.value(QStringLiteral("source")))) });
    }
    if (fanRows.isEmpty())
        fanRows.append({ QStringLiteral("Статус"),
            QStringLiteral("обороты/процент вентиляторов не опубликованы доступными источниками") });
    addHardwareSection(QStringLiteral("Вентиляторы"), fanRows);

    const auto batteryValue = latestHardwareReport_.value(QStringLiteral("battery"));
    QList<QPair<QString, QString>> batteryRows;
    if (!batteryValue.isObject())
    {
        batteryRows.append(
            { QStringLiteral("Статус"), QStringLiteral("батарея не обнаружена (стационарный ПК)") });
    }
    else
    {
        const auto battery = batteryValue.toObject();
        batteryRows.append({ QStringLiteral("Заряд"),
            battery.value(QStringLiteral("percent")).isDouble()
                ? QStringLiteral("%1%").arg(battery.value(QStringLiteral("percent")).toDouble(), 0, 'f', 1)
                : QStringLiteral("н/д") });
        batteryRows.append({ QStringLiteral("Питание"),
            !battery.value(QStringLiteral("plugged_in")).isBool()      ? QStringLiteral("н/д")
                : battery.value(QStringLiteral("plugged_in")).toBool() ? QStringLiteral("от сети")
                                                                       : QStringLiteral("от батареи") });
        if (battery.value(QStringLiteral("eta_seconds")).isDouble())
        {
            batteryRows.append({ QStringLiteral("Осталось (оценка)"),
                QStringLiteral("~%1 мин").arg(
                    battery.value(QStringLiteral("eta_seconds")).toDouble() / 60.0, 0, 'f', 0) });
        }
    }
    addHardwareSection(QStringLiteral("Батарея"), batteryRows);

    QList<QPair<QString, QString>> networkRows;
    for (const auto& value : latestHardwareReport_.value(QStringLiteral("network_adapters")).toArray())
    {
        const auto adapter = value.toObject();
        networkRows.append({ text(adapter.value(QStringLiteral("name"))),
            QStringLiteral("%1 — %2; IPv4 %3; MTU %4\nСкорость соединения: приём %5 / передача %6 Мбит/с")
                .arg(text(adapter.value(QStringLiteral("mac"))),
                    adapter.value(QStringLiteral("is_up")).toBool() ? QStringLiteral("активен")
                                                                    : QStringLiteral("неактивен"),
                    text(adapter.value(QStringLiteral("ipv4"))), text(adapter.value(QStringLiteral("mtu"))),
                    text(adapter.value(QStringLiteral("receive_link_mbps")), 1),
                    text(adapter.value(QStringLiteral("transmit_link_mbps")), 1)) });
    }
    if (networkRows.isEmpty())
        networkRows.append({ QStringLiteral("Статус"), QStringLiteral("сетевые адаптеры не обнаружены") });
    addHardwareSection(QStringLiteral("Сетевые адаптеры"), networkRows);

    QList<QPair<QString, QString>> sensorRows;
    for (const auto& value : latestHardwareReport_.value(QStringLiteral("sensors")).toArray())
    {
        const auto sensor = value.toObject();
        QString sensorValue = sensor.value(QStringLiteral("current_c")).isDouble()
            ? QStringLiteral("%1°C").arg(sensor.value(QStringLiteral("current_c")).toDouble(), 0, 'f', 1)
            : QStringLiteral("н/д");
        if (sensor.value(QStringLiteral("critical_c")).isDouble())
        {
            sensorValue += QStringLiteral(" (крит. %1°C)")
                               .arg(sensor.value(QStringLiteral("critical_c")).toDouble(), 0, 'f', 1);
        }
        sensorValue += QStringLiteral(" (%1; %2)")
                           .arg(text(sensor.value(QStringLiteral("component"))).toUpper(),
                               text(sensor.value(QStringLiteral("source"))));
        sensorRows.append({ text(sensor.value(QStringLiteral("label"))), sensorValue });
    }
    if (sensorRows.isEmpty())
        sensorRows.append({ QStringLiteral("Статус"),
            QStringLiteral("температурные датчики не опубликованы доступными источниками") });
    addHardwareSection(QStringLiteral("Температурные датчики"), sensorRows);
}

QWidget* MainWindow::createPlaceholderPage(const QString& titleText, const QString& description)
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 12, 10, 10);
    auto* title = new QLabel(titleText, page);
    title->setObjectName(QStringLiteral("Header"));
    auto* panel = new QFrame(page);
    panel->setObjectName(QStringLiteral("MetricCard"));
    auto* panelLayout = new QVBoxLayout(panel);
    auto* state = new QLabel(QStringLiteral("Каркас вкладки перенесён"), panel);
    state->setObjectName(QStringLiteral("CardTitle"));
    auto* text = new QLabel(description, panel);
    text->setObjectName(QStringLiteral("CardDetail"));
    text->setWordWrap(true);
    panelLayout->addWidget(state);
    panelLayout->addWidget(text);
    layout->addWidget(title);
    layout->addWidget(panel);
    layout->addStretch(1);
    return page;
}

QWidget* MainWindow::createAppMonitorPage()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("AppMonitorScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Проблемное приложение"), page);
    title->setObjectName(QStringLiteral("Header"));
    auto* explanation = new QLabel(
        QStringLiteral("O.R.I.O.N. запустит выбранный EXE и будет наблюдать всё его дерево процессов: "
                       "CPU, память, I/O, окна без отклика, системную телеметрию и новые события Windows."),
        page);
    explanation->setObjectName(QStringLiteral("CardDetail"));
    explanation->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(explanation);

    auto* controls = new QFrame(page);
    controls->setObjectName(QStringLiteral("MetricCard"));
    auto* controlsLayout = new QGridLayout(controls);
    controlsLayout->setContentsMargins(12, 12, 12, 12);
    appExecutablePath_ = new QLineEdit(controls);
    appExecutablePath_->setObjectName(QStringLiteral("AppMonitorExecutable"));
    appExecutablePath_->setPlaceholderText(QStringLiteral("Выберите исполняемый файл .exe"));
    auto* browse = new QPushButton(QStringLiteral("Выбрать EXE…"), controls);
    appBrowseButton_ = browse;
    browse->setObjectName(QStringLiteral("AppMonitorBrowse"));
    appDuration_ = new QComboBox(controls);
    appDuration_->setObjectName(QStringLiteral("AppMonitorDuration"));
    for (const int minutes : { 1, 5, 10, 15, 30, 60, 120, 240, 480 })
    {
        appDuration_->addItem(minutes < 60 ? QStringLiteral("%1 мин").arg(minutes)
            : QStringLiteral("%1 ч").arg(minutes / 60), minutes * 60);
    }
    appDuration_->setCurrentIndex(appDuration_->findData(60 * 60));
    appCloseOnTimeout_
        = new QCheckBox(QStringLiteral("По истечении времени закрыть запущенное дерево процессов"), controls);
    appCloseOnTimeout_->setObjectName(QStringLiteral("AppMonitorAutoClose"));
    appCloseOnTimeout_->setChecked(true);
    appCloseOnTimeout_->setToolTip(
        QStringLiteral("Сначала обычное закрытие окна, затем 7 секунд ожидания и принудительное завершение. "
                       "Несохранённые данные могут быть потеряны. Остановить наблюдение — не значит закрыть приложение."));
    appStartButton_ = new QPushButton(QStringLiteral("Запустить и наблюдать"), controls);
    appStartButton_->setObjectName(QStringLiteral("PrimaryButton"));
    appStartButton_->setEnabled(false);
    appStopButton_ = new QPushButton(QStringLiteral("Остановить наблюдение"), controls);
    appStopButton_->setObjectName(QStringLiteral("DangerButton"));
    appStopButton_->setEnabled(false);
    controlsLayout->addWidget(new QLabel(QStringLiteral("Приложение:"), controls), 0, 0);
    controlsLayout->addWidget(appExecutablePath_, 0, 1);
    controlsLayout->addWidget(browse, 0, 2);
    controlsLayout->addWidget(new QLabel(QStringLiteral("Длительность:"), controls), 1, 0);
    controlsLayout->addWidget(appDuration_, 1, 1);
    controlsLayout->addWidget(appCloseOnTimeout_, 2, 0, 1, 3);
    controlsLayout->addWidget(appStartButton_, 3, 0, 1, 2);
    controlsLayout->addWidget(appStopButton_, 3, 2);
    appSamplingHint_ = new QLabel(controls);
    appSamplingHint_->setObjectName(QStringLiteral("AppMonitorSamplingHint"));
    appSamplingHint_->setWordWrap(true);
    controlsLayout->addWidget(appSamplingHint_, 4, 0, 1, 3);
    layout->addWidget(controls);

    auto* live = new QFrame(page);
    live->setObjectName(QStringLiteral("MetricCard"));
    auto* liveLayout = new QVBoxLayout(live);
    auto* liveHeader = new QHBoxLayout;
    appStatusValue_ = makeValueLabel(QStringLiteral("Выберите приложение для наблюдения"), live);
    appStatusValue_->setObjectName(QStringLiteral("AppMonitorStatus"));
    appStatusValue_->setWordWrap(true);
    appProgress_ = new QProgressBar(live);
    appProgress_->setObjectName(QStringLiteral("AppMonitorProgress"));
    appProgress_->setRange(0, 100);
    appProgress_->setValue(0);
    appProgress_->setFixedWidth(100);
    liveHeader->addWidget(appStatusValue_, 1);
    liveHeader->addWidget(appProgress_);
    auto* liveGrid = new QGridLayout;
    appCpuValue_ = makeValueLabel(QStringLiteral("н/д"), live);
    appRamValue_ = makeValueLabel(QStringLiteral("н/д"), live);
    appIoValue_ = makeValueLabel(QStringLiteral("н/д"), live);
    appWindowValue_ = makeValueLabel(QStringLiteral("н/д"), live);
    appCpuValue_->setObjectName(QStringLiteral("AppMonitorCpu"));
    appRamValue_->setObjectName(QStringLiteral("AppMonitorRam"));
    appWindowValue_->setObjectName(QStringLiteral("AppMonitorWindow"));
    for (auto* value : {appCpuValue_, appRamValue_, appIoValue_, appWindowValue_}) value->setWordWrap(true);
    liveGrid->addWidget(new QLabel(QStringLiteral("CPU дерева"), live), 0, 0);
    liveGrid->addWidget(appCpuValue_, 0, 1);
    liveGrid->addWidget(new QLabel(QStringLiteral("Память"), live), 0, 2);
    liveGrid->addWidget(appRamValue_, 0, 3);
    liveGrid->addWidget(new QLabel(QStringLiteral("I/O"), live), 1, 0);
    liveGrid->addWidget(appIoValue_, 1, 1);
    liveGrid->addWidget(new QLabel(QStringLiteral("Окна"), live), 1, 2);
    liveGrid->addWidget(appWindowValue_, 1, 3);
    appSampleTable_ = new QTableWidget(0, 6, live);
    appSampleTable_->setObjectName(QStringLiteral("AppMonitorSamples"));
    appSampleTable_->setHorizontalHeaderLabels({
        QStringLiteral("Время"),
        QStringLiteral("CPU"),
        QStringLiteral("Working set"),
        QStringLiteral("Private"),
        QStringLiteral("Процессы"),
        QStringLiteral("Отклик"),
    });
    appSampleTable_->verticalHeader()->hide();
    appSampleTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    appSampleTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    appSampleTable_->setSelectionMode(QAbstractItemView::NoSelection);
    appSampleTable_->setAlternatingRowColors(true);
    appSampleTable_->setMinimumHeight(180);
    liveLayout->addLayout(liveHeader);
    liveLayout->addLayout(liveGrid);
    appLiveDetails_ = new DetailCard(QStringLiteral("Текущий срез"), QStringLiteral("app_live"), false, live);
    appLiveDetails_->setObjectName(QStringLiteral("AppMonitorLiveDetails"));
    appLiveDetails_->setField(QStringLiteral("threads"), QStringLiteral("Потоки / handles"), QStringLiteral("н/д"));
    appLiveDetails_->setField(QStringLiteral("faults"), QStringLiteral("Page faults /с"), QStringLiteral("н/д"));
    appLiveDetails_->setField(QStringLiteral("commit"), QStringLiteral("Commit системы"), QStringLiteral("н/д"));
    appLiveDetails_->setField(QStringLiteral("gpu"), QStringLiteral("GPU системы"), QStringLiteral("н/д"));
    appLiveDetails_->setField(QStringLiteral("quality"), QStringLiteral("Системные данные"), QStringLiteral("н/д"));
    liveLayout->addWidget(appLiveDetails_);
    appCpuChart_ = new AppMonitorChart(AppMonitorChart::Mode::Cpu, live);
    appCpuChart_->setObjectName(QStringLiteral("AppMonitorCpuChart"));
    appMemoryChart_ = new AppMonitorChart(AppMonitorChart::Mode::Memory, live);
    appMemoryChart_->setObjectName(QStringLiteral("AppMonitorMemoryChart"));
    liveLayout->addWidget(appCpuChart_);
    liveLayout->addWidget(appMemoryChart_);
    auto* sampleToggle = new QPushButton(QStringLiteral("Последние 50 замеров"), live);
    sampleToggle->setCheckable(true);
    liveLayout->addWidget(sampleToggle);
    appSampleTable_->hide();
    connect(sampleToggle, &QPushButton::toggled, appSampleTable_, &QWidget::setVisible);
    liveLayout->addWidget(appSampleTable_);
    layout->addWidget(live);

    appVerdictCard_ = new DetailCard(QStringLiteral("Итог наблюдения"), QStringLiteral("app_verdict"), true, page);
    appVerdictCard_->setObjectName(QStringLiteral("AppMonitorVerdict"));
    for (auto* label : appVerdictCard_->findChildren<QLabel*>()) label->setTextFormat(Qt::PlainText);
    appVerdictCard_->hide();
    layout->addWidget(appVerdictCard_);

    auto* report = new QFrame(page);
    report->setObjectName(QStringLiteral("MetricCard"));
    auto* reportLayout = new QVBoxLayout(report);
    auto* reportHeader = new QHBoxLayout;
    auto* reportTitle = new QLabel(QStringLiteral("Отчёт наблюдения"), report);
    reportTitle->setObjectName(QStringLiteral("Header"));
    appSaveJsonButton_ = new QPushButton(QStringLiteral("Сохранить JSON"), report);
    appSaveTextButton_ = new QPushButton(QStringLiteral("Сохранить TXT"), report);
    appCopyButton_ = new QPushButton(QStringLiteral("📋 Скопировать"), report);
    for (auto* button : { appSaveJsonButton_, appSaveTextButton_, appCopyButton_ })
        button->setEnabled(false);
    reportHeader->addWidget(reportTitle, 1);
    reportHeader->addWidget(appSaveJsonButton_);
    reportHeader->addWidget(appSaveTextButton_);
    reportHeader->addWidget(appCopyButton_);
    appReportPreview_ = new QTextEdit(report);
    appReportPreview_->setObjectName(QStringLiteral("AppMonitorReportPreview"));
    appReportPreview_->setReadOnly(true);
    appReportPreview_->setFontFamily(QStringLiteral("Consolas"));
    appReportPreview_->setPlainText(QStringLiteral("(наблюдение ещё не запускалось)"));
    appReportPreview_->setMinimumHeight(210);
    reportLayout->addLayout(reportHeader);
    appOpenReportButton_ = new QPushButton(QStringLiteral("Открыть подробный отчёт"), report);
    appOpenReportButton_->setObjectName(QStringLiteral("AppMonitorOpenReport"));
    appOpenReportButton_->setEnabled(false);
    reportLayout->addWidget(appOpenReportButton_);
    connect(appOpenReportButton_, &QPushButton::clicked, this, &MainWindow::openAppMonitorReport);
    reportLayout->addWidget(appReportPreview_);
    layout->addWidget(report);

    connect(browse, &QPushButton::clicked, this,
        [this]
        {
            const QString path
                = QFileDialog::getOpenFileName(this, QStringLiteral("Выберите проблемное приложение"),
                    QDir::homePath(), QStringLiteral("Приложения Windows (*.exe);;Все файлы (*.*)"));
            if (!path.isEmpty())
                appExecutablePath_->setText(QDir::toNativeSeparators(path));
        });
    connect(appStartButton_, &QPushButton::clicked, this, &MainWindow::startAppMonitoring);
    connect(appExecutablePath_, &QLineEdit::textChanged, this, &MainWindow::updateAppMonitorControls);
    connect(appDuration_, &QComboBox::currentIndexChanged, this, [this] {
        appSamplingHint_->setText(QStringLiteral("Интервал замеров: %1 сек. Пауза не расходует время наблюдения; приложение продолжает работать.")
            .arg(orion::diagnostics::appMonitorSampleInterval(appDuration_->currentData().toInt()), 0, 'f', 0));
    });
    appSamplingHint_->setText(QStringLiteral("Интервал замеров: 2 сек. Пауза не расходует время наблюдения; приложение продолжает работать."));
    connect(appStopButton_, &QPushButton::clicked, this,
        [this]
        {
            if (appMonitorWorker_ != nullptr && appMonitorWorker_->isRunning())
            {
                appStopButton_->setEnabled(false);
                appStatusValue_->setText(
                    QStringLiteral("Останавливаю наблюдение и дальнейшее автозакрытие…"));
                appMonitorWorker_->stopObservation();
            }
        });
    connect(appSaveJsonButton_, &QPushButton::clicked, this, [this] { saveAppMonitorReport(true); });
    connect(appSaveTextButton_, &QPushButton::clicked, this, [this] { saveAppMonitorReport(false); });
    connect(appCopyButton_, &QPushButton::clicked, this,
        [this]
        {
            QApplication::clipboard()->setText(
                latestAppMonitorReport_.value(QStringLiteral("report_text")).toString());
            appStatusValue_->setText(QStringLiteral("Отчёт скопирован в буфер обмена"));
        });

    scroll->setWidget(page);
    return scroll;
}

QWidget* MainWindow::createSettingsPage()
{
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(10);

    auto* sections = new QTabWidget(page);
    sections->setObjectName(QStringLiteral("SettingsTabs"));

    auto* appearancePage = new QWidget(sections);
    auto* appearanceLayout = new QVBoxLayout(appearancePage);
    auto* appearanceForm = new QFormLayout;
    appearanceForm->setSpacing(12);
    themeCombo_ = new QComboBox(appearancePage);
    themeCombo_->setObjectName(QStringLiteral("ThemeCombo"));
    themeCombo_->addItem(QStringLiteral("Eclipse"), QStringLiteral("eclipse"));
    themeCombo_->addItem(QStringLiteral("Quantum Cyan"), QStringLiteral("quantum_cyan"));
    themeCombo_->addItem(QStringLiteral("Matrix Terminal"), QStringLiteral("matrix_terminal"));
    themeCombo_->addItem(QStringLiteral("Slate Minimal"), QStringLiteral("slate_minimal"));
    themeCombo_->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
    const int themeIndex = themeCombo_->findData(settings_.themeKey);
    themeCombo_->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);

    auto* opacityRow = new QWidget(appearancePage);
    auto* opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    trayEnabled_ = new QCheckBox(QStringLiteral("Показывать значок в трее"), page);
    trayEnabled_->setChecked(settings_.trayIconEnabled);
    notificationsEnabled_ = new QCheckBox(QStringLiteral("Показывать системные предупреждения"), page);
    notificationsEnabled_->setChecked(settings_.trayNotificationsEnabled);
    opacitySlider_ = new QSlider(Qt::Horizontal, appearancePage);
    opacitySlider_->setObjectName(QStringLiteral("OpacitySlider"));
    opacitySlider_->setRange(50, 100);
    opacitySlider_->setValue(qRound(settings_.windowOpacity * 100.0));
    opacityValue_ = new QLabel(QStringLiteral("%1%").arg(opacitySlider_->value()), appearancePage);
    opacityValue_->setMinimumWidth(44);
    opacityLayout->addWidget(opacitySlider_, 1);
    opacityLayout->addWidget(opacityValue_);
    connect(opacitySlider_, &QSlider::valueChanged, this,
        [this](const int value) { opacityValue_->setText(QStringLiteral("%1%").arg(value)); });
    appearanceForm->addRow(QStringLiteral("Тема"), themeCombo_);
    appearanceForm->addRow(QStringLiteral("Прозрачность окна"), opacityRow);
    appearanceLayout->addLayout(appearanceForm);

    auto* customGroup = new QGroupBox(QStringLiteral("Конструктор темы Custom"), appearancePage);
    auto* customForm = new QFormLayout(customGroup);
    const auto makeColorButton = [this, customGroup](const QString& initial, const QString& dialogTitle)
    {
        auto* button = new QPushButton(customGroup);
        displayColor(button, initial);
        connect(button, &QPushButton::clicked, this,
            [button, dialogTitle]
            {
                const QColor initialColor(button->property("selectedColor").toString());
                const QColor selected = QColorDialog::getColor(initialColor, button, dialogTitle);
                if (selected.isValid())
                    displayColor(button, selected.name());
            });
        return button;
    };
    customBackground_ = makeColorButton(
        customColor(settings_.customThemeColors, QStringLiteral("bg"), QStringLiteral("#121212")),
        QStringLiteral("Фон темы"));
    customText_ = makeColorButton(
        customColor(settings_.customThemeColors, QStringLiteral("text"), QStringLiteral("#F2F2F7")),
        QStringLiteral("Цвет текста"));
    customAccent_ = makeColorButton(
        customColor(settings_.customThemeColors, QStringLiteral("accent"), QStringLiteral("#FF9F0A")),
        QStringLiteral("Цвет акцента"));
    customGraph_ = makeColorButton(
        customColor(settings_.customThemeColors, QStringLiteral("graph"), QStringLiteral("#FF9F0A")),
        QStringLiteral("Цвет графиков"));
    customBackground_->setObjectName(QStringLiteral("CustomBackgroundColor"));
    customText_->setObjectName(QStringLiteral("CustomTextColor"));
    customAccent_->setObjectName(QStringLiteral("CustomAccentColor"));
    customGraph_->setObjectName(QStringLiteral("CustomGraphColor"));
    customForm->addRow(QStringLiteral("Фон"), customBackground_);
    customForm->addRow(QStringLiteral("Текст"), customText_);
    customForm->addRow(QStringLiteral("Акцент"), customAccent_);
    customForm->addRow(QStringLiteral("Графики"), customGraph_);
    auto* customActions = new QWidget(customGroup);
    auto* customActionsLayout = new QHBoxLayout(customActions);
    customActionsLayout->setContentsMargins(0, 0, 0, 0);
    auto* saveCustomButton = new QPushButton(QStringLiteral("Сохранить Custom"), customActions);
    auto* useCustomButton = new QPushButton(QStringLiteral("Использовать сейчас"), customActions);
    customActionsLayout->addWidget(saveCustomButton);
    customActionsLayout->addWidget(useCustomButton);
    customActionsLayout->addStretch(1);
    customThemeStatus_ = new QLabel(customGroup);
    customThemeStatus_->setObjectName(QStringLiteral("SettingsNote"));
    customForm->addRow(customActions);
    customForm->addRow(customThemeStatus_);
    const auto storeCustomColors = [this]
    {
        settings_.customThemeColors = QJsonObject {
            { QStringLiteral("bg"), customBackground_->property("selectedColor").toString() },
            { QStringLiteral("text"), customText_->property("selectedColor").toString() },
            { QStringLiteral("accent"), customAccent_->property("selectedColor").toString() },
            { QStringLiteral("graph"), customGraph_->property("selectedColor").toString() },
        };
    };
    connect(saveCustomButton, &QPushButton::clicked, this,
        [this, storeCustomColors]
        {
            storeCustomColors();
            saveSettings();
            customThemeStatus_->setText(QStringLiteral("Цвета Custom сохранены"));
        });
    connect(useCustomButton, &QPushButton::clicked, this,
        [this, storeCustomColors]
        {
            storeCustomColors();
            themeCombo_->setCurrentIndex(themeCombo_->findData(QStringLiteral("custom")));
            applySettings();
            customThemeStatus_->setText(QStringLiteral("Тема Custom применена"));
        });
    appearanceLayout->addWidget(customGroup);
    appearanceLayout->addStretch(1);
    sections->addTab(appearancePage, QStringLiteral("Оформление"));

    auto* windowPage = new QWidget(sections);
    auto* windowLayout = new QVBoxLayout(windowPage);
    auto* windowForm = new QFormLayout;
    minimumWidth_ = new QSpinBox(windowPage);
    minimumHeight_ = new QSpinBox(windowPage);
    maximumWidth_ = new QSpinBox(windowPage);
    maximumHeight_ = new QSpinBox(windowPage);
    minimumWidth_->setRange(400, 3000);
    minimumHeight_->setRange(300, 3000);
    maximumWidth_->setRange(400, 4000);
    maximumHeight_->setRange(300, 4000);
    minimumWidth_->setValue(settings_.minimumWidth);
    minimumHeight_->setValue(settings_.minimumHeight);
    maximumWidth_->setValue(settings_.maximumWidth);
    maximumHeight_->setValue(settings_.maximumHeight);
    minimumWidth_->setObjectName(QStringLiteral("MinimumWindowWidth"));
    minimumHeight_->setObjectName(QStringLiteral("MinimumWindowHeight"));
    maximumWidth_->setObjectName(QStringLiteral("MaximumWindowWidth"));
    maximumHeight_->setObjectName(QStringLiteral("MaximumWindowHeight"));
    alwaysOnTop_ = new QCheckBox(QStringLiteral("Окно поверх остальных"), windowPage);
    alwaysOnTop_->setChecked(settings_.alwaysOnTop);
    freeFormResize_ = new QCheckBox(QStringLiteral("Свободное изменение размера"), windowPage);
    freeFormResize_->setObjectName(QStringLiteral("FreeFormResize"));
    freeFormResize_->setChecked(settings_.freeFormResize);
    windowForm->addRow(QStringLiteral("Минимальная ширина"), minimumWidth_);
    windowForm->addRow(QStringLiteral("Минимальная высота"), minimumHeight_);
    windowForm->addRow(QStringLiteral("Максимальная ширина"), maximumWidth_);
    windowForm->addRow(QStringLiteral("Максимальная высота"), maximumHeight_);
    windowLayout->addLayout(windowForm);
    windowLayout->addWidget(alwaysOnTop_);
    windowLayout->addWidget(freeFormResize_);
    auto* resizeNote = new QLabel(
        QStringLiteral("Свободный режим снимает ограничения минимального и максимального размера."),
        windowPage);
    resizeNote->setObjectName(QStringLiteral("SettingsNote"));
    resizeNote->setWordWrap(true);
    windowLayout->addWidget(resizeNote);
    windowLayout->addStretch(1);
    sections->addTab(windowPage, QStringLiteral("Окно"));

    auto* cardsPage = new QWidget(sections);
    auto* cardsLayout = new QVBoxLayout(cardsPage);
    cardCpuVisible_ = new QCheckBox(QStringLiteral("CPU"), cardsPage);
    cardGpuVisible_ = new QCheckBox(QStringLiteral("GPU"), cardsPage);
    cardRamVisible_ = new QCheckBox(QStringLiteral("RAM"), cardsPage);
    cardDiskVisible_ = new QCheckBox(QStringLiteral("Хранилище"), cardsPage);
    cardNetVisible_ = new QCheckBox(QStringLiteral("Сеть"), cardsPage);
    cardCpuVisible_->setObjectName(QStringLiteral("CardCpuVisible"));
    cardGpuVisible_->setObjectName(QStringLiteral("CardGpuVisible"));
    cardRamVisible_->setObjectName(QStringLiteral("CardRamVisible"));
    cardDiskVisible_->setObjectName(QStringLiteral("CardDiskVisible"));
    cardNetVisible_->setObjectName(QStringLiteral("CardNetVisible"));
    cardCpuVisible_->setChecked(settings_.cardCpuVisible);
    cardGpuVisible_->setChecked(settings_.cardGpuVisible);
    cardRamVisible_->setChecked(settings_.cardRamVisible);
    cardDiskVisible_->setChecked(settings_.cardDiskVisible);
    cardNetVisible_->setChecked(settings_.cardNetVisible);
    for (auto* check :
        { cardCpuVisible_, cardGpuVisible_, cardRamVisible_, cardDiskVisible_, cardNetVisible_ })
        cardsLayout->addWidget(check);
    cardDragDropEnabled_
        = new QCheckBox(QStringLiteral("Разрешить перетаскивание и вынос карточек Обзора"), cardsPage);
    cardDragDropEnabled_->setObjectName(QStringLiteral("CardDragDropEnabled"));
    cardDragDropEnabled_->setChecked(settings_.cardDragDropEnabled);
    cardsLayout->addSpacing(8);
    cardsLayout->addWidget(cardDragDropEnabled_);
    auto* resetCardsButton = new QPushButton(QStringLiteral("Сбросить раскладку карточек"), cardsPage);
    resetCardsButton->setObjectName(QStringLiteral("ResetOverviewLayoutButton"));
    connect(resetCardsButton, &QPushButton::clicked, this,
        [this]
        {
            resetOverviewLayout();
            saveSettings();
            collectionStatus_->setText(QStringLiteral("Раскладка карточек сброшена"));
        });
    cardsLayout->addWidget(resetCardsButton, 0, Qt::AlignLeft);
    cardsLayout->addSpacing(16);
    auto* diagnosticLayoutNote = new QLabel(QStringLiteral(
        "Панели диагностики: расположение сохраняется между запусками. При полном выходе "
        "вынесенные панели возвращаются внутрь окна; сворачивание их не затрагивает."), cardsPage);
    diagnosticLayoutNote->setWordWrap(true);
    diagnosticLayoutNote->setObjectName(QStringLiteral("SettingsNote"));
    cardsLayout->addWidget(diagnosticLayoutNote);
    auto* resetDiagnosticButton = new QPushButton(QStringLiteral("Сбросить панели диагностики"), cardsPage);
    resetDiagnosticButton->setObjectName(QStringLiteral("ResetDiagnosticLayoutButton"));
    resetDiagnosticButton->setToolTip(QStringLiteral(
        "Возвращает три панели диагностики в исходное положение. Отчёты и карточки Обзора сохраняются."));
    connect(resetDiagnosticButton, &QPushButton::clicked, this, [this] {
        resetDiagnosticLayout();
        saveSettings();
        collectionStatus_->setText(QStringLiteral("Раскладка панелей диагностики сброшена"));
    });
    cardsLayout->addWidget(resetDiagnosticButton, 0, Qt::AlignLeft);
    cardsLayout->addStretch(1);
    sections->addTab(cardsPage, QStringLiteral("Карточки и панели"));

    auto* advancedPage = new QWidget(sections);
    auto* advancedLayout = new QVBoxLayout(advancedPage);
    terminalEnabled_ = new QCheckBox(QStringLiteral("Вкладка Терминал"), advancedPage);
    serverTabEnabled_ = new QCheckBox(QStringLiteral("Вкладка Серверы"), advancedPage);
    gamerMode_ = new QCheckBox(QStringLiteral("Геймерский режим"), advancedPage);
    terminalEnabled_->setObjectName(QStringLiteral("TerminalEnabled"));
    serverTabEnabled_->setObjectName(QStringLiteral("ServerTabEnabled"));
    gamerMode_->setObjectName(QStringLiteral("GamerMode"));
    terminalEnabled_->setChecked(settings_.terminalEnabled);
    serverTabEnabled_->setChecked(settings_.serverTabEnabled);
    gamerMode_->setChecked(gamerModeActive_);
    terminalEnabled_->setToolTip(
        QStringLiteral("Запускает встроенную системную оболочку; при выключении процесс завершается"));
    serverTabEnabled_->setToolTip(
        QStringLiteral("Профили и реальные TCP-проверки доступности с таймаутом 2 секунды"));
    gamerMode_->setToolTip(
        QStringLiteral("Скрывает главное окно и открывает компактный always-on-top оверлей. "
                       "FPS означает частоту отрисовки интерфейса O.R.I.O.N., не FPS игры."));
    alertFreezeDisabled_ = new QCheckBox(
        QStringLiteral("Не удерживать предупреждение после нормализации (5 с)"), advancedPage);
    alertFreezeDisabled_->setObjectName(QStringLiteral("AlertFreezeDisabled"));
    alertFreezeDisabled_->setChecked(settings_.alertFreezeDisabled);
    advancedLayout->addWidget(terminalEnabled_);
    advancedLayout->addWidget(serverTabEnabled_);
    advancedLayout->addWidget(gamerMode_);
    advancedLayout->addWidget(alertFreezeDisabled_);
    advancedLayout->addSpacing(8);
    advancedLayout->addWidget(trayEnabled_);
    advancedLayout->addWidget(notificationsEnabled_);
    auto* advancedForm = new QFormLayout;
    pingTarget_ = new QLineEdit(settings_.pingTarget, advancedPage);
    pingTarget_->setObjectName(QStringLiteral("PingTarget"));
    pingTarget_->setClearButtonEnabled(true);
    pingTarget_->setPlaceholderText(QStringLiteral("8.8.8.8 или dns.example.com"));
    pingTarget_->setToolTip(
        QStringLiteral("TCP-подключение к порту 53, таймаут 1 секунда; измерение выполняется в фоне"));
    advancedForm->addRow(QStringLiteral("Цель ping"), pingTarget_);
    advancedLayout->addLayout(advancedForm);
    auto* pendingNote
        = new QLabel(QStringLiteral("Terminal, Servers, Gamer Mode и цель ping применяются без перезапуска. "
                                    "Геймерский режим действует только в текущей сессии и не включается "
                                    "автоматически при следующем запуске."),
            advancedPage);
    pendingNote->setWordWrap(true);
    pendingNote->setObjectName(QStringLiteral("SettingsNote"));
    advancedLayout->addWidget(pendingNote);
    advancedLayout->addStretch(1);
    sections->addTab(advancedPage, QStringLiteral("Дополнительно"));

    outer->addWidget(sections, 1);
    auto* note = new QLabel(QStringLiteral("Изменения применяются сразу и сохраняются в AppData."), page);
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("SettingsNote"));
    auto* saveButton = new QPushButton(QStringLiteral("Применить и сохранить"), page);
    saveButton->setObjectName(QStringLiteral("SettingsApplyButton"));
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::applySettings);
    outer->addWidget(note);
    outer->addWidget(saveButton, 0, Qt::AlignLeft);
    return page;
}

void MainWindow::applyTelemetry(const QString& backend, const QString& operatingSystem,
    const QString& cpuName, const QString& gpuName, const double cpuPercent, const QVector<double>& cpuCores,
    const QVector<double>& cpuCoreFrequenciesMhz, const double cpuFrequencyMhz, const double cpuTemperatureC,
    const double gpuPercent, const double gpuTemperatureC, const double gpuMemoryTotalGiB,
    const double gpuMemoryUsedGiB, const double gpuMemoryPercent, const double ramPercent,
    const double ramTotalGiB, const RuntimeTelemetry& runtime, const QVector<DiskTelemetry>& disks,
    const QVector<TemperatureTelemetry>& temperatures, const QVector<FanTelemetry>& fans,
    const double downloadBytesPerSecond, const double uploadBytesPerSecond)
{
    // A telemetry signal may already be queued when Pause is clicked.  Do not
    // let that stale delivery extend the CPU history or revive live values.
    if (paused_)
        return;

    latestBackend_ = backend;
    latestOperatingSystem_ = operatingSystem;
    latestCpuName_ = cpuName;
    latestGpuName_ = gpuName;
    latestCpuPercent_ = cpuPercent;
    latestCpuCores_ = cpuCores;
    latestCpuCoreFrequenciesMhz_ = cpuCoreFrequenciesMhz;
    latestCpuFrequencyMhz_ = cpuFrequencyMhz;
    latestCpuTemperatureC_ = cpuTemperatureC;
    latestGpuPercent_ = gpuPercent;
    latestGpuTemperatureC_ = gpuTemperatureC;
    latestGpuMemoryTotalGiB_ = gpuMemoryTotalGiB;
    latestGpuMemoryUsedGiB_ = gpuMemoryUsedGiB;
    latestGpuMemoryPercent_ = gpuMemoryPercent;
    latestRamPercent_ = ramPercent;
    latestRamTotalGiB_ = ramTotalGiB;
    latestRuntime_ = runtime;
    latestDisks_ = disks;
    latestTemperatures_ = temperatures;
    latestFans_ = fans;
    latestDownloadBytesPerSecond_ = downloadBytesPerSecond;
    latestUploadBytesPerSecond_ = uploadBytesPerSecond;
    updateDiagnosticTemperatureCards(cpuTemperatureC, gpuTemperatureC);
    // Keep network history while its page is hidden, including Gamer Mode.
    networkTrafficChart_->appendSample(sessionTimer_.elapsed(), downloadBytesPerSecond, uploadBytesPerSecond);
    telemetryReceived_ = true;
    if (hardwareScanPending_)
        startHardwareScan();

    const double cpuCriticalTemperature = orion::core::temperatureLimits("cpu").second;
    const double gpuCriticalTemperature = orion::core::temperatureLimits("gpu").second;
    cpuCriticalState_
        = cpuPercent >= orion::core::kCpuCriticalPercent || cpuTemperatureC >= cpuCriticalTemperature;
    gpuCriticalState_
        = gpuPercent >= orion::core::kGpuCriticalPercent || gpuTemperatureC >= gpuCriticalTemperature;
    ramCriticalState_ = ramPercent >= orion::core::kRamCriticalPercent;
    if (gamerOverlay_ != nullptr)
    {
        gamerOverlay_->setCriticalMetrics(cpuCriticalState_, gpuCriticalState_, ramCriticalState_);
        gamerOverlay_->setAlarmState(cpuCriticalState_ || gpuCriticalState_ || ramCriticalState_);
        gamerOverlay_->updateTelemetry(
            cpuPercent, gpuPercent, ramPercent, downloadBytesPerSecond, uploadBytesPerSecond);
    }

    orion::core::TelemetryData peakSample;
    const auto collectedAt = std::chrono::system_clock::now();
    if (cpuPercent >= 0.0)
        peakSample.cpuUsagePercent
            = orion::core::Metric<double>::valid(cpuPercent, "native UI telemetry", collectedAt);
    if (ramPercent >= 0.0)
        peakSample.ramUsagePercent
            = orion::core::Metric<double>::valid(ramPercent, "native UI telemetry", collectedAt);
    if (gpuPercent >= 0.0)
        peakSample.gpuUsagePercent
            = orion::core::Metric<double>::valid(gpuPercent, "native UI telemetry", collectedAt);
    if (cpuTemperatureC >= 0.0)
        peakSample.cpuTemperatureC
            = orion::core::Metric<double>::valid(cpuTemperatureC, "native UI telemetry", collectedAt);
    if (gpuTemperatureC >= 0.0)
        peakSample.gpuTemperatureC
            = orion::core::Metric<double>::valid(gpuTemperatureC, "native UI telemetry", collectedAt);
    sessionPeaks_.update(peakSample);
    // Match Python: per-core history is recorded even while Gamer Mode hides the main window.
    cpuCoreChart_->updateCores(cpuCores);
    if (deepTelemetryDialog_ != nullptr)
    {
        deepTelemetryDialog_->applyTelemetry(cpuCores, cpuCoreFrequenciesMhz, cpuFrequencyMhz,
            sessionPeaks_.uptimeSeconds(), sessionPeaks_.peaks());
    }

    const bool floatingOverviewVisible = std::any_of(overviewDocks_.cbegin(), overviewDocks_.cend(),
        [](const QDockWidget* dock) { return dock && dock->isFloating() && !dock->isHidden(); });
    if (!gamerModeActive_ || floatingOverviewVisible)
    {
        cpuCard_->setPercent(cpuPercent);
        cpuCard_->setDetailText(QStringLiteral("Температура: %2   ·   Частота: %1")
                .arg(cpuFrequencyMhz >= 0.0
                        ? QStringLiteral("%1 ГГц").arg(cpuFrequencyMhz / 1000.0, 0, 'f', 2)
                        : QStringLiteral("н/д"))
                .arg(cpuTemperatureC >= 0.0 ? QStringLiteral("%1°C").arg(cpuTemperatureC, 0, 'f', 0)
                                            : QStringLiteral("н/д")));

        const auto gpuTemperatureText = gpuTemperatureC >= 0.0
            ? QStringLiteral("%1°C").arg(gpuTemperatureC, 0, 'f', 0)
            : QStringLiteral("н/д");
        const auto gpuVramText = gpuMemoryTotalGiB >= 0.0 ? QStringLiteral("%1 / %2 ГБ")
                                                                .arg(qMax(gpuMemoryUsedGiB, 0.0), 0, 'f', 1)
                                                                .arg(gpuMemoryTotalGiB, 0, 'f', 1)
                                                          : QStringLiteral("н/д");
        if (gpuPercent >= 0.0)
        {
            gpuCard_->setPercent(gpuPercent);
            gpuCard_->setDetailText(
                QStringLiteral("Температура: %1   ·   VRAM: %2").arg(gpuTemperatureText, gpuVramText));
        }
        else
        {
            gpuCard_->setUnavailable(
                QStringLiteral("%1 · VRAM: %2 · загрузка/температура недоступны").arg(gpuName, gpuVramText));
        }

        ramCard_->setPercent(ramPercent);
        ramCard_->setDetailText(ramTotalGiB > 0.0 && ramPercent >= 0.0
                ? QStringLiteral("Занято: %1 / %2 ГБ")
                      .arg(ramTotalGiB * ramPercent / 100.0, 0, 'f', 1)
                      .arg(ramTotalGiB, 0, 'f', 1)
                : QStringLiteral("Общий объём: н/д"));

        const auto colors = paletteFor(settings_);
        const auto statusColor = [&colors](double usage, double temperature, double critical)
        {
            using namespace orion::core;
            const auto valid = [](double value) -> std::optional<double>
            { return std::isfinite(value) && value >= 0 ? std::optional<double>(value) : std::nullopt; };
            const auto status
                = worse(levelForPercent(valid(usage), critical), levelForTemperature(valid(temperature)));
            return status == StatusLevel::Critical ? QColor(QStringLiteral("#FF3B30"))
                : status == StatusLevel::Warning   ? QColor(colors.accent)
                : status == StatusLevel::Ok        ? QColor(QStringLiteral("#2ECC71"))
                                                   : QColor(colors.muted);
        };
        cpuCard_->setStatusColor(statusColor(cpuPercent, cpuTemperatureC, orion::core::kCpuCriticalPercent));
        gpuCard_->setStatusColor(statusColor(gpuPercent, gpuTemperatureC, orion::core::kGpuCriticalPercent));
        ramCard_->setStatusColor(statusColor(ramPercent, -1, orion::core::kRamCriticalPercent));

        if (disks.isEmpty())
        {
            diskCard_->setUnavailable(QStringLiteral("Накопители не обнаружены или недоступны"));
            diskCard_->setBadge({});
        }
        else
        {
            const auto& mainDisk = disks.front();
            double totalRead = 0.0;
            double totalWrite = 0.0;
            bool hasRead = false;
            bool hasWrite = false;
            for (const auto& disk : disks)
            {
                if (disk.readMiBPerSecond >= 0.0)
                {
                    totalRead += disk.readMiBPerSecond;
                    hasRead = true;
                }
                if (disk.writeMiBPerSecond >= 0.0)
                {
                    totalWrite += disk.writeMiBPerSecond;
                    hasWrite = true;
                }
            }
            diskCard_->setTextValue(QStringLiteral("%1%").arg(mainDisk.usedPercent, 0, 'f', 1),
                QStringLiteral("↓%1 ↑%2 МБ/с   ·   Занято: %3 / %4 ГБ")
                    .arg(hasRead ? QString::number(totalRead, 'f', 1) : QStringLiteral("н/д"))
                    .arg(hasWrite ? QString::number(totalWrite, 'f', 1) : QStringLiteral("н/д"))
                    .arg(mainDisk.usedGiB, 0, 'f', 1)
                    .arg(mainDisk.totalGiB, 0, 'f', 1));
            diskCard_->setBadge(
                mainDisk.storageType.isEmpty() ? QStringLiteral("н/д") : mainDisk.storageType);
            diskCard_->pushHistory(mainDisk.usedPercent);
            const double freePercent = 100.0 - mainDisk.usedPercent;
            diskCard_->setStatusColor(QColor(freePercent < 5.0 ? QStringLiteral("#FF3B30")
                    : freePercent < 15.0                       ? QStringLiteral("#F4C542")
                                                               : QStringLiteral("#2ECC71")));
        }

        if (downloadBytesPerSecond >= 0.0)
        {
            networkCard_->pushHistory(downloadBytesPerSecond / (1024.0 * 1024.0));
        }
        const qint64 uptimeSeconds = sessionTimer_.elapsed() / 1000;
        collectionStatus_->setText(QStringLiteral("%1 · %2:%3")
                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
                .arg(uptimeSeconds / 60, 2, 10, QLatin1Char('0'))
                .arg(uptimeSeconds % 60, 2, 10, QLatin1Char('0')));

        DetailsSnapshot details;
        details.operatingSystem = operatingSystem == QStringLiteral("Windows")
            ? QStringLiteral("%1 · %2").arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion())
            : operatingSystem;
        details.cpuName = cpuName;
        details.gpuName = gpuName;
        details.cpuPercent = cpuPercent;
        details.cpuFrequencyMhz = cpuFrequencyMhz;
        details.cpuTemperatureC = cpuTemperatureC;
        details.logicalCpus = cpuCores.size();
        details.gpuPercent = gpuPercent;
        details.gpuTemperatureC = gpuTemperatureC;
        details.gpuMemoryTotalGiB = gpuMemoryTotalGiB;
        details.gpuMemoryUsedGiB = gpuMemoryUsedGiB;
        details.gpuMemoryPercent = gpuMemoryPercent;
        details.ramPercent = ramPercent;
        details.ramTotalGiB = ramTotalGiB;
        details.disks = disks;
        details.temperatures = temperatures;
        details.fans = fans;
        detailsPanel_->updateSnapshot(details, sessionPeaks_.peaks());
    }
    updateNetworkPresentation();

    const bool alarmActive = cpuCriticalState_ || gpuCriticalState_ || ramCriticalState_;
    if (alarmActive)
    {
        alarmClearTimer_->stop();
        QStringList reasons;
        if (cpuCriticalState_)
            reasons.append(QStringLiteral("CPU"));
        if (gpuCriticalState_)
            reasons.append(QStringLiteral("GPU"));
        if (ramCriticalState_)
            reasons.append(QStringLiteral("RAM"));
        alarmBanner_->setText(
            QStringLiteral("Критическое состояние: %1").arg(reasons.join(QStringLiteral(", "))));
        alarmBanner_->show();
    }
    else if (settings_.alertFreezeDisabled)
    {
        alarmClearTimer_->stop();
        alarmBanner_->hide();
    }
    else if (alarmBanner_->isVisible() && !alarmClearTimer_->isActive())
    {
        alarmClearTimer_->start();
    }
    maybeNotify(cpuPercent, ramPercent);

    if (appMonitorWorker_ != nullptr)
        appMonitorWorker_->updateSystemSample(runtime.appMonitorSystemEnvelope);
    QElapsedTimer receivedClock;
    receivedClock.start();
    const QJsonObject ping = latestPing_.target == settings_.pingTarget ? QJsonObject{
        {"value", latestPing_.usable() ? QJsonValue(latestPing_.latencyMs) : QJsonValue(QJsonValue::Null)},
        {"quality", latestPing_.quality}, {"source", latestPing_.source},
        {"reason", latestPing_.reason}, {"observed_at", latestPing_.observedAt},
        {"target", latestPing_.target}, {"observed_monotonic_ms", latestPing_.observedMonotonicMs}} : QJsonObject{};
    const auto runtimeSample = incidentSampleBuilder_.build(runtime.incidentSystemEnvelope,
        receivedClock.msecsSinceReference(), sessionTimer_.msecsSinceReference(), incidentNotBeforeMs_,
        ping, appMonitorWorker_ && appMonitorWorker_->isRunning() ? latestAppLiveSample_ : QJsonObject{});
    if (runtimeSample.isEmpty()) return; // Never timestamp a stale queued frame as a new observation.
    const double monotonic = runtimeSample.value("monotonic").toDouble();
    runtimeSamples_.append(runtimeSample);
    while (!runtimeSamples_.isEmpty()
        && (runtimeSamples_.size() > 1200
            || runtimeSamples_.front().value(QStringLiteral("monotonic")).toDouble() < monotonic - 300.0))
    {
        runtimeSamples_.removeFirst();
    }
}

void MainWindow::applyPingTelemetry(const PingTelemetry& sample)
{
    latestPing_ = sample;
    updateNetworkPresentation();
}

void MainWindow::updateNetworkPresentation()
{
    if (networkCard_ == nullptr || networkDownloadValue_ == nullptr || networkUploadValue_ == nullptr
        || networkPingValue_ == nullptr || networkStateCard_ == nullptr)
    {
        return;
    }

    const QString download = formatRate(latestDownloadBytesPerSecond_);
    const QString upload = formatRate(latestUploadBytesPerSecond_);
    const QString target = latestPing_.target.isEmpty() ? settings_.pingTarget : latestPing_.target;
    const QString ping = latestPing_.usable() ? QStringLiteral("%1 мс").arg(latestPing_.latencyMs, 0, 'f', 1)
                                              : QStringLiteral("н/д");

    networkCard_->setTextValue(
        QStringLiteral("↓ %1   ↑ %2").arg(download, upload), QStringLiteral("Пинг: %1").arg(ping));
    networkCard_->setToolTip(QStringLiteral("Цель: %1 · источник: %2").arg(target, latestBackend_));
    if (latestDownloadBytesPerSecond_ >= 0.0)
    {
        networkCard_->setStatusColor(QColor(QStringLiteral("#2ECC71")));
    }

    networkDownloadValue_->setText(download);
    networkUploadValue_->setText(upload);
    networkPingValue_->setText(ping);
    networkPingValue_->setToolTip(latestPing_.usable()
            ? QStringLiteral("Цель: %1 · TCP порт %2 · %3")
                  .arg(target)
                  .arg(latestPing_.port)
                  .arg(latestPing_.observedAt)
            : QStringLiteral("Цель: %1 · качество: %2 · %3")
                  .arg(target, latestPing_.quality, latestPing_.reason));

    const auto fresh = [](const orion::core::Metric<std::uint64_t>& metric)
    { return metric.usable() && metric.quality != orion::core::DataQuality::Stale; };
    const auto count = [&fresh](const orion::core::Metric<std::uint64_t>& metric)
    {
        return fresh(metric) ? QString::number(static_cast<qulonglong>(*metric.value))
                             : QStringLiteral("н/д");
    };
    const auto volume = [&fresh](const orion::core::Metric<std::uint64_t>& metric)
    {
        return fresh(metric)
            ? QStringLiteral("%1 ГБ").arg(static_cast<double>(*metric.value) / 1073741824.0, 0, 'f', 2)
            : QStringLiteral("н/д");
    };
    const auto provenance = [](const orion::core::Metric<std::uint64_t>& metric)
    {
        return QStringLiteral("Источник: %1 · качество: %2\n%3")
            .arg(QString::fromStdString(metric.source),
                QString::fromUtf8(orion::core::toString(metric.quality).data()),
                QString::fromStdString(metric.reason));
    };
    const auto& counters = latestRuntime_.network;
    networkReceivedValue_->setText(volume(counters.receivedBytes));
    networkSentValue_->setText(volume(counters.sentBytes));
    const QString totalNote
        = QStringLiteral("Счётчик ОС по системе, не за сеанс ORION. "
                         "Может сброситься при перезапуске ОС/адаптера или изменении набора интерфейсов. "
                         "1 ГБ = 1024³ байт.\n");
    networkReceivedValue_->setToolTip(totalNote + provenance(counters.receivedBytes));
    networkSentValue_->setToolTip(totalNote + provenance(counters.sentBytes));
    networkErrorsValue_->setText(
        QStringLiteral("%1 (всего: %2)").arg(count(counters.intervalErrors), count(counters.errors)));
    networkDropsValue_->setText(
        QStringLiteral("%1 (всего: %2)").arg(count(counters.intervalDrops), count(counters.drops)));
    const QString intervalNote
        = QStringLiteral("До скобок — число событий между последовательными замерами, "
                         "не в секунду; в скобках — счётчик ОС. После паузы, пропуска или сброса "
                         "первый интервал недоступен.\n");
    networkErrorsValue_->setToolTip(intervalNote + provenance(counters.intervalErrors));
    networkDropsValue_->setToolTip(intervalNote
        + QStringLiteral("Сумма отброшенных пакетов интерфейсов; "
                         "это не процент потерь ping.\n")
        + provenance(counters.intervalDrops));
}

void MainWindow::refreshNetworkInterfaces()
{
    if (networkInterfaceCombo_ == nullptr || networkScannerWorker_ == nullptr
        || networkScannerWorker_->isRunning())
    {
        return;
    }
    QString previousAddress;
    const int previousIndex = networkInterfaceCombo_->currentIndex();
    if (previousIndex >= 0 && previousIndex < networkInterfaces_.size())
    {
        previousAddress = networkInterfaces_.at(previousIndex).address;
    }

    networkInterfaces_ = NetworkScannerWorker::listIpv4Interfaces();
    networkInterfaceCombo_->blockSignals(true);
    networkInterfaceCombo_->clear();
    int selected = 0;
    for (int index = 0; index < networkInterfaces_.size(); ++index)
    {
        const auto& interfaceInfo = networkInterfaces_.at(index);
        networkInterfaceCombo_->addItem(interfaceInfo.label());
        if (interfaceInfo.address == previousAddress)
            selected = index;
    }
    if (!networkInterfaces_.isEmpty())
        networkInterfaceCombo_->setCurrentIndex(selected);
    networkInterfaceCombo_->blockSignals(false);

    if (networkInterfaces_.isEmpty())
    {
        networkLocalIpValue_->setText(QStringLiteral("н/д"));
        networkMacValue_->setText(QStringLiteral("н/д"));
        networkScopeValue_->setText(QStringLiteral("н/д"));
        networkScanStatus_->setText(QStringLiteral("Нет активного IPv4-адаптера"));
        restoreNetworkScanControls();
        return;
    }
    updateSelectedNetworkInterface();
    restoreNetworkScanControls();
}

void MainWindow::updateSelectedNetworkInterface()
{
    if (networkInterfaceCombo_ == nullptr || networkScannerWorker_ == nullptr
        || networkScannerWorker_->isRunning())
    {
        return;
    }
    const int index = networkInterfaceCombo_->currentIndex();
    if (index < 0 || index >= networkInterfaces_.size())
        return;
    const auto& interfaceInfo = networkInterfaces_.at(index);
    bool truncated = false;
    QString network;
    QString originalNetwork;
    const QStringList targets
        = NetworkScannerWorker::scanTargetsFor(interfaceInfo, &truncated, &network, &originalNetwork);
    networkLocalIpValue_->setText(interfaceInfo.address);
    networkMacValue_->setText(interfaceInfo.mac.isEmpty() ? QStringLiteral("н/д") : interfaceInfo.mac);
    networkScopeValue_->setText(QStringLiteral("Подсеть: %1").arg(network));
    networkScanStatus_->setText(QStringLiteral("Готово: %1; адресов для проверки — %2%3")
            .arg(interfaceInfo.displayName.isEmpty() ? interfaceInfo.name : interfaceInfo.displayName)
            .arg(targets.size())
            .arg(truncated
                    ? QStringLiteral(" (исходная сеть %1 ограничена локальным /24)").arg(originalNetwork)
                    : QString()));
}

void MainWindow::toggleNetworkScan()
{
    if (networkScannerWorker_ != nullptr && networkScannerWorker_->isRunning())
    {
        stopNetworkScan(false);
        return;
    }
    startNetworkScan();
}

void MainWindow::startNetworkScan()
{
    if (paused_)
    {
        networkScanStatus_->setText(QStringLiteral("Мониторинг приостановлен"));
        return;
    }
    const int index = networkInterfaceCombo_->currentIndex();
    if (networkScannerWorker_ == nullptr || index < 0 || index >= networkInterfaces_.size())
    {
        networkScanStatus_->setText(QStringLiteral("Сначала выберите активный IPv4-адаптер"));
        return;
    }
    const auto interfaceInfo = networkInterfaces_.at(index);
    if (!networkScannerWorker_->setInterface(interfaceInfo))
    {
        networkScanStatus_->setText(QStringLiteral("Сканер ещё завершает предыдущую операцию"));
        return;
    }

    networkDeviceTable_->setSortingEnabled(false);
    networkDeviceTable_->setRowCount(0);
    networkScanProgress_->setValue(0);
    networkScanProgress_->show();
    networkScanButton_->setText(QStringLiteral("Остановить"));
    networkScanButton_->setEnabled(true);
    networkInterfaceCombo_->setEnabled(false);
    networkRefreshButton_->setEnabled(false);
    networkScanStatus_->setText(QStringLiteral("Поиск устройств через %1 (%2/%3)…")
            .arg(interfaceInfo.displayName.isEmpty() ? interfaceInfo.name : interfaceInfo.displayName,
                interfaceInfo.address)
            .arg(interfaceInfo.prefixLength));
    networkScannerWorker_->start();
    if (!settings_.freeFormResize)
        networkSplitter_->setSizes({ 210, 620 });
}

void MainWindow::stopNetworkScan(const bool waitForFinish)
{
    if (networkScannerWorker_ == nullptr || !networkScannerWorker_->isRunning())
        return;
    networkScannerWorker_->requestStop();
    networkScanButton_->setEnabled(false);
    networkScanButton_->setText(QStringLiteral("Останавливаем…"));
    networkScanStatus_->setText(QStringLiteral("Остановка после текущей короткой сетевой пробы…"));
    if (waitForFinish && !networkScannerWorker_->wait(3000))
    {
        networkScanStatus_->setText(QStringLiteral("Сканер не завершился за отведённые 3 секунды"));
    }
}

void MainWindow::upsertNetworkDevice(const NetworkDevice& device)
{
    if (networkDeviceTable_ == nullptr || device.ip.isEmpty())
        return;
    int row = -1;
    for (int candidate = 0; candidate < networkDeviceTable_->rowCount(); ++candidate)
    {
        const auto* item = networkDeviceTable_->item(candidate, 0);
        if (item != nullptr && item->text() == device.ip)
        {
            row = candidate;
            break;
        }
    }
    if (row < 0)
    {
        row = networkDeviceTable_->rowCount();
        networkDeviceTable_->insertRow(row);
    }

    const QString evidence = device.identificationEvidence.isEmpty()
        ? QStringLiteral("Источник идентификации не опубликован")
        : device.identificationEvidence.join(QStringLiteral("; "));
    QStringList tooltipLines {
        QStringLiteral("Уверенность: %1")
            .arg(device.confidence.isEmpty() ? QStringLiteral("низкая") : device.confidence),
    };
    if (!device.nameSource.isEmpty())
    {
        tooltipLines.push_back(QStringLiteral("Источник имени: %1").arg(device.nameSource));
    }
    if (!device.services.isEmpty())
    {
        tooltipLines.push_back(QStringLiteral("Службы: %1").arg(device.services.join(QStringLiteral(", "))));
    }
    tooltipLines.push_back(evidence);
    const QString tooltip = tooltipLines.join(QLatin1Char('\n'));
    const QStringList values { device.ip, device.mac.isEmpty() ? QStringLiteral("н/д") : device.mac,
        device.name.isEmpty() ? QStringLiteral("н/д") : device.name,
        device.typeOrVendor.isEmpty() ? QStringLiteral("Неизвестное устройство") : device.typeOrVendor,
        device.latencyMs >= 0.0 ? QStringLiteral("%1 мс").arg(device.latencyMs, 0, 'f', 0)
                                : QStringLiteral("н/д") };
    for (int column = 0; column < values.size(); ++column)
    {
        auto* item = networkDeviceTable_->item(row, column);
        if (item == nullptr)
        {
            item = new QTableWidgetItem;
            networkDeviceTable_->setItem(row, column, item);
        }
        item->setText(values.at(column));
        item->setToolTip(tooltip);
    }
}

void MainWindow::finishNetworkScan(const NetworkScanResult& result)
{
    if (!settings_.freeFormResize)
        networkSplitter_->setSizes({ 360, 470 });
    networkDeviceTable_->setRowCount(0);
    for (const NetworkDevice& device : result.devices)
        upsertNetworkDevice(device);
    networkDeviceTable_->setSortingEnabled(true);
    networkScanProgress_->setValue(result.cancelled ? networkScanProgress_->value() : 100);
    const double elapsedSeconds = static_cast<double>(result.elapsedMs) / 1000.0;
    if (result.cancelled)
    {
        networkScanStatus_->setText(QStringLiteral("Сканирование остановлено за %1 с. Уже найдено: %2")
                .arg(elapsedSeconds, 0, 'f', 2)
                .arg(result.devices.size()));
        return;
    }
    QString status = QStringLiteral("Готово за %1 с. Найдено: %2, распознано имён: %3. Просканировано: %4.")
                         .arg(elapsedSeconds, 0, 'f', 2)
                         .arg(result.devices.size())
                         .arg(result.identified)
                         .arg(result.network);
    if (result.identityLimited)
    {
        status += QStringLiteral(" Расширенная идентификация ограничена первыми %1 устройствами.")
                      .arg(result.identityLimit);
    }
    if (result.truncated)
    {
        status
            += QStringLiteral(" Исходная сеть %1 ограничена локальным /24 для безопасного времени проверки.")
                   .arg(result.originalNetwork);
    }
    networkScanStatus_->setText(status);
}

void MainWindow::failNetworkScan(const QString& error)
{
    if (!settings_.freeFormResize)
        networkSplitter_->setSizes({ 360, 470 });
    networkScanProgress_->setValue(100);
    networkScanStatus_->setText(QStringLiteral("Ошибка сканирования: %1").arg(error));
}

void MainWindow::restoreNetworkScanControls()
{
    const bool running = networkScannerWorker_ != nullptr && networkScannerWorker_->isRunning();
    const bool enabled = !paused_ && !running && !networkInterfaces_.isEmpty();
    networkScanButton_->setText(running ? QStringLiteral("Остановить") : QStringLiteral("Сканировать сеть"));
    networkScanButton_->setEnabled(running ? !paused_ : enabled);
    networkInterfaceCombo_->setEnabled(enabled);
    networkRefreshButton_->setEnabled(!paused_ && !running);
    if (!running)
    {
        QTimer::singleShot(1200, this,
            [this]
            {
                if (networkScannerWorker_ != nullptr && !networkScannerWorker_->isRunning())
                {
                    networkScanProgress_->hide();
                }
            });
    }
}

void MainWindow::startPublicIpLookup()
{
    if (paused_ || internetToolsWorker_ == nullptr || internetToolsWorker_->isRunning()
        || (deepScanWorker_ != nullptr && deepScanWorker_->isRunning())
        || (fullScanWorker_ != nullptr && fullScanWorker_->isRunning()))
        return;
    activeInternetOperation_ = InternetOperation::PublicIpLookup;
    internetToolsProgress_->setValue(0);
    internetToolsProgress_->show();
    internetToolsStatus_->setText(QStringLiteral("Определение публичного IP и провайдера…"));
    networkPublicIpValue_->setText(QStringLiteral("проверяется…"));
    if (diagnosticPublicIpCard_ != nullptr)
        diagnosticPublicIpCard_->setSubtitle(QStringLiteral("⏳ Определяем публичный IP и провайдера…"));
    publicIpRefreshButton_->setEnabled(false);
    speedTestButton_->setEnabled(false);
    internetToolsWorker_->startPublicIpLookup();
}

void MainWindow::startInternetSpeedTest()
{
    if (internetToolsWorker_ == nullptr || paused_
        || (deepScanWorker_ != nullptr && deepScanWorker_->isRunning())
        || (fullScanWorker_ != nullptr && fullScanWorker_->isRunning()))
        return;
    if (internetToolsWorker_->isRunning())
    {
        if (activeInternetOperation_ == InternetOperation::SpeedTest)
        {
            speedTestButton_->setEnabled(false);
            speedTestButton_->setText(QStringLiteral("Останавливаем…"));
            internetToolsStatus_->setText(QStringLiteral("Остановка после текущей сетевой операции…"));
            internetToolsWorker_->requestStop();
        }
        return;
    }
    activeInternetOperation_ = InternetOperation::SpeedTest;
    internetToolsProgress_->setValue(0);
    internetToolsProgress_->show();
    internetToolsStatus_->setText(QStringLiteral("Однопоточный замер: до 10 МБ загрузки и 5 МБ отдачи…"));
    networkSpeedTestValue_->setText(QStringLiteral("идёт измерение…"));
    publicIpRefreshButton_->setEnabled(false);
    speedTestButton_->setEnabled(true);
    speedTestButton_->setText(QStringLiteral("Остановить тест скорости"));
    internetToolsWorker_->startSpeedTest();
}

void MainWindow::applyInternetResult(const InternetOperation operation, const QJsonObject& result)
{
    const bool ok = result.value(QStringLiteral("ok")).toBool();
    const QString error
        = result.value(QStringLiteral("error")).toString(QStringLiteral("неизвестная ошибка"));
    if (operation == InternetOperation::PublicIpLookup)
    {
        latestPublicIp_ = result;
        renderPublicIpCard(result);
        if (!ok)
        {
            const QString text = result.value(QStringLiteral("cancelled")).toBool()
                ? QStringLiteral("проверка отменена")
                : QStringLiteral("⚠ ошибка: %1").arg(error);
            networkPublicIpValue_->setText(text);
            internetToolsStatus_->setText(QStringLiteral("Публичный IP: %1").arg(text));
            return;
        }
        const QString text
            = QStringLiteral("%1 — %2 (%3, %4)")
                  .arg(result.value(QStringLiteral("public_ip")).toString(QStringLiteral("н/д")),
                      result.value(QStringLiteral("provider")).toString(QStringLiteral("н/д")),
                      result.value(QStringLiteral("location")).toString(QStringLiteral("н/д")),
                      result.value(QStringLiteral("checked_at")).toString(QStringLiteral("н/д")));
        networkPublicIpValue_->setText(text);
        internetToolsStatus_->setText(QStringLiteral("Публичный IP обновлён · источник: %1")
                .arg(result.value(QStringLiteral("source")).toString(QStringLiteral("н/д"))));
        return;
    }

    latestSpeedTest_ = result;
    const QString diagnosticKind = latestDiagnosticReport_.value(QStringLiteral("scan"))
                                       .toObject().value(QStringLiteral("kind")).toString();
    if (!latestDiagnosticReport_.isEmpty() && diagnosticKind != QStringLiteral("deep_local")
        && diagnosticKind != QStringLiteral("full"))
    {
        latestDiagnosticReport_.insert(QStringLiteral("speed_test"), result);
        diagnosticReportPreview_->setPlainText(diagnosticReportText(latestDiagnosticReport_));
    }
    if (!ok)
    {
        const QString text = result.value(QStringLiteral("cancelled")).toBool()
            ? QStringLiteral("тест отменён")
            : QStringLiteral("⚠ ошибка: %1").arg(error);
        networkSpeedTestValue_->setText(text);
        internetToolsStatus_->setText(QStringLiteral("Тест скорости: %1").arg(text));
        return;
    }
    const QJsonValue ping = result.value(QStringLiteral("ping_ms"));
    const QString pingText
        = ping.isDouble() ? QStringLiteral("%1 мс").arg(ping.toDouble(), 0, 'f', 1) : QStringLiteral("н/д");
    const QString text
        = QStringLiteral("↓ %1 / ↑ %2 Мбит/с, пинг %3 (%4)")
              .arg(result.value(QStringLiteral("download_mbps")).toDouble(), 0, 'f', 1)
              .arg(result.value(QStringLiteral("upload_mbps")).toDouble(), 0, 'f', 1)
              .arg(pingText, result.value(QStringLiteral("tested_at")).toString(QStringLiteral("н/д")));
    networkSpeedTestValue_->setText(text);
    internetToolsStatus_->setText(
        QStringLiteral("%1 · %2").arg(text, result.value(QStringLiteral("method")).toString()));
}

void MainWindow::restoreInternetControls()
{
    const bool running = internetToolsWorker_ != nullptr && internetToolsWorker_->isRunning();
    const bool fullRunning = fullScanWorker_ != nullptr && fullScanWorker_->isRunning();
    publicIpRefreshButton_->setEnabled(!paused_ && !running && !fullRunning);
    speedTestButton_->setEnabled(
        !paused_ && !fullRunning && (!running || activeInternetOperation_ == InternetOperation::SpeedTest));
    speedTestButton_->setText(running && activeInternetOperation_ == InternetOperation::SpeedTest
            ? QStringLiteral("Остановить тест скорости")
            : QStringLiteral("Тест скорости интернета"));
    if (!running)
    {
        QTimer::singleShot(1200, this,
            [this]
            {
                if (internetToolsWorker_ != nullptr && !internetToolsWorker_->isRunning())
                {
                    internetToolsProgress_->hide();
                }
            });
    }
}

void MainWindow::applyProcesses(
    const QVector<ProcessTelemetry>& processes, const int logicalProcessorCount, const QString& source)
{
    processRows_ = processes;
    processSourceValue_->setText(QStringLiteral("Источник: %1").arg(source));
    processCountValue_->setText(QStringLiteral("Процессов: %1 · логических CPU: %2")
            .arg(processRows_.size())
            .arg(logicalProcessorCount > 0 ? QString::number(logicalProcessorCount) : QStringLiteral("н/д")));
    renderProcessTable();
}

void MainWindow::renderProcessTable()
{
    if (processTable_ == nullptr || processSearch_ == nullptr)
    {
        return;
    }
    const QString filter = processSearch_->text().trimmed().toLower();
    QVector<ProcessTelemetry> rows;
    rows.reserve(processRows_.size());
    for (const auto& process : processRows_)
    {
        if (filter.isEmpty() || process.name.toLower().contains(filter)
            || QString::number(process.pid).contains(filter))
        {
            rows.append(process);
        }
    }
    if (filter.isEmpty() && !processShowAll_)
    {
        std::ranges::sort(
            rows, [](const auto& left, const auto& right) { return left.cpuPercent > right.cpuPercent; });
        constexpr qsizetype topCount = 15;
        if (rows.size() > topCount)
        {
            rows.resize(topCount);
        }
    }

    quint32 selectedPid = 0;
    quint64 selectedIdentity = 0;
    if (const auto selected = processTable_->selectionModel()->selectedRows(); !selected.isEmpty())
    {
        if (const auto* item = processTable_->item(selected.first().row(), 1); item != nullptr)
        {
            selectedPid = item->data(Qt::DisplayRole).toUInt();
            selectedIdentity = item->data(Qt::UserRole).toULongLong();
        }
    }

    const auto numericItem = [](const double value)
    {
        auto* item = new QTableWidgetItem;
        if (value >= 0.0)
        {
            item->setData(Qt::DisplayRole, qRound64(value * 10.0) / 10.0);
        }
        else
        {
            item->setText(QStringLiteral("н/д"));
        }
        return item;
    };
    const QSignalBlocker blocker(processTable_);
    processTable_->clearSelection();
    processTable_->setSortingEnabled(false);
    processTable_->setRowCount(rows.size());
    for (qsizetype row = 0; row < rows.size(); ++row)
    {
        const auto& process = rows[row];
        auto* name = new QTableWidgetItem(process.name);
        name->setToolTip(process.name);
        auto* pid = new QTableWidgetItem;
        pid->setData(Qt::DisplayRole, process.pid);
        pid->setData(Qt::UserRole, process.creationIdentity);
        auto* cpu = numericItem(process.cpuPercent);
        auto* memory = numericItem(process.workingSetMiB);
        auto* privateMemory = numericItem(process.privateMiB);
        auto* io = numericItem(process.ioMiB);
        auto* threads = new QTableWidgetItem;
        threads->setData(Qt::DisplayRole, process.threadCount);
        if (process.cpuPercent >= orion::core::kCpuCriticalPercent)
        {
            cpu->setForeground(QColor(QStringLiteral("#FF3B30")));
        }
        else if (process.cpuPercent >= orion::core::kCpuCriticalPercent * orion::core::kWarningRatio)
        {
            cpu->setForeground(QColor(QStringLiteral("#F4C542")));
        }
        else if (process.cpuPercent >= 0.0)
        {
            cpu->setForeground(QColor(QStringLiteral("#2ECC71")));
        }
        processTable_->setItem(static_cast<int>(row), 0, name);
        processTable_->setItem(static_cast<int>(row), 1, pid);
        processTable_->setItem(static_cast<int>(row), 2, cpu);
        processTable_->setItem(static_cast<int>(row), 3, memory);
        processTable_->setItem(static_cast<int>(row), 4, privateMemory);
        processTable_->setItem(static_cast<int>(row), 5, io);
        processTable_->setItem(static_cast<int>(row), 6, threads);
    }
    processTable_->setSortingEnabled(true);
    if (selectedPid != 0)
    {
        for (int row = 0; row < processTable_->rowCount(); ++row)
        {
            if (const auto* pid = processTable_->item(row, 1);
                pid != nullptr && pid->data(Qt::DisplayRole).toUInt() == selectedPid
                && pid->data(Qt::UserRole).toULongLong() == selectedIdentity)
            {
                processTable_->selectRow(row);
                break;
            }
        }
    }
    processCountValue_->setText(
        processCountValue_->text().section(QStringLiteral(" · отображается:"), 0, 0)
        + QStringLiteral(" · отображается: %1").arg(rows.size()));
    updateProcessActions();
}

void MainWindow::updateProcessActions()
{
    if (processTerminateButton_ == nullptr || processTable_ == nullptr)
        return;
    bool canTerminate = false;
    const auto selected = processTable_->selectionModel()->selectedRows();
    if (selected.size() == 1) {
        if (const auto* item = processTable_->item(selected.first().row(), 1)) {
            const auto pid = item->data(Qt::DisplayRole).toUInt();
            canTerminate = pid > 4 && pid != static_cast<quint32>(QCoreApplication::applicationPid())
                && item->data(Qt::UserRole).toULongLong() != 0;
        }
    }
    processTerminateButton_->setEnabled(canTerminate && ProcessActionWorker::supported()
        && !paused_ && !processTerminationPending_ && processActionWorker_ != nullptr
        && !processActionWorker_->isRunning());
}

void MainWindow::terminateSelectedProcess()
{
    updateProcessActions();
    if (!processTerminateButton_->isEnabled())
        return;
    const int row = processTable_->selectionModel()->selectedRows().first().row();
    // Capture immutable identity before opening the modal: refreshes continue during it.
    const auto pid = processTable_->item(row, 1)->data(Qt::DisplayRole).toUInt();
    const auto identity = processTable_->item(row, 1)->data(Qt::UserRole).toULongLong();
    const QString name = processTable_->item(row, 0)->text();
    processTerminationPending_ = true;
    updateProcessActions();
    QMessageBox confirmation(QMessageBox::Warning, QStringLiteral("Завершить процесс?"),
        QStringLiteral("Принудительно завершить «%1» (PID %2)?\n\nНесохранённые данные могут быть потеряны.")
            .arg(name).arg(pid), QMessageBox::Yes | QMessageBox::No, this);
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::No);
    confirmation.setEscapeButton(QMessageBox::No);
    const bool confirmed = confirmation.exec() == QMessageBox::Yes;
    if (confirmed && !paused_ && processActionWorker_->startTermination(pid, identity, true)) {
        processActionStatus_->setText(QStringLiteral("Завершение «%1» (PID %2)…").arg(name).arg(pid));
        return;
    }
    processTerminationPending_ = false;
    if (confirmed)
        processActionStatus_->setText(QStringLiteral("Завершение не запущено. Проверьте паузу и обновите список."));
    updateProcessActions();
}

void MainWindow::applyAutostartEntries(const QVector<AutostartTelemetry>& entries, const QString& source)
{
    autostartRows_ = entries;
    autostartLoaded_ = true;
    autostartRefreshButton_->setEnabled(!paused_);
    autostartSourceValue_->setText(QStringLiteral("Источник: %1").arg(source));
    const auto userCount = std::ranges::count_if(
        autostartRows_, [](const auto& entry) { return entry.category == QStringLiteral("user"); });
    autostartCountValue_->setText(QStringLiteral("Всего: %1 · система: %2 · пользователь: %3")
            .arg(autostartRows_.size())
            .arg(autostartRows_.size() - userCount)
            .arg(userCount));
    renderAutostartTable();
}

void MainWindow::renderAutostartTable()
{
    if (autostartTable_ == nullptr || autostartSearch_ == nullptr || autostartCategory_ == nullptr)
    {
        return;
    }
    const QString search = autostartSearch_->text().trimmed().toLower();
    const QString category = autostartCategory_->currentData().toString();
    QVector<AutostartTelemetry> rows;
    rows.reserve(autostartRows_.size());
    for (const auto& entry : autostartRows_)
    {
        const bool categoryMatches = category == QStringLiteral("all") || entry.category == category;
        const bool searchMatches = search.isEmpty() || entry.name.toLower().contains(search)
            || entry.command.toLower().contains(search) || entry.source.toLower().contains(search);
        if (categoryMatches && searchMatches)
        {
            rows.append(entry);
        }
    }
    QStringList selectedKey;
    const auto selected = autostartTable_->selectionModel()->selectedRows();
    if (!selected.isEmpty())
        selectedKey = autostartTable_->item(selected.first().row(), 0)->data(Qt::UserRole).toStringList();
    const QSignalBlocker blocker(autostartTable_);
    autostartTable_->clearSelection();
    autostartTable_->setSortingEnabled(false);
    autostartTable_->setRowCount(rows.size());
    for (qsizetype row = 0; row < rows.size(); ++row)
    {
        const auto& entry = rows[row];
        const QString categoryText = entry.category == QStringLiteral("user") ? QStringLiteral("Пользователь")
                                                                              : QStringLiteral("Система");
        const QString statusText = entry.enabled > 0 ? QStringLiteral("Включено")
            : entry.enabled == 0                     ? QStringLiteral("Отключено")
                                                     : QStringLiteral("—");
        auto* status = new QTableWidgetItem(statusText);
        if (entry.enabled > 0)
        {
            status->setForeground(QColor(QStringLiteral("#2ECC71")));
        }
        else if (entry.enabled == 0)
        {
            status->setForeground(QColor(QStringLiteral("#8B95A5")));
        }
        auto* name = new QTableWidgetItem(entry.name);
        name->setData(Qt::UserRole, QStringList {entry.name, entry.command, entry.source, entry.category});
        autostartTable_->setItem(static_cast<int>(row), 0, name);
        autostartTable_->setItem(static_cast<int>(row), 1, new QTableWidgetItem(entry.source));
        autostartTable_->setItem(static_cast<int>(row), 2, new QTableWidgetItem(categoryText));
        autostartTable_->setItem(static_cast<int>(row), 3, status);
        autostartTable_->setItem(static_cast<int>(row), 4, new QTableWidgetItem(entry.command));
        for (int column = 0; column < autostartTable_->columnCount(); ++column)
            autostartTable_->item(static_cast<int>(row), column)->setToolTip(
                autostartTable_->item(static_cast<int>(row), column)->text());
    }
    autostartTable_->setSortingEnabled(true);
    if (!selectedKey.isEmpty()) {
        for (int row = 0; row < autostartTable_->rowCount(); ++row) {
            if (autostartTable_->item(row, 0)->data(Qt::UserRole).toStringList() == selectedKey) {
                autostartTable_->selectRow(row);
                break;
            }
        }
    }
}

QJsonObject MainWindow::buildDiagnosticSnapshot() const
{
    const auto temperatureObject = [](const double value, const std::string_view component)
    {
        QJsonObject result;
        const auto optionalValue = value >= 0.0 ? std::optional<double> { value } : std::nullopt;
        result.insert(QStringLiteral("level"),
            QString::fromLatin1(
                orion::core::toString(orion::core::levelForTemperature(optionalValue, component))));
        result.insert(QStringLiteral("duration_seconds"), 0.0);
        if (optionalValue.has_value())
        {
            result.insert(QStringLiteral("current_value_c"), *optionalValue);
        }
        return result;
    };

    QJsonObject current;
    if (latestCpuPercent_ >= 0.0)
    {
        current.insert(QStringLiteral("cpu_usage_percent"), latestCpuPercent_);
    }
    if (latestRamPercent_ >= 0.0)
    {
        current.insert(QStringLiteral("ram_usage_percent"), latestRamPercent_);
    }
    if (latestGpuPercent_ >= 0.0)
    {
        current.insert(QStringLiteral("gpu_usage_percent"), latestGpuPercent_);
    }
    if (latestCpuTemperatureC_ >= 0.0)
    {
        current.insert(QStringLiteral("cpu_temperature_c"), latestCpuTemperatureC_);
    }
    if (latestGpuTemperatureC_ >= 0.0)
    {
        current.insert(QStringLiteral("gpu_temperature_c"), latestGpuTemperatureC_);
    }
    if (latestPing_.usable())
    {
        current.insert(QStringLiteral("net_ping_ms"), latestPing_.latencyMs);
    }

    QJsonArray disks;
    for (const auto& disk : latestDisks_)
    {
        disks.append(QJsonObject {
            { QStringLiteral("device"), disk.name },
            { QStringLiteral("mountpoint"), disk.mountPoint },
            { QStringLiteral("type"), disk.storageType },
            { QStringLiteral("filesystem"), disk.fileSystem },
            { QStringLiteral("total_gb"), disk.totalGiB },
            { QStringLiteral("free_gb"), disk.freeGiB },
            { QStringLiteral("used_percent"), disk.usedPercent },
        });
    }
    QJsonArray autostart;
    for (const auto& entry : autostartRows_)
    {
        QJsonObject object {
            { QStringLiteral("name"), entry.name },
            { QStringLiteral("command"), entry.command },
            { QStringLiteral("source"), entry.source },
            { QStringLiteral("category"), entry.category },
        };
        object.insert(QStringLiteral("enabled"),
            entry.enabled < 0 ? QJsonValue { QJsonValue::Null } : QJsonValue { entry.enabled > 0 });
        autostart.append(object);
    }
    QJsonArray temperatureSensors;
    QJsonArray sensorSources;
    for (const auto& sensor : latestTemperatures_)
    {
        QJsonObject object {
            { QStringLiteral("component"), sensor.component },
            { QStringLiteral("label"), sensor.label },
            { QStringLiteral("value_c"), sensor.valueC },
            { QStringLiteral("source"), sensor.source },
        };
        if (sensor.highC >= 0.0)
            object.insert(QStringLiteral("high_c"), sensor.highC);
        if (sensor.criticalC >= 0.0)
            object.insert(QStringLiteral("critical_c"), sensor.criticalC);
        temperatureSensors.append(object);
        if (!sensor.source.isEmpty() && !sensorSources.contains(sensor.source))
        {
            sensorSources.append(sensor.source);
        }
    }
    QJsonArray fanSensors;
    for (const auto& fan : latestFans_)
    {
        QJsonObject object {
            { QStringLiteral("component"), fan.component },
            { QStringLiteral("label"), fan.label },
            { QStringLiteral("source"), fan.source },
        };
        if (fan.rpm >= 0.0)
            object.insert(QStringLiteral("rpm"), fan.rpm);
        if (fan.percent >= 0.0)
            object.insert(QStringLiteral("percent"), fan.percent);
        fanSensors.append(object);
        if (!fan.source.isEmpty() && !sensorSources.contains(fan.source))
        {
            sensorSources.append(fan.source);
        }
    }
    const QJsonObject sensors {
        { QStringLiteral("data_quality"),
            temperatureSensors.isEmpty() && fanSensors.isEmpty() ? QStringLiteral("unsupported")
                                                                 : QStringLiteral("valid") },
        { QStringLiteral("sources"), sensorSources },
        { QStringLiteral("temperatures"), temperatureSensors },
        { QStringLiteral("fans"), fanSensors },
    };
    const QJsonObject diagnostics {
        { QStringLiteral("temperature"),
            QJsonObject {
                { QStringLiteral("cpu"), temperatureObject(latestCpuTemperatureC_, "cpu") },
                { QStringLiteral("gpu"), temperatureObject(latestGpuTemperatureC_, "gpu") },
            } },
        { QStringLiteral("current"), current },
        { QStringLiteral("sensors"), sensors },
    };
    const QJsonObject hardware {
        { QStringLiteral("os"), latestOperatingSystem_ },
        { QStringLiteral("backend"), latestBackend_ },
        { QStringLiteral("cpu"), QJsonObject { { QStringLiteral("model"), latestCpuName_ } } },
        { QStringLiteral("gpu"), QJsonArray { QJsonObject { { QStringLiteral("model"), latestGpuName_ } } } },
        { QStringLiteral("ram"), QJsonObject { { QStringLiteral("total_gb"), latestRamTotalGiB_ } } },
        { QStringLiteral("disks"), disks },
    };
    QJsonObject runtime {
        { QStringLiteral("data_quality"),
            latestRamPercent_ >= 0.0 ? latestRuntime_.pagingRateQuality : QStringLiteral("unsupported") },
        { QStringLiteral("paging_sampling_mode"), QStringLiteral("endpoint_rates") },
        { QStringLiteral("paging_activity"), latestRuntime_.pagingActivity },
        { QStringLiteral("hard_fault_activity"), latestRuntime_.hardFaultActivity },
        { QStringLiteral("memory_pressure"), latestRuntime_.memoryPressure },
        { QStringLiteral("paging_interpretation"), latestRuntime_.pagingInterpretation },
    };
    const auto insertKnown = [&runtime](const QString& key, const double value)
    {
        if (value >= 0.0)
            runtime.insert(key, value);
    };
    insertKnown(QStringLiteral("memory_available_percent"),
        latestRuntime_.ramAvailablePercent >= 0.0 ? latestRuntime_.ramAvailablePercent
            : latestRamPercent_ >= 0.0            ? 100.0 - latestRamPercent_
                                                  : -1.0);
    insertKnown(QStringLiteral("swap_used_percent"), latestRuntime_.swapUsedPercent);
    insertKnown(QStringLiteral("commit_used_percent"), latestRuntime_.commitUsedPercent);
    insertKnown(QStringLiteral("commit_used_mb"), latestRuntime_.commitUsedMiB);
    insertKnown(QStringLiteral("commit_limit_mb"), latestRuntime_.commitLimitMiB);
    insertKnown(QStringLiteral("pagefile_used_percent"), latestRuntime_.pagefileUsedPercent);
    insertKnown(QStringLiteral("pages_input_per_sec"), latestRuntime_.pagesInputPerSecond);
    insertKnown(QStringLiteral("page_reads_per_sec"), latestRuntime_.pageReadsPerSecond);
    insertKnown(QStringLiteral("pages_per_sec"), latestRuntime_.pagesPerSecond);
    insertKnown(QStringLiteral("pages_output_per_sec"), latestRuntime_.pagesOutputPerSecond);
    insertKnown(QStringLiteral("page_writes_per_sec"), latestRuntime_.pageWritesPerSecond);
    insertKnown(
        QStringLiteral("system_context_switches_per_sec"), latestRuntime_.systemContextSwitchesPerSecond);
    insertKnown(QStringLiteral("disk_busy_percent"), latestRuntime_.diskBusyPercent);
    insertKnown(QStringLiteral("disk_read_latency_ms"), latestRuntime_.diskReadLatencyMs);
    insertKnown(QStringLiteral("disk_write_latency_ms"), latestRuntime_.diskWriteLatencyMs);
    runtime.insert(QStringLiteral("net_ping_ms"),
        latestPing_.usable() ? QJsonValue { latestPing_.latencyMs } : QJsonValue { QJsonValue::Null });
    runtime.insert(QStringLiteral("net_ping_quality"), latestPing_.quality);
    runtime.insert(QStringLiteral("net_ping_target"), latestPing_.target);
    runtime.insert(QStringLiteral("net_ping_source"), latestPing_.source);
    runtime.insert(QStringLiteral("net_ping_reason"), latestPing_.reason);
    QJsonObject network {
        { QStringLiteral("data_quality"), latestPing_.quality },
        { QStringLiteral("target"), latestPing_.target },
        { QStringLiteral("port"), static_cast<int>(latestPing_.port) },
        { QStringLiteral("latency_ms"),
            latestPing_.usable() ? QJsonValue { latestPing_.latencyMs } : QJsonValue { QJsonValue::Null } },
        { QStringLiteral("source"), latestPing_.source },
        { QStringLiteral("reason"), latestPing_.reason },
        { QStringLiteral("observed_at"), latestPing_.observedAt },
    };
    if (!latestPublicIp_.isEmpty())
    {
        network.insert(QStringLiteral("public_ip"), latestPublicIp_);
    }
    QJsonObject snapshot {
        { QStringLiteral("diagnostics"), diagnostics },
        { QStringLiteral("stress_test"), latestStressResult_ },
        { QStringLiteral("hardware"), hardware },
        { QStringLiteral("runtime"), runtime },
        { QStringLiteral("network"), network },
        { QStringLiteral("incident"), latestIncident_ },
        { QStringLiteral("app_monitor"), latestAppMonitorReport_ },
        { QStringLiteral("autostart_entries"), autostart },
        { QStringLiteral("autostart_collected"), autostartLoaded_ },
        { QStringLiteral("tray"),
            QJsonObject {
                { QStringLiteral("icon_enabled"), settings_.trayIconEnabled },
                { QStringLiteral("notifications_enabled"), settings_.trayNotificationsEnabled },
                { QStringLiteral("system_available"), QSystemTrayIcon::isSystemTrayAvailable() },
                { QStringLiteral("visible"), trayIcon_ != nullptr && trayIcon_->isVisible() },
                { QStringLiteral("monitoring_paused"), paused_ },
            } },
    };
    if (!latestSpeedTest_.isEmpty())
    {
        snapshot.insert(QStringLiteral("speed_test"), latestSpeedTest_);
    }
    return snapshot;
}

void MainWindow::startDiagnosticScan()
{
    if (paused_ || diagnosticWorker_ == nullptr || diagnosticWorker_->isRunning()
        || (deepScanWorker_ != nullptr && deepScanWorker_->isRunning())
        || (fullScanWorker_ != nullptr && fullScanWorker_->isRunning()))
    {
        return;
    }
    diagnosticWorker_->scan(buildDiagnosticSnapshot());
}

void MainWindow::startDeepScan()
{
    if (paused_ || !telemetryReceived_ || deepScanWorker_ == nullptr || deepScanWorker_->isRunning())
        return;
    if (stressWorker_->isRunning() || diagnosticWorker_->isRunning() || hardwareWorker_->isRunning()
        || internetToolsWorker_->isRunning() || appMonitorWorker_->isRunning()
        || (fullScanWorker_ != nullptr && fullScanWorker_->isRunning()))
    {
        QMessageBox::information(this, QStringLiteral("Проверка уже выполняется"),
            QStringLiteral(
                "Дождитесь завершения текущей проверки или остановите её перед глубокой диагностикой."));
        return;
    }
    QMessageBox confirmation(QMessageBox::Warning, QStringLiteral("Запустить глубокую диагностику?"),
        QStringLiteral("Будут заново собраны характеристики ПК, SMART, журнал ОС и автозагрузка.\n\n"
                       "Затем последовательно, не одновременно:\n"
                       "• CPU — нагрузка всех логических потоков, 30 секунд;\n"
                       "• GPU — Direct3D 11 compute-нагрузка, 30 секунд;\n"
                       "• диск — запись, flush, чтение и проверка временного файла 200 МБ в AppData.\n\n"
                       "Это может вызвать нагрев и замедление ПК. Сохраните работу. "
                       "Температурная защита доступна только при наличии достоверного датчика. "
                       "Без датчика тепловая проверка останется неполной.\n\n"
                       "Интернет и сканирование локальной сети не используются. Продолжить?"),
        QMessageBox::Yes | QMessageBox::No, this);
    confirmation.setObjectName(QStringLiteral("DeepScanConfirmation"));
    confirmation.setDefaultButton(QMessageBox::No);
    confirmation.setEscapeButton(QMessageBox::No);
    if (confirmation.exec() != QMessageBox::Yes || paused_)
        return;
    // A background check can finish/start while the confirmation is open.
    if (stressWorker_->isRunning() || diagnosticWorker_->isRunning() || hardwareWorker_->isRunning()
        || internetToolsWorker_->isRunning() || appMonitorWorker_->isRunning()
        || deepScanWorker_->isRunning() || (fullScanWorker_ != nullptr && fullScanWorker_->isRunning()))
        return;
    auto* dialog = new DeepScanDialog(deepScanWorker_, this);
    if (!deepScanWorker_->startScan(buildDiagnosticSnapshot(), buildHardwareSeed(), true))
    {
        delete dialog;
        return;
    }
    deepScanButton_->setEnabled(false);
    fullScanButton_->setEnabled(false);
    diagnosticScanButton_->setEnabled(false);
    stressStartButton_->setEnabled(false);
    dialog->show();
}

void MainWindow::startFullScan()
{
    if (paused_ || !telemetryReceived_ || fullScanWorker_ == nullptr || fullScanWorker_->isRunning())
        return;
    if (stressWorker_->isRunning() || diagnosticWorker_->isRunning() || hardwareWorker_->isRunning()
        || internetToolsWorker_->isRunning() || appMonitorWorker_->isRunning()
        || deepScanWorker_->isRunning())
    {
        QMessageBox::information(this, QStringLiteral("Проверка уже выполняется"),
            QStringLiteral("Дождитесь завершения текущей проверки или остановите её перед полной проверкой."));
        return;
    }
    QMessageBox confirmation(QMessageBox::Warning, QStringLiteral("Запустить полную проверку?"),
        QStringLiteral("Сначала будет выполнена глубокая локальная диагностика:\n"
                       "• свежие характеристики, SMART, журнал ОС и автозагрузка;\n"
                       "• CPU и GPU — по 30 секунд нагрузки;\n"
                       "• диск — запись, flush, чтение и проверка временного файла 200 МБ.\n\n"
                       "Затем будут выполнены интернет-этапы:\n"
                       "• загрузка контрольных 10 МБ и отправка 5 МБ через Cloudflare;\n"
                       "• запрос публичного IP и провайдера через ipinfo.io, при ошибке — ip-api.com.\n\n"
                       "Также будут прочитаны зарегистрированные антивирусы из Центра безопасности Windows. "
                       "Проверка может вызвать нагрев, временное замедление и расход трафика. "
                       "Сохраните работу. Продолжить?"),
        QMessageBox::Yes | QMessageBox::No, this);
    confirmation.setObjectName(QStringLiteral("FullScanConfirmation"));
    confirmation.setDefaultButton(QMessageBox::No);
    confirmation.setEscapeButton(QMessageBox::No);
    if (confirmation.exec() != QMessageBox::Yes || paused_)
        return;
    if (stressWorker_->isRunning() || diagnosticWorker_->isRunning() || hardwareWorker_->isRunning()
        || internetToolsWorker_->isRunning() || appMonitorWorker_->isRunning()
        || deepScanWorker_->isRunning() || fullScanWorker_->isRunning())
        return;

    auto* dialog = new FullScanDialog(fullScanWorker_, this);
    if (!fullScanWorker_->startScan(buildDiagnosticSnapshot(), buildHardwareSeed(), true))
    {
        delete dialog;
        return;
    }
    fullScanButton_->setEnabled(false);
    deepScanButton_->setEnabled(false);
    diagnosticScanButton_->setEnabled(false);
    stressStartButton_->setEnabled(false);
    publicIpRefreshButton_->setEnabled(false);
    speedTestButton_->setEnabled(false);
    dialog->show();
}

void MainWindow::applyDiagnosticReport(const QJsonObject& report)
{
    latestDiagnosticReport_ = report;
    diagnosticLoaded_ = true;
    diagnosticScanButton_->setEnabled(!paused_ && !deepScanWorker_->isRunning()
        && (fullScanWorker_ == nullptr || !fullScanWorker_->isRunning()));
    diagnosticSaveJsonButton_->setEnabled(true);
    diagnosticSaveTextButton_->setEnabled(true);
    diagnosticCopyButton_->setEnabled(true);
    renderPassiveDiagnosticCards(report);

    const auto risk = report.value(QStringLiteral("risk_assessment")).toObject();
    const auto coverage = report.value(QStringLiteral("coverage")).toObject();
    const auto scan = report.value(QStringLiteral("scan")).toObject();
    const QString scanKind = scan.value(QStringLiteral("kind")).toString();
    const bool incompleteLongScan = (scanKind == QStringLiteral("deep_local")
        || scanKind == QStringLiteral("full"))
        && scan.value(QStringLiteral("state")) != QStringLiteral("complete");
    diagnosticStatusValue_->setText(incompleteLongScan
            ? QStringLiteral("%1 проверка неполная — частичный отчёт. %2")
                  .arg(scanKind == QStringLiteral("full") ? QStringLiteral("Полная")
                                                          : QStringLiteral("Глубокая"),
                       risk.value(QStringLiteral("headline")).toString())
            : risk.value(QStringLiteral("headline")).toString());
    diagnosticCoverageValue_->setText(coverage.value(QStringLiteral("message")).toString());
    const auto findings = report.value(QStringLiteral("findings")).toArray();
    const auto logReport = report.value(QStringLiteral("diagnostics"))
                               .toObject()
                               .value(QStringLiteral("log_errors"))
                               .toObject();
    const auto logQuality
        = logReport.value(QStringLiteral("data_quality")).toString(QStringLiteral("collector_error"));
    const int logCount = logReport.value(QStringLiteral("errors")).toArray().size();
    if (errorsBadge_ != nullptr)
    {
        if (logQuality == QStringLiteral("valid"))
        {
            errorsBadge_->setText(QStringLiteral("Ошибки ОС с загрузки: %1").arg(logCount));
            errorsBadge_->setEnabled(true);
        }
        else
        {
            errorsBadge_->setText(QStringLiteral("Ошибки ОС: проверка неполна"));
            errorsBadge_->setEnabled(false);
        }
    }
    diagnosticTable_->setSortingEnabled(false);
    diagnosticTable_->setRowCount(findings.size());
    const auto severityName = [](const QString& value)
    {
        if (value == QStringLiteral("critical"))
            return QStringLiteral("Критично");
        if (value == QStringLiteral("warning"))
            return QStringLiteral("Внимание");
        return QStringLiteral("Информация");
    };
    const auto domainName = [](const QString& value)
    {
        if (value == QStringLiteral("memory"))
            return QStringLiteral("Память");
        if (value == QStringLiteral("cooling"))
            return QStringLiteral("Охлаждение");
        if (value == QStringLiteral("storage"))
            return QStringLiteral("Накопители");
        if (value == QStringLiteral("startup"))
            return QStringLiteral("Автозагрузка");
        if (value == QStringLiteral("coverage"))
            return QStringLiteral("Покрытие");
        if (value == QStringLiteral("stability"))
            return QStringLiteral("Стабильность");
        return value.isEmpty() ? QStringLiteral("Общее") : value;
    };
    const auto confidenceName = [](const QString& value)
    {
        if (value == QStringLiteral("high"))
            return QStringLiteral("Высокая");
        if (value == QStringLiteral("medium"))
            return QStringLiteral("Средняя");
        if (value == QStringLiteral("low"))
            return QStringLiteral("Низкая");
        return QStringLiteral("Не определена");
    };
    for (qsizetype row = 0; row < findings.size(); ++row)
    {
        const auto finding = findings.at(row).toObject();
        const auto severity = finding.value(QStringLiteral("severity")).toString();
        auto* severityItem = new QTableWidgetItem(severityName(severity));
        severityItem->setData(Qt::UserRole, finding);
        severityItem->setForeground(QColor(severity == QStringLiteral("critical") ? QStringLiteral("#FF3B30")
                : severity == QStringLiteral("warning")                           ? QStringLiteral("#F4C542")
                                                        : QStringLiteral("#8B95A5")));
        diagnosticTable_->setItem(static_cast<int>(row), 0, severityItem);
        diagnosticTable_->setItem(static_cast<int>(row), 1,
            new QTableWidgetItem(domainName(finding.value(QStringLiteral("domain")).toString())));
        diagnosticTable_->setItem(static_cast<int>(row), 2,
            new QTableWidgetItem(finding.value(QStringLiteral("title")).toString()));
        diagnosticTable_->setItem(static_cast<int>(row), 3,
            new QTableWidgetItem(confidenceName(finding.value(QStringLiteral("confidence")).toString())));
    }
    diagnosticTable_->setSortingEnabled(true);
    diagnosticReportPreview_->setPlainText(diagnosticReportText(report));
    if (!findings.isEmpty())
    {
        diagnosticTable_->selectRow(0);
    }
    else
    {
        diagnosticDetails_->setPlainText(QStringLiteral("Явных находок по доступным данным нет."));
    }
}

void MainWindow::renderPassiveDiagnosticCards(const QJsonObject& report)
{
    const auto diagnostics = report.value(QStringLiteral("diagnostics")).toObject();
    const auto smart = diagnostics.value(QStringLiteral("smart")).toObject();
    const auto logErrors = diagnostics.value(QStringLiteral("log_errors")).toObject();
    if (!smart.isEmpty())
        renderSmartCards(smart);
    if (!logErrors.isEmpty())
        renderLogErrorsCard(logErrors);
    if (!latestPublicIp_.isEmpty())
        renderPublicIpCard(latestPublicIp_);

    // A background diagnostic report may predate the current user marker.
    auto incident = latestIncident_.isEmpty()
        ? report.value(QStringLiteral("incident")).toObject() : latestIncident_;
    if (!incident.isEmpty())
        renderIncidentCard(incident);
}

void MainWindow::renderSmartCards(const QJsonObject& report)
{
    if (diagnosticSmartCardsLayout_ == nullptr || diagnosticSmartStatus_ == nullptr)
        return;
    for (auto* card : std::as_const(diagnosticSmartCards_))
    {
        diagnosticSmartCardsLayout_->removeWidget(card);
        delete card;
    }
    diagnosticSmartCards_.clear();

    const bool available = report.value(QStringLiteral("available")).toBool();
    const QString note = report.value(QStringLiteral("note")).toString();
    const QString hint = report.value(QStringLiteral("install_hint")).toString();
    const QString source = report.value(QStringLiteral("source")).toString();
    if (!available)
    {
        QString text
            = note.isEmpty() ? QStringLiteral("SMART недоступен.") : QStringLiteral("⚠ %1").arg(note);
        if (!hint.isEmpty())
            text += QStringLiteral("\nУстановить: %1").arg(hint);
        diagnosticSmartStatus_->setText(text);
        return;
    }

    const auto disks = report.value(QStringLiteral("disks")).toArray();
    if (!note.isEmpty())
        diagnosticSmartStatus_->setText(note);
    else if (disks.isEmpty())
        diagnosticSmartStatus_->setText(
            QStringLiteral("SMART доступен, но опрашиваемые устройства не обнаружены."));
    else
        diagnosticSmartStatus_->setText(source.isEmpty()
                ? QStringLiteral("Найдено устройств: %1").arg(disks.size())
                : QStringLiteral("Найдено устройств: %1 · источник: %2").arg(disks.size()).arg(source));

    const auto palette = paletteFor(settings_);
    int index = 0;
    for (const auto& value : disks)
    {
        const auto disk = value.toObject();
        const QString device = disk.value(QStringLiteral("device")).toString(QStringLiteral("устройство"));
        const QString model = disk.value(QStringLiteral("model")).toString(QStringLiteral("н/д"));
        auto* card = new DetailCard(QStringLiteral("%1 — %2").arg(device, model),
            QStringLiteral("smart_%1").arg(index), true, diagnosticSmartCardsHost_);
        markDiagnosticCard(card, QStringLiteral("smart"));
        card->setThemeColors(QColor(palette.accent), QColor(palette.muted));
        card->setStatus(statusLevel(disk.value(QStringLiteral("level")).toString()));
        card->setField(QStringLiteral("health"), QStringLiteral("Статус SMART"),
            disk.value(QStringLiteral("health")).toString(QStringLiteral("н/д")));
        card->setField(QStringLiteral("type"), QStringLiteral("Тип диска"),
            disk.value(QStringLiteral("disk_type")).toString(QStringLiteral("н/д")));
        card->setField(QStringLiteral("reallocated"), QStringLiteral("Reallocated sectors"),
            jsonNumber(disk.value(QStringLiteral("reallocated"))));
        card->setField(QStringLiteral("pending"), QStringLiteral("Pending sectors"),
            jsonNumber(disk.value(QStringLiteral("pending"))));
        card->setField(QStringLiteral("uncorrectable"), QStringLiteral("Uncorrectable errors"),
            jsonNumber(disk.value(QStringLiteral("uncorrectable"))));
        card->setField(QStringLiteral("temperature"), QStringLiteral("Температура"),
            jsonNumber(disk.value(QStringLiteral("temperature_c")), QStringLiteral("°C"), 1));
        if (disk.value(QStringLiteral("is_nvme")).toBool())
        {
            card->setField(QStringLiteral("nvme_warning"), QStringLiteral("NVMe Critical Warning"),
                jsonNumber(disk.value(QStringLiteral("nvme_critical_warning"))));
            card->setField(QStringLiteral("nvme_wear"), QStringLiteral("Использовано ресурса"),
                jsonNumber(disk.value(QStringLiteral("nvme_percentage_used")), QStringLiteral("%")));
            card->setField(QStringLiteral("nvme_media_errors"), QStringLiteral("Media/Data Integrity Errors"),
                jsonNumber(disk.value(QStringLiteral("nvme_media_errors"))));
        }

        QString subtitle = disk.value(QStringLiteral("note")).toString();
        if (subtitle.isEmpty())
        {
            QStringList risks;
            for (const auto& riskValue : disk.value(QStringLiteral("risk_reasons")).toArray())
            {
                const QString text = riskValue.toObject().value(QStringLiteral("text")).toString();
                if (!text.isEmpty())
                    risks.append(text);
            }
            subtitle = risks.join(QStringLiteral("; "));
        }
        card->setSubtitle(subtitle);
        diagnosticSmartCardsLayout_->addWidget(card);
        diagnosticSmartCards_.append(card);
        ++index;
    }
}

void MainWindow::renderLogErrorsCard(const QJsonObject& report)
{
    if (diagnosticLogErrorsCard_ == nullptr || diagnosticLogErrorsText_ == nullptr)
        return;
    const auto errors = report.value(QStringLiteral("errors")).toArray();
    auto groups = report.value(QStringLiteral("groups")).toArray();
    if (groups.isEmpty() && !errors.isEmpty())
        groups = orion::diagnostics::summarizeSystemErrorEntries(errors, 20);

    QStringList lines;
    int visibleGroups = 0;
    for (const auto& value : groups)
    {
        const auto group = value.toObject();
        const QString example = group.value(QStringLiteral("example")).toString();
        if (example.isEmpty())
            continue;
        if (group.value(QStringLiteral("omitted")).toBool())
        {
            lines.append(example);
            continue;
        }
        ++visibleGroups;
        const int count = group.value(QStringLiteral("count")).toInt();
        lines.append(count > 1 ? QStringLiteral("[%1×] %2").arg(count).arg(example) : example);
    }
    const auto collectionNote = report.value(QStringLiteral("note")).toString();
    if (!collectionNote.isEmpty()) lines.prepend(collectionNote);
    if (lines.isEmpty())
    {
        lines.append(report.value(QStringLiteral("data_quality")).toString() == QStringLiteral("valid")
            ? QStringLiteral("Критических событий и ошибок не найдено.")
            : QStringLiteral("Нет доступных записей; отсутствие данных не подтверждает отсутствие ошибок."));
    }
    diagnosticLogErrorsText_->setPlainText(lines.join(u'\n'));
    const QString quality = report.value(QStringLiteral("data_quality")).toString(QStringLiteral("unknown"));
    const QString source = report.value(QStringLiteral("source")).toString();
    diagnosticLogErrorsCard_->setSubtitle(errors.isEmpty()
            ? QStringLiteral("Качество данных: %1%2")
                  .arg(quality, source.isEmpty() ? QString {} : QStringLiteral(" · источник: %1").arg(source))
            : QStringLiteral("Записей: %1; групп сообщений: %2 · качество: %3")
                  .arg(errors.size())
                  .arg(visibleGroups)
                  .arg(quality));
}

void MainWindow::renderPublicIpCard(const QJsonObject& result)
{
    if (diagnosticPublicIpCard_ == nullptr || result.isEmpty())
        return;
    const bool ok = result.value(QStringLiteral("ok")).toBool();
    if (!ok)
    {
        const QString error = result.value(QStringLiteral("cancelled")).toBool()
            ? QStringLiteral("проверка отменена")
            : result.value(QStringLiteral("error")).toString(QStringLiteral("неизвестная ошибка"));
        diagnosticPublicIpCard_->setSubtitle(
            QStringLiteral("⚠ Не удалось выполнить проверку: %1").arg(error));
        diagnosticPublicIpCard_->setField(
            QStringLiteral("public_ip"), QStringLiteral("Публичный IP"), QStringLiteral("н/д"));
        diagnosticPublicIpCard_->setField(
            QStringLiteral("provider"), QStringLiteral("Провайдер"), QStringLiteral("н/д"));
        diagnosticPublicIpCard_->setField(
            QStringLiteral("location"), QStringLiteral("Примерное местоположение"), QStringLiteral("н/д"));
        return;
    }
    diagnosticPublicIpCard_->setSubtitle(QStringLiteral("Проверено: %1 (источник: %2)")
            .arg(result.value(QStringLiteral("checked_at")).toString(QStringLiteral("н/д")),
                result.value(QStringLiteral("source")).toString(QStringLiteral("н/д"))));
    diagnosticPublicIpCard_->setField(QStringLiteral("public_ip"), QStringLiteral("Публичный IP"),
        result.value(QStringLiteral("public_ip")).toString(QStringLiteral("н/д")));
    diagnosticPublicIpCard_->setField(QStringLiteral("provider"), QStringLiteral("Провайдер"),
        result.value(QStringLiteral("provider")).toString(QStringLiteral("н/д")));
    diagnosticPublicIpCard_->setField(QStringLiteral("location"), QStringLiteral("Примерное местоположение"),
        result.value(QStringLiteral("location")).toString(QStringLiteral("н/д")));
}

void MainWindow::renderIncidentCard(const QJsonObject& incident)
{
    if (diagnosticIncidentCard_ == nullptr || incident.isEmpty())
        return;
    const QString state = incident.value(QStringLiteral("status")).toString(QStringLiteral("unknown"));
    const auto findings = incident.value(QStringLiteral("verdict_findings")).toArray().isEmpty()
        ? incident.value(QStringLiteral("findings")).toArray()
        : incident.value(QStringLiteral("verdict_findings")).toArray();
    // Completion describes the collector lifecycle, never proof of a healthy PC.
    auto level = orion::core::StatusLevel::Unknown;
    if (state == QStringLiteral("collecting"))
        level = orion::core::StatusLevel::Warning;
    for (const auto& value : findings)
    {
        const QString severity = value.toObject().value(QStringLiteral("severity")).toString();
        if (severity == QStringLiteral("critical"))
        {
            level = orion::core::StatusLevel::Critical;
            break;
        }
        if (severity == QStringLiteral("warning") && level != orion::core::StatusLevel::Critical)
            level = orion::core::StatusLevel::Warning;
    }
    diagnosticIncidentCard_->setStatus(level);
    const QString stateText = state == QStringLiteral("complete") ? QStringLiteral("анализ завершён")
        : state == QStringLiteral("collecting") ? QStringLiteral("собирается окно после события")
        : state == QStringLiteral("cancelled")  ? QStringLiteral("сбор отменён")
        : state == QStringLiteral("invalid")    ? QStringLiteral("некорректные параметры отметки")
                                                : QStringLiteral("состояние неизвестно");
    incidentStatusValue_
        = diagnosticIncidentCard_->setField(QStringLiteral("state"), QStringLiteral("Состояние"), stateText);
    diagnosticIncidentCard_->setField(QStringLiteral("marked_at"), QStringLiteral("Отмечено"),
        incident.value(QStringLiteral("marked_at"))
            .toString(incident.value(QStringLiteral("marker_timestamp")).toString(QStringLiteral("н/д"))));
    diagnosticIncidentCard_->setField(QStringLiteral("window"), QStringLiteral("Окно анализа"),
        QStringLiteral("%1 сек до / %2 сек после")
            .arg(incident.value(QStringLiteral("pre_seconds")).toDouble(60.0), 0, 'f', 0)
            .arg(incident.value(QStringLiteral("post_seconds")).toDouble(15.0), 0, 'f', 0));
    const auto summary = incident.value(QStringLiteral("summary")).toObject();
    const QString quality = summary.value("data_quality").toString();
    const bool timestamped = summary.value("measurement_contract") == orion::diagnostics::kIncidentMeasurementContract;
    diagnosticIncidentCard_->setField("source_freshness", QStringLiteral("Исходные данные"), timestamped
        ? QStringLiteral("исключено старых: %1; повторов сети: %2")
              .arg(summary.value("stale_metric_count").toInt()).arg(summary.value("repeated_interval_count").toInt())
        : QStringLiteral("проверка исходного времени недоступна"));
    diagnosticIncidentCard_->setField("coincidences", QStringLiteral("Свежие совпадения"), timestamped
        ? QStringLiteral("RAM %1; CPU %2; приложение %3")
              .arg(summary.value("memory_pressure_sample_count").toInt()).arg(summary.value("cpu_thermal_sample_count").toInt())
              .arg(summary.value("app_contributor_sample_count").toInt())
        : QStringLiteral("н/д"));
    const QString qualityText = quality == "valid" ? QStringLiteral("окно покрыто")
        : quality == "estimated" ? QStringLiteral("неполное окно")
        : quality == "stale" ? QStringLiteral("мало данных") : QStringLiteral("нет оценки");
    diagnosticIncidentCard_->setField(QStringLiteral("samples"), QStringLiteral("Телеметрия"),
        QStringLiteral("%1 замеров; %2")
            .arg(summary.value(QStringLiteral("sample_count")).toInt())
            .arg(qualityText));
    const auto numeric = [&summary](const char* key) {
        const auto value = summary.value(QLatin1String(key));
        return value.isDouble() && std::isfinite(value.toDouble())
            ? QString::number(value.toDouble(), 'f', 1) : QStringLiteral("н/д");
    };
    diagnosticIncidentCard_->setField("gaps", QStringLiteral("Пробелы, сек"),
        QStringLiteral("начало %1 / внутри %2 / конец %3").arg(numeric("leading_gap_seconds"),
            numeric("max_sample_gap_seconds"), numeric("trailing_gap_seconds")));
    diagnosticIncidentCard_->setField("filtered", QStringLiteral("Проверка замеров"),
        QStringLiteral("повторы: %1; конфликтные метки: %2; неверное время: %3")
            .arg(summary.value("duplicate_sample_count").toInt())
            .arg(summary.value("conflicting_timestamp_count").toInt())
            .arg(summary.value("invalid_timestamp_count").toInt()));
    const auto counter = [&summary](const char* key, const char* qualityKey) {
        const auto value = summary.value(QLatin1String(key));
        if (!value.isDouble()) return QStringLiteral("н/д");
        return (summary.value(QLatin1String(qualityKey)) == "partial" ? QStringLiteral("≥") : QString{})
            + QString::number(value.toDouble(), 'g', 8);
    };
    diagnosticIncidentCard_->setField("network_counts", QStringLiteral("Сеть около метки"),
        QStringLiteral("ошибки: %1; потери: %2").arg(counter("focus_net_error_count", "net_errors_data_quality"),
            counter("focus_net_drop_count", "net_drops_data_quality")));
    diagnosticIncidentCard_->setField("network_coverage", QStringLiteral("Счётчики известны"),
        QStringLiteral("ошибки %1/%3; потери %2/%3 замеров")
            .arg(summary.value("net_errors_known_sample_count").toInt())
            .arg(summary.value("net_drops_known_sample_count").toInt())
            .arg(summary.value("focus_sample_count").toInt()));
    diagnosticIncidentCard_->setField("log_window", QStringLiteral("Журнал событий"),
        QStringLiteral("%1 сек до / %2 сек после; без времени: %3")
            .arg(incident.value("log_pre_seconds").toDouble(incident.value("pre_seconds").toDouble(60)), 0, 'f', 0)
            .arg(incident.value("post_seconds").toDouble(15), 0, 'f', 0)
            .arg(incident.value("recent_system_error_correlation").toObject().value("unparseable_count").toInt()));

    QStringList lines;
    if (!summary.isEmpty())
        lines.append(QStringLiteral("Покрытие окна не подтверждает свежесть всех датчиков или исправность ПК. Сетевые суммы — только известные интервалы."));
    if (timestamped)
        lines.append(QStringLiteral("Для совпадений: возраст данных ≤3 сек, разброс исходного времени ≤1 сек. Это признаки, не доказательство причины."));
    if (!findings.isEmpty())
    {
        lines.append(QStringLiteral("Главные выводы:"));
        for (qsizetype index = 0; index < qMin<qsizetype>(3, findings.size()); ++index)
        {
            const auto finding = findings.at(index).toObject();
            lines.append(QStringLiteral("• %1 (уверенность: %2)")
                    .arg(finding.value(QStringLiteral("title")).toString(QStringLiteral("Находка")),
                        finding.value(QStringLiteral("confidence")).toString(QStringLiteral("unknown"))));
        }
    }
    else if (state == QStringLiteral("complete"))
    {
        lines.append(QStringLiteral("Выраженная причинная цепочка в записанном окне не подтверждена."));
    }
    const auto actions = incident.value(QStringLiteral("action_plan")).toArray();
    if (!actions.isEmpty())
    {
        lines.append(QString {});
        lines.append(QStringLiteral("Что сделать сначала:"));
        for (qsizetype index = 0; index < qMin<qsizetype>(4, actions.size()); ++index)
        {
            const auto action = actions.at(index).toObject();
            lines.append(QStringLiteral("%1. %2")
                    .arg(action.value(QStringLiteral("step")).toInt(static_cast<int>(index + 1)))
                    .arg(action.value(QStringLiteral("action")).toString()));
        }
    }
    const QString coverage
        = incident.value(QStringLiteral("coverage")).toObject().value(QStringLiteral("message")).toString();
    if (!coverage.isEmpty())
    {
        lines.append(QString {});
        lines.append(coverage);
    }
    diagnosticIncidentCard_->setSubtitle(lines.isEmpty()
            ? QStringLiteral("Собираются показатели восстановления и новые системные события.")
            : lines.join(u'\n'));
}

void MainWindow::updateDiagnosticTemperatureCards(const double cpuTemperatureC, const double gpuTemperatureC)
{
    if (diagnosticCpuTemperatureCard_ == nullptr || diagnosticGpuTemperatureCard_ == nullptr)
        return;
    const qint64 now = sessionTimer_.isValid() ? sessionTimer_.elapsed() : 0;
    const auto update = [now](DetailCard* card, const double temperature, const std::string_view kind,
                            orion::core::StatusLevel& previous, qint64& stateSince)
    {
        const std::optional<double> measured = std::isfinite(temperature) && temperature >= 0.0
            ? std::optional<double> { temperature }
            : std::nullopt;
        const auto current = orion::core::levelForTemperature(measured, kind);
        if (stateSince < 0 || current != previous)
        {
            previous = current;
            stateSince = now;
        }
        const auto [warning, critical] = orion::core::temperatureLimits(kind);
        card->setStatus(current);
        card->setField(QStringLiteral("temperature"), QStringLiteral("Текущая температура"),
            measured.has_value() ? QStringLiteral("%1°C").arg(*measured, 0, 'f', 1) : QStringLiteral("н/д"));
        card->setField(QStringLiteral("limits"), QStringLiteral("Пороги"),
            QStringLiteral("повышена от %1°C · критично от %2°C")
                .arg(warning, 0, 'f', 0)
                .arg(critical, 0, 'f', 0));
        QString state;
        if (current == orion::core::StatusLevel::Unknown)
        {
            state = QStringLiteral("датчик недоступен или ещё не опрошен");
            if (now - stateSince >= 1000)
                state += QStringLiteral(" — нет данных %1").arg(durationText(now - stateSince));
        }
        else if (current == orion::core::StatusLevel::Ok)
        {
            state = QStringLiteral("в норме");
        }
        else
        {
            state = current == orion::core::StatusLevel::Critical ? QStringLiteral("КРИТИЧЕСКАЯ")
                                                                  : QStringLiteral("Повышена");
            if (now - stateSince >= 1000)
                state += QStringLiteral(" — держится %1").arg(durationText(now - stateSince));
        }
        card->setField(QStringLiteral("state"), QStringLiteral("Состояние"), state);
    };
    update(diagnosticCpuTemperatureCard_, cpuTemperatureC, "cpu", diagnosticCpuTemperatureStatus_,
        diagnosticCpuTemperatureSinceMs_);
    update(diagnosticGpuTemperatureCard_, gpuTemperatureC, "gpu", diagnosticGpuTemperatureStatus_,
        diagnosticGpuTemperatureSinceMs_);
}

void MainWindow::renderDiagnosticDetails()
{
    const auto rows = diagnosticTable_->selectionModel()->selectedRows();
    if (rows.isEmpty())
    {
        diagnosticDetails_->clear();
        return;
    }
    const auto* item = diagnosticTable_->item(rows.first().row(), 0);
    const auto finding = item == nullptr ? QJsonObject {} : item->data(Qt::UserRole).toJsonObject();
    if (finding.isEmpty())
    {
        return;
    }
    QStringList lines {
        finding.value(QStringLiteral("title")).toString(),
        finding.value(QStringLiteral("detail")).toString(),
        QStringLiteral("Уверенность: %1 · статус: %2")
            .arg(finding.value(QStringLiteral("confidence")).toString(),
                finding.value(QStringLiteral("status")).toString()),
    };
    const auto evidence = finding.value(QStringLiteral("evidence")).toArray();
    if (!evidence.isEmpty())
    {
        lines.append(QStringLiteral("\nДоказательства:"));
        for (const auto& value : evidence)
        {
            const auto object = value.toObject();
            lines.append(QStringLiteral("• %1: %2 (%3)")
                    .arg(object.value(QStringLiteral("label")).toString(),
                        object.value(QStringLiteral("value")).toVariant().toString(),
                        object.value(QStringLiteral("source"))
                            .toString(QStringLiteral("источник не указан"))));
        }
    }
    const auto actions = finding.value(QStringLiteral("actions")).toArray();
    if (!actions.isEmpty())
    {
        lines.append(QStringLiteral("\nЧто сделать:"));
        int step = 1;
        for (const auto& value : actions)
        {
            lines.append(QStringLiteral("%1. %2").arg(step++).arg(value.toString()));
        }
    }
    diagnosticDetails_->setPlainText(lines.join(u'\n'));
}

void MainWindow::saveDiagnosticReport(const bool jsonFormat)
{
    if (latestDiagnosticReport_.isEmpty())
    {
        return;
    }
    const QString defaultName
        = jsonFormat ? QStringLiteral("orion_report.json") : QStringLiteral("orion_report.txt");
    const QString filter
        = jsonFormat ? QStringLiteral("JSON (*.json)") : QStringLiteral("Текстовый отчёт (*.txt)");
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Сохранить отчёт"), QDir::home().filePath(defaultName), filter);
    if (path.isEmpty())
    {
        return;
    }
    const QByteArray data = jsonFormat
        ? QJsonDocument(latestDiagnosticReport_).toJson(QJsonDocument::Indented)
        : diagnosticReportText(latestDiagnosticReport_).toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        QMessageBox::warning(this, QStringLiteral("Не удалось сохранить отчёт"), file.errorString());
        return;
    }
    diagnosticStatusValue_->setText(QStringLiteral("Отчёт сохранён: %1").arg(path));
}

void MainWindow::startStressTest()
{
    if (paused_ || stressWorker_ == nullptr || stressWorker_->isRunning()
        || (deepScanWorker_ != nullptr && deepScanWorker_->isRunning())
        || (fullScanWorker_ != nullptr && fullScanWorker_->isRunning()))
    {
        return;
    }
    const bool runCpu = stressCpuCheck_->isChecked();
    const bool runGpu = stressGpuCheck_->isChecked();
    const bool runDisk = stressDiskCheck_->isChecked();
    if (!runCpu && !runGpu && !runDisk)
    {
        QMessageBox::information(
            this, QStringLiteral("Стресс-тест"), QStringLiteral("Выберите хотя бы один компонент."));
        return;
    }
    if (runGpu && !stressGpuConfirm_->isChecked())
    {
        QMessageBox::warning(this, QStringLiteral("Нужно подтверждение"),
            QStringLiteral("Подтвердите разрешение на интенсивную нагрузку GPU."));
        return;
    }
    if (runDisk && !stressDiskConfirm_->isChecked())
    {
        QMessageBox::warning(this, QStringLiteral("Нужно подтверждение"),
            QStringLiteral("Подтвердите разрешение на запись временного файла."));
        return;
    }
    QStringList stages;
    if (runCpu)
        stages.append(QStringLiteral("CPU на всех логических ядрах"));
    if (runGpu)
        stages.append(QStringLiteral("GPU Direct3D 11 compute"));
    if (runDisk)
        stages.append(QStringLiteral("диск: запись, flush, чтение и проверка"));
    const auto answer = QMessageBox::warning(this, QStringLiteral("Запустить стресс-тест?"),
        QStringLiteral("Тест активно нагружает систему и может временно снизить отзывчивость.\n\n"
                       "Этапы: %1.\n\nCPU остановится при 100°C, GPU — при 92°C, если датчик доступен. "
                       "Можно в любой момент нажать «Экстренная остановка». Продолжить?")
            .arg(stages.join(QStringLiteral("; "))),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
    {
        return;
    }
    StressOptions options;
    options.runCpu = runCpu;
    options.runGpu = runGpu;
    options.runDisk = runDisk;
    options.durationSeconds = stressDuration_->currentData().toInt();
    options.diskSizeMiB = stressDiskSize_->currentData().toInt();
    stressStartButton_->setEnabled(false);
    stressStopButton_->setEnabled(true);
    stressCpuCheck_->setEnabled(false);
    stressGpuCheck_->setEnabled(false);
    stressDiskCheck_->setEnabled(false);
    stressDuration_->setEnabled(false);
    stressDiskSize_->setEnabled(false);
    stressGpuConfirm_->setEnabled(false);
    stressDiskConfirm_->setEnabled(false);
    stressProgress_->setValue(0);
    stressProgress_->show();
    stressResult_->hide();
    stressStatus_->setText(QStringLiteral("Подготовка теста…"));
    stressWorker_->startTest(options);
}

void MainWindow::applyStressReport(const QJsonObject& report)
{
    latestStressResult_ = report;
    stressStartButton_->setEnabled(!paused_);
    stressStopButton_->setEnabled(false);
    stressCpuCheck_->setEnabled(true);
    stressGpuCheck_->setEnabled(true);
    stressDiskCheck_->setEnabled(true);
    stressDuration_->setEnabled(true);
    stressDiskSize_->setEnabled(true);
    stressGpuConfirm_->setEnabled(true);
    stressDiskConfirm_->setEnabled(true);
    stressProgress_->setValue(
        report.value(QStringLiteral("stopped_early")).toBool() ? stressProgress_->value() : 100);
    const auto reason = report.value(QStringLiteral("stop_reason")).toString();
    stressStatus_->setText(reason == QStringLiteral("safety")
            ? report.value(QStringLiteral("safety_stop_reason")).toString()
            : reason == QStringLiteral("user") ? QStringLiteral("Тест остановлен пользователем")
                                               : QStringLiteral("Тест завершён, результат добавлен в отчёт"));
    renderStressResult(report);
    startDiagnosticScan();
}

void MainWindow::renderStressResult(const QJsonObject& result)
{
    QStringList lines;
    if (result.value(QStringLiteral("run_cpu")).toBool())
    {
        lines.append(QStringLiteral("CPU: %1 сек, %2 воркеров, %3% avg / %4% peak, максимум %5°C")
                .arg(result.value(QStringLiteral("cpu_actual_seconds")).toDouble(), 0, 'f', 1)
                .arg(result.value(QStringLiteral("cpu_workers")).toInt())
                .arg(result.value(QStringLiteral("cpu_load_avg_percent")).toDouble(), 0, 'f', 1)
                .arg(result.value(QStringLiteral("cpu_load_peak_percent")).toDouble(), 0, 'f', 1)
                .arg(result.value(QStringLiteral("cpu_max_temp_c")).isDouble()
                        ? QString::number(result.value(QStringLiteral("cpu_max_temp_c")).toDouble(), 'f', 1)
                        : QStringLiteral("н/д")));
    }
    if (result.value(QStringLiteral("run_gpu")).toBool())
    {
        lines.append(result.value(QStringLiteral("gpu_supported")).toBool()
                ? QStringLiteral("GPU: %1, %2 сек, %3% avg / %4% peak, output=%5")
                      .arg(result.value(QStringLiteral("gpu_backend")).toString())
                      .arg(result.value(QStringLiteral("gpu_actual_seconds")).toDouble(), 0, 'f', 1)
                      .arg(result.value(QStringLiteral("gpu_usage_avg_percent")).toDouble(), 0, 'f', 1)
                      .arg(result.value(QStringLiteral("gpu_usage_peak_percent")).toDouble(), 0, 'f', 1)
                      .arg(result.value(QStringLiteral("gpu_output_verified")).toBool()
                              ? QStringLiteral("проверен")
                              : QStringLiteral("НЕ проверен"))
                : QStringLiteral("GPU: не выполнен — %1")
                      .arg(result.value(QStringLiteral("gpu_worker_error")).toString()));
    }
    if (result.value(QStringLiteral("run_disk")).toBool())
    {
        lines.append(QStringLiteral("Диск: %1 МБ, запись %2 МБ/с, чтение %3 МБ/с, данные %4")
                .arg(result.value(QStringLiteral("disk_actual_mb")).toDouble(), 0, 'f', 1)
                .arg(result.value(QStringLiteral("disk_write_mbps")).toDouble(), 0, 'f', 1)
                .arg(result.value(QStringLiteral("disk_read_mbps")).toDouble(), 0, 'f', 1)
                .arg(result.value(QStringLiteral("disk_data_verified")).toBool()
                        ? QStringLiteral("совпали")
                        : QStringLiteral("НЕ подтверждены")));
    }
    stressResult_->setPlainText(lines.join(u'\n'));
    stressResult_->show();
}

void MainWindow::updateAppMonitorControls()
{
    if (!appStartButton_) return;
    const bool running = appMonitorWorker_ && appMonitorWorker_->isRunning();
    const bool scanning = (deepScanWorker_ && deepScanWorker_->isRunning())
        || (fullScanWorker_ && fullScanWorker_->isRunning());
    const bool editable = !paused_ && !running;
    appBrowseButton_->setEnabled(editable);
    appExecutablePath_->setEnabled(editable);
    appDuration_->setEnabled(editable);
    appCloseOnTimeout_->setEnabled(editable);
    appStartButton_->setEnabled(editable && !scanning
        && QFileInfo(QDir::fromNativeSeparators(appExecutablePath_->text().trimmed())).isFile());
}

void MainWindow::startAppMonitoring()
{
    if (paused_ || appMonitorWorker_ == nullptr || appMonitorWorker_->isRunning()
        || (deepScanWorker_ != nullptr && deepScanWorker_->isRunning())
        || (fullScanWorker_ != nullptr && fullScanWorker_->isRunning()))
        return;
    const QString path = QDir::fromNativeSeparators(appExecutablePath_->text().trimmed());
    if (path.isEmpty() || !QFileInfo(path).isFile())
    {
        QMessageBox::warning(this, QStringLiteral("Приложение не выбрано"),
            QStringLiteral("Укажите существующий исполняемый файл .exe."));
        return;
    }
    latestAppLiveSample_ = {};
    latestAppMonitorReport_ = {};
    appCpuChart_->clear();
    appMemoryChart_->clear();
    appVerdictCard_->hide();
    appOpenReportButton_->setEnabled(false);
    appSampleTable_->setRowCount(0);
    appReportPreview_->setPlainText(QStringLiteral("Идёт наблюдение…"));
    appProgress_->setValue(0);
    appStartButton_->setEnabled(false);
    appBrowseButton_->setEnabled(false);
    appStopButton_->setEnabled(true);
    appExecutablePath_->setEnabled(false);
    appDuration_->setEnabled(false);
    appCloseOnTimeout_->setEnabled(false);
    appSaveJsonButton_->setEnabled(false);
    appSaveTextButton_->setEnabled(false);
    appCopyButton_->setEnabled(false);
    for (auto* value : {appCpuValue_, appRamValue_, appIoValue_, appWindowValue_}) value->setText(QStringLiteral("н/д"));
    for (const auto& key : {QStringLiteral("threads"), QStringLiteral("faults"), QStringLiteral("commit"), QStringLiteral("gpu"), QStringLiteral("quality")})
        appLiveDetails_->setField(key, {}, QStringLiteral("н/д"));
    appMonitorWorker_->monitor(AppMonitorOptions {
        path,
        appDuration_->currentData().toInt(),
        appCloseOnTimeout_->isChecked(),
    });
}

void MainWindow::applyAppMonitorSample(const QJsonObject& suppliedSample)
{
    if (paused_) return;
    auto sample = suppliedSample;
    sample.insert(
        QStringLiteral("_received_monotonic"), static_cast<double>(sessionTimer_.elapsed()) / 1000.0);
    latestAppLiveSample_ = sample;
    appCpuChart_->appendSample(sample);
    appMemoryChart_->appendSample(sample);
    const auto number = [&sample](const QString& key, const QString& suffix = QString {}, int digits = 1) {
        const auto value = sample.value(key);
        return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= 0.0
            ? QString::number(value.toDouble(), 'f', digits) + suffix : QStringLiteral("н/д");
    };
    appCpuValue_->setText(QStringLiteral("%1 · %2 процессов")
            .arg(number(QStringLiteral("cpu_percent"), QStringLiteral("%")))
            .arg(sample.value(QStringLiteral("process_count")).toInt()));
    appRamValue_->setText(QStringLiteral("%1 WS · %2 private")
        .arg(number(QStringLiteral("ram_mb"), QStringLiteral(" МБ")), number(QStringLiteral("private_mb"), QStringLiteral(" МБ"))));
    appIoValue_->setText(QStringLiteral("↓ %1 · ↑ %2")
        .arg(number(QStringLiteral("read_mbps"), QStringLiteral(" МБ/с"), 2), number(QStringLiteral("write_mbps"), QStringLiteral(" МБ/с"), 2)));
    const auto hung = sample.value(QStringLiteral("ui_hung"));
    const QString windowState = hung.isBool() ? (hung.toBool() ? QStringLiteral("не отвечает") : QStringLiteral("отвечает"))
        : QStringLiteral("н/д");
    appWindowValue_->setText(windowState);
    appLiveDetails_->setField(QStringLiteral("threads"), QStringLiteral("Потоки / handles"), number(QStringLiteral("thread_count"), {}, 0)
        + QStringLiteral(" / ") + number(QStringLiteral("handle_count"), {}, 0));
    appLiveDetails_->setField(QStringLiteral("faults"), QStringLiteral("Page faults /с"), number(QStringLiteral("page_faults_per_sec")));
    appLiveDetails_->setField(QStringLiteral("commit"), QStringLiteral("Commit системы"), number(QStringLiteral("system_commit_used_percent"), QStringLiteral("%")));
    appLiveDetails_->setField(QStringLiteral("gpu"), QStringLiteral("GPU системы"), number(QStringLiteral("gpu_usage_percent"), QStringLiteral("%"))
        + QStringLiteral(" / ") + number(QStringLiteral("gpu_temperature_c"), QStringLiteral("°C")));
    appStatusValue_->setText(QStringLiteral("Наблюдаю: %1 / %2 сек").arg(number(QStringLiteral("elapsed"), {}, 0), number(QStringLiteral("duration"), {}, 0)));
    const auto quality = sample.value(QStringLiteral("system_data_quality")).toString();
    const auto qualityText = quality == QStringLiteral("valid") ? QStringLiteral("свежие")
        : quality == QStringLiteral("partial") ? QStringLiteral("частично доступны")
        : quality == QStringLiteral("stale") ? QStringLiteral("устарели — исключены") : QStringLiteral("н/д");
    appLiveDetails_->setField(QStringLiteral("quality"), QStringLiteral("Системные данные"), qualityText);

    const int row = appSampleTable_->rowCount();
    appSampleTable_->insertRow(row);
    const QStringList values {
        QStringLiteral("%1 сек").arg(sample.value(QStringLiteral("elapsed")).toDouble(), 0, 'f', 0),
        number(QStringLiteral("cpu_percent"), QStringLiteral("%")),
        number(QStringLiteral("ram_mb"), QStringLiteral(" МБ")),
        number(QStringLiteral("private_mb"), QStringLiteral(" МБ")),
        QString::number(sample.value(QStringLiteral("process_count")).toInt()),
        windowState,
    };
    for (int column = 0; column < values.size(); ++column)
    {
        appSampleTable_->setItem(row, column, new QTableWidgetItem(values[column]));
    }
    while (appSampleTable_->rowCount() > 50)
        appSampleTable_->removeRow(0);
    appSampleTable_->scrollToBottom();
}

void MainWindow::applyAppMonitorReport(const QJsonObject& report)
{
    latestAppMonitorReport_ = report;
    // One canonical representation for preview, full dialog, clipboard and TXT.
    latestAppMonitorReport_.insert(QStringLiteral("report_text"), orion::diagnostics::appMonitorReportToText(report));
    latestAppLiveSample_ = {};
    updateAppMonitorControls();
    appStopButton_->setEnabled(false);
    appSaveJsonButton_->setEnabled(true);
    appSaveTextButton_->setEnabled(true);
    appCopyButton_->setEnabled(true);
    appOpenReportButton_->setEnabled(true);
    appVerdictCard_->setSubtitle(orion::diagnostics::appMonitorVerdictText(report));
    const auto verdict = report.value(QStringLiteral("verdict")).toString();
    appVerdictCard_->setStatus(verdict == QStringLiteral("похоже норма") ? orion::core::StatusLevel::Ok
        : verdict == QStringLiteral("похоже, проблема здесь") ? orion::core::StatusLevel::Critical : orion::core::StatusLevel::Warning);
    appVerdictCard_->show();
    if (report.value(QStringLiteral("timed_out")).toBool()) appProgress_->setValue(100);
    appStatusValue_->setText(orion::diagnostics::appMonitorOutcomeText(report));
    appReportPreview_->setPlainText(latestAppMonitorReport_.value(QStringLiteral("report_text")).toString());
    diagnosticLoaded_ = false;
    if (settings_.trayNotificationsEnabled && trayIcon_->isVisible())
    {
        trayIcon_->showMessage(QStringLiteral("Наблюдение завершено"),
            QStringLiteral("%1 — %2").arg(
                QFileInfo(report.value(QStringLiteral("exe_path")).toString()).fileName(),
                report.value(QStringLiteral("verdict")).toString()),
            QSystemTrayIcon::Information, 6000);
    }
}

void MainWindow::saveAppMonitorReport(const bool jsonFormat)
{
    saveAppMonitorReportSnapshot(latestAppMonitorReport_, jsonFormat);
}

void MainWindow::openAppMonitorReport()
{
    if (latestAppMonitorReport_.isEmpty()) return;
    const auto snapshot = latestAppMonitorReport_;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("AppMonitorReportDialog"));
    dialog.setWindowTitle(QStringLiteral("Подробный отчёт наблюдения"));
    dialog.resize(920, 700);
    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QPlainTextEdit(&dialog);
    text->setObjectName(QStringLiteral("AppMonitorFullReport"));
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setFont(QFont(QStringLiteral("Consolas"), 10));
    text->setPlainText(snapshot.value(QStringLiteral("report_text")).toString());
    layout->addWidget(text, 1);
    auto* buttons = new QHBoxLayout;
    auto* copy = new QPushButton(QStringLiteral("Скопировать"), &dialog);
    auto* txt = new QPushButton(QStringLiteral("Сохранить TXT"), &dialog);
    auto* json = new QPushButton(QStringLiteral("Сохранить JSON"), &dialog);
    auto* close = new QPushButton(QStringLiteral("Закрыть"), &dialog);
    buttons->addWidget(copy); buttons->addWidget(txt); buttons->addWidget(json);
    buttons->addStretch(); buttons->addWidget(close); layout->addLayout(buttons);
    connect(copy, &QPushButton::clicked, &dialog, [snapshot] { QApplication::clipboard()->setText(snapshot.value(QStringLiteral("report_text")).toString()); });
    connect(txt, &QPushButton::clicked, &dialog, [this, snapshot] { saveAppMonitorReportSnapshot(snapshot, false); });
    connect(json, &QPushButton::clicked, &dialog, [this, snapshot] { saveAppMonitorReportSnapshot(snapshot, true); });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::saveAppMonitorReportSnapshot(QJsonObject report, const bool jsonFormat)
{
    if (report.isEmpty())
        return;
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить отчёт наблюдения"),
        QDir::home().filePath(
            jsonFormat ? QStringLiteral("orion_app_monitor.json") : QStringLiteral("orion_app_monitor.txt")),
        jsonFormat ? QStringLiteral("JSON (*.json)") : QStringLiteral("Текстовый отчёт (*.txt)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty()) {
        path += jsonFormat ? QStringLiteral(".json") : QStringLiteral(".txt");
        if (QFileInfo::exists(path) && QMessageBox::question(this, QStringLiteral("Заменить файл?"),
            QStringLiteral("Файл уже существует: %1\nЗаменить его?").arg(path),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    }
    auto payload = report;
    payload.remove(QStringLiteral("report_text"));
    const QByteArray data = jsonFormat
        ? QJsonDocument(payload).toJson(QJsonDocument::Indented)
        : report.value(QStringLiteral("report_text")).toString().toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        QMessageBox::warning(this, QStringLiteral("Не удалось сохранить отчёт"), file.errorString());
        return;
    }
    appStatusValue_->setText(QStringLiteral("Отчёт сохранён: %1").arg(path));
}

void MainWindow::finalizeIncident(const QJsonObject& suppliedPartial)
{
    if (suppliedPartial.value("incident_id").toString().isEmpty()
        || suppliedPartial.value("incident_id") != latestIncident_.value("incident_id")
        || latestIncident_.value("status") != "collecting") return;
    auto incident = suppliedPartial;
    const double marker = incident.value(QStringLiteral("marker_monotonic")).toDouble();
    const double pre = incident.value(QStringLiteral("pre_seconds")).toDouble(60.0);
    const double post = incident.value(QStringLiteral("post_seconds")).toDouble(15.0);
    QJsonArray input;
    for (const auto& sample : runtimeSamples_) input.append(sample);
    const auto normalized = orion::diagnostics::normalizeIncidentSamples(input, marker, pre, post);
    const auto summary = orion::diagnostics::summarizeIncidentSamples(input, marker, pre, post);
    const QString memoryPressure = summary.value(QStringLiteral("focus_memory_pressure")).toString();
    incident.insert(QStringLiteral("samples"), normalized.samples);
    incident.insert(QStringLiteral("summary"), summary);
    incident.insert(QStringLiteral("runtime_memory"),
        QJsonObject {
            { QStringLiteral("data_quality"), summary.value(QStringLiteral("paging_data_quality")) },
            { QStringLiteral("memory_pressure"), memoryPressure },
            { QStringLiteral("paging_activity"), summary.value(QStringLiteral("focus_paging_activity")) },
        });
    const QJsonArray findings
        = orion::diagnostics::analyzeSnapshot(QJsonObject { { QStringLiteral("incident"), incident } });
    incident.insert(QStringLiteral("findings"), findings);
    incident.insert(QStringLiteral("verdict_findings"), orion::diagnostics::selectVerdictFindings(findings));
    incident.insert(QStringLiteral("action_plan"), orion::diagnostics::buildActionPlan(findings));
    incident.insert(QStringLiteral("coverage"), orion::diagnostics::buildCoverageSummary(findings));
    latestIncident_ = incident;
    problemButton_->setEnabled(!paused_ && !incidentWorker_->isRunning());
    if (diagnosticIncidentButton_ != nullptr)
        diagnosticIncidentButton_->setEnabled(!paused_ && !incidentWorker_->isRunning());
    renderIncidentCard(incident);
    collectionStatus_->setText(QStringLiteral("⚡ Инцидент сохранён · %1")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
    diagnosticLoaded_ = false;
    if (tabWidget_->currentWidget() == diagnosticsPage_)
    {
        QTimer::singleShot(0, this, &MainWindow::startDiagnosticScan);
    }
}

void MainWindow::togglePaused() { setPaused(!paused_); }

void MainWindow::handleCurrentTabChanged()
{
    const auto* current = tabWidget_->currentWidget();
    if (processWorker_ != nullptr)
    {
        processWorker_->setActive(!paused_ && current == taskManagerPage_);
    }
    if (startupSequence_ != nullptr && startupSequence_->isActive())
        return;
    if (!paused_ && current == autostartPage_ && !autostartLoaded_ && autostartWorker_ != nullptr)
    {
        autostartWorker_->scan();
    }
    if (!paused_ && current == diagnosticsPage_ && !diagnosticLoaded_ && diagnosticWorker_ != nullptr)
    {
        startDiagnosticScan();
    }
    if (!paused_ && current == hardwarePage_ && !hardwareLoaded_ && hardwareWorker_ != nullptr
        && !hardwareWorker_->isRunning())
    {
        startHardwareScan();
    }
}

void MainWindow::syncOptionalTabs()
{
    if (settings_.serverTabEnabled && serverPanel_ == nullptr)
    {
        const QString profilePath
            = QFileInfo(settingsPath_).absoluteDir().filePath(QStringLiteral("server_profiles.db"));
        serverPanel_ = new ServerMonitorWidget(profilePath, tabWidget_);
        const int networkIndex = tabWidget_->indexOf(networkPage_);
        tabWidget_->insertTab(networkIndex + 1, serverPanel_, QStringLiteral("Серверы"));
        serverPanel_->setGlobalPaused(paused_);
    }
    else if (!settings_.serverTabEnabled && serverPanel_ != nullptr)
    {
        auto* panel = serverPanel_;
        serverPanel_ = nullptr;
        panel->shutdown();
        const int index = tabWidget_->indexOf(panel);
        if (index >= 0)
            tabWidget_->removeTab(index);
        panel->deleteLater();
    }

    if (settings_.terminalEnabled && terminalPanel_ == nullptr)
    {
        terminalPanel_ = new TerminalWidget(tabWidget_);
        const auto palette = paletteFor(settings_);
        terminalPanel_->refreshTheme(palette.accent, palette.font);
        tabWidget_->addTab(terminalPanel_, QStringLiteral("Терминал"));
    }
    else if (!settings_.terminalEnabled && terminalPanel_ != nullptr)
    {
        auto* panel = terminalPanel_;
        terminalPanel_ = nullptr;
        panel->terminateShell();
        const int index = tabWidget_->indexOf(panel);
        if (index >= 0)
            tabWidget_->removeTab(index);
        panel->deleteLater();
    }
    else if (terminalPanel_ != nullptr)
    {
        const auto palette = paletteFor(settings_);
        terminalPanel_->refreshTheme(palette.accent, palette.font);
    }
    handleCurrentTabChanged();
}

void MainWindow::syncGamerMode(const bool enabled)
{
    const bool wasActive = gamerModeActive_ || (gamerOverlay_ != nullptr && gamerOverlay_->isVisible());
    gamerModeActive_ = enabled;
    if (gamerMode_ != nullptr)
    {
        gamerMode_->blockSignals(true);
        gamerMode_->setChecked(enabled);
        gamerMode_->blockSignals(false);
    }

    if (enabled)
    {
        if (gamerOverlay_ == nullptr)
        {
            gamerOverlay_ = new GamerOverlay(settings_.pingTarget);
            connect(gamerOverlay_, &GamerOverlay::closedByUser, this, &MainWindow::restoreFromGamerOverlay);
        }
        const auto palette = paletteFor(settings_);
        gamerOverlay_->applyTheme(
            palette.panel, palette.text, palette.accent, palette.secondary, palette.border, palette.font);
        gamerOverlay_->setPingTarget(settings_.pingTarget);
        gamerOverlay_->setCriticalMetrics(cpuCriticalState_, gpuCriticalState_, ramCriticalState_);
        gamerOverlay_->setAlarmState(cpuCriticalState_ || gpuCriticalState_ || ramCriticalState_);
        gamerOverlay_->setMonitoringPaused(paused_);
        gamerOverlay_->updateTelemetry(latestCpuPercent_, latestGpuPercent_, latestRamPercent_,
            latestDownloadBytesPerSecond_, latestUploadBytesPerSecond_);
        QApplication::setQuitOnLastWindowClosed(false);
        hide();
        gamerOverlay_->show();
        gamerOverlay_->raise();
        gamerOverlay_->activateWindow();
        return;
    }

    if (gamerOverlay_ != nullptr)
        gamerOverlay_->hide();
    if (wasActive)
    {
        showNormal();
        raise();
        activateWindow();
    }
    updateTrayVisibility();
}

void MainWindow::restoreFromGamerOverlay()
{
    auto* closedOverlay = gamerOverlay_;
    gamerOverlay_ = nullptr;
    gamerModeActive_ = false;
    if (gamerMode_ != nullptr)
    {
        gamerMode_->blockSignals(true);
        gamerMode_->setChecked(false);
        gamerMode_->blockSignals(false);
    }
    showNormal();
    raise();
    activateWindow();
    updateTrayVisibility();
    if (closedOverlay != nullptr)
        closedOverlay->deleteLater();
}

void MainWindow::setPaused(const bool paused)
{
    if (paused_ != paused) {
        QElapsedTimer boundary;
        boundary.start();
        incidentNotBeforeMs_ = boundary.msecsSinceReference();
    }
    paused_ = paused;
    if (processRefreshButton_ != nullptr)
        processRefreshButton_->setEnabled(!paused_);
    updateProcessActions();
    if (deepScanWorker_ != nullptr && deepScanWorker_->isRunning() && paused)
        deepScanWorker_->requestStop();
    if (fullScanWorker_ != nullptr && fullScanWorker_->isRunning() && paused)
        fullScanWorker_->requestStop();
    const bool longScanRunning = (deepScanWorker_ != nullptr && deepScanWorker_->isRunning())
        || (fullScanWorker_ != nullptr && fullScanWorker_->isRunning());
    if (deepScanButton_ != nullptr)
        deepScanButton_->setEnabled(!paused && !longScanRunning);
    if (fullScanButton_ != nullptr)
        fullScanButton_->setEnabled(!paused && !longScanRunning);
    if (startupSequence_ != nullptr)
        startupSequence_->setPaused(paused);
    if (networkTrafficChart_ != nullptr)
        networkTrafficChart_->setMonitoringPaused(paused);
    if (telemetryWorker_ != nullptr)
    {
        telemetryWorker_->setPaused(paused_);
    }
    if (pingWorker_ != nullptr)
    {
        pingWorker_->setPaused(paused_);
    }
    if (paused_ && networkScannerWorker_ != nullptr && networkScannerWorker_->isRunning())
    {
        stopNetworkScan(false);
    }
    else if (networkScanButton_ != nullptr)
    {
        restoreNetworkScanControls();
        if (paused_)
            networkScanStatus_->setText(QStringLiteral("Мониторинг приостановлен"));
        else if (networkScannerWorker_ != nullptr && !networkScannerWorker_->isRunning())
        {
            updateSelectedNetworkInterface();
        }
    }
    if (paused_ && internetToolsWorker_ != nullptr && internetToolsWorker_->isRunning())
    {
        internetToolsStatus_->setText(QStringLiteral("Интернет-проверка останавливается из-за паузы…"));
        internetToolsWorker_->requestStop();
    }
    if (publicIpRefreshButton_ != nullptr && speedTestButton_ != nullptr)
    {
        restoreInternetControls();
    }
    if (processWorker_ != nullptr)
    {
        processWorker_->setActive(!paused_ && tabWidget_->currentWidget() == taskManagerPage_);
    }
    if (autostartRefreshButton_ != nullptr)
    {
        autostartRefreshButton_->setEnabled(
            !paused_ && (autostartWorker_ == nullptr || !autostartWorker_->isRunning()));
    }
    if (!paused_ && tabWidget_->currentWidget() == autostartPage_ && !autostartLoaded_
        && autostartWorker_ != nullptr)
    {
        autostartWorker_->scan();
    }
    if (diagnosticScanButton_ != nullptr)
    {
        diagnosticScanButton_->setEnabled(!paused_
            && (deepScanWorker_ == nullptr || !deepScanWorker_->isRunning())
            && (fullScanWorker_ == nullptr || !fullScanWorker_->isRunning())
            && (diagnosticWorker_ == nullptr || !diagnosticWorker_->isRunning()));
    }
    if (!paused_ && tabWidget_->currentWidget() == diagnosticsPage_ && !diagnosticLoaded_
        && diagnosticWorker_ != nullptr)
    {
        startDiagnosticScan();
    }
    if (hardwareRefreshButton_ != nullptr)
    {
        hardwareRefreshButton_->setEnabled(
            !paused_ && (hardwareWorker_ == nullptr || !hardwareWorker_->isRunning()));
    }
    if (paused_ && hardwareWorker_ != nullptr && hardwareWorker_->isRunning())
    {
        hardwareScanPending_ = true;
        hardwareStatus_->setText(QStringLiteral("Сбор характеристик останавливается из-за паузы…"));
        hardwareWorker_->stop();
    }
    else if (!paused_ && tabWidget_->currentWidget() == hardwarePage_ && !hardwareLoaded_
        && hardwareWorker_ != nullptr && !hardwareWorker_->isRunning())
    {
        startHardwareScan();
    }
    if (paused_ && stressWorker_ != nullptr && stressWorker_->isRunning())
    {
        stressWorker_->requestStop();
    }
    if (stressStartButton_ != nullptr && (stressWorker_ == nullptr || !stressWorker_->isRunning()))
    {
        stressStartButton_->setEnabled(
            !paused_ && (deepScanWorker_ == nullptr || !deepScanWorker_->isRunning())
            && (fullScanWorker_ == nullptr || !fullScanWorker_->isRunning()));
    }
    if (appCpuChart_) appCpuChart_->setMonitoringPaused(paused_);
    if (appMemoryChart_) appMemoryChart_->setMonitoringPaused(paused_);
    if (appMonitorWorker_ != nullptr && appMonitorWorker_->isRunning())
    {
        appMonitorWorker_->setPaused(paused_);
        if (appStatusValue_ != nullptr)
        {
            appStatusValue_->setText(paused_
                    ? QStringLiteral("Наблюдение приостановлено; приложение продолжает работать")
                    : QStringLiteral("Наблюдение возобновляется…"));
        }
    }
    updateAppMonitorControls();
    if (problemButton_ != nullptr)
    {
        problemButton_->setEnabled(!paused_ && (incidentWorker_ == nullptr || !incidentWorker_->isRunning()));
    }
    if (diagnosticIncidentButton_ != nullptr)
    {
        diagnosticIncidentButton_->setEnabled(
            !paused_ && (incidentWorker_ == nullptr || !incidentWorker_->isRunning()));
    }
    if (deepTelemetryDialog_ != nullptr)
    {
        deepTelemetryDialog_->setMonitoringPaused(paused_);
    }
    if (serverPanel_ != nullptr)
    {
        serverPanel_->setGlobalPaused(paused_);
    }
    if (gamerOverlay_ != nullptr)
    {
        gamerOverlay_->setMonitoringPaused(paused_);
    }
    if (networkPingValue_ != nullptr && paused_)
    {
        networkPingValue_->setText(QStringLiteral("пауза · %1").arg(settings_.pingTarget));
    }
    pauseButton_->setChecked(paused_);
    const bool denseLayout = settings_.themeKey == QStringLiteral("slate_minimal");
    pauseButton_->setText(paused_
            ? (denseLayout ? QStringLiteral("Продолжить") : QStringLiteral("▶ Продолжить"))
            : (denseLayout ? QStringLiteral("Пауза") : QStringLiteral("⏸ Пауза")));
    trayPauseAction_->setText(paused_ ? QStringLiteral("▶ Продолжить") : QStringLiteral("⏸ Пауза"));
    pauseBanner_->setVisible(paused_);
    collectionStatus_->setText(
        paused_ ? QStringLiteral("Мониторинг остановлен") : QStringLiteral("Сбор возобновляется…"));
}

void MainWindow::openSettings()
{
    if (settingsDialog_ == nullptr)
    {
        settingsDialog_ = new QDialog(this);
        settingsDialog_->setObjectName(QStringLiteral("SettingsDialog"));
        settingsDialog_->setWindowTitle(QStringLiteral("Настройки O.R.I.O.N."));
        settingsDialog_->setMinimumSize(720, 620);
        settingsDialog_->resize(780, 680);
        auto* layout = new QVBoxLayout(settingsDialog_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(createSettingsPage());
    }
    settingsDialog_->show();
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
}

void MainWindow::openDeepTelemetry()
{
    if (deepTelemetryDialog_ == nullptr)
    {
        deepTelemetryDialog_ = new DeepTelemetryDialog(this);
        connect(deepTelemetryDialog_, &QObject::destroyed, this, [this] { deepTelemetryDialog_ = nullptr; });
    }
    deepTelemetryDialog_->applyTelemetry(latestCpuCores_, latestCpuCoreFrequenciesMhz_,
        latestCpuFrequencyMhz_, sessionPeaks_.uptimeSeconds(), sessionPeaks_.peaks());
    deepTelemetryDialog_->setMonitoringPaused(paused_);
    deepTelemetryDialog_->show();
    deepTelemetryDialog_->raise();
    deepTelemetryDialog_->activateWindow();
}

void MainWindow::markProblemNow()
{
    if (paused_ || incidentWorker_ == nullptr || incidentWorker_->isRunning())
        return;
    const auto steadyMarker = std::chrono::steady_clock::now();
    const double sessionMarker = static_cast<double>(sessionTimer_.elapsed()) / 1000.0;
    const auto timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    qInfo().noquote() << QStringLiteral("Пользователь поставил метку проблемы: %1").arg(timestamp);
    problemButton_->setEnabled(false);
    if (diagnosticIncidentButton_ != nullptr)
        diagnosticIncidentButton_->setEnabled(false);
    const QString incidentId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    latestIncident_ = QJsonObject {
        { QStringLiteral("schema_version"), 2 },
        { QStringLiteral("incident_id"), incidentId },
        { QStringLiteral("status"), QStringLiteral("collecting") },
        { QStringLiteral("marker_timestamp"), timestamp },
        { QStringLiteral("marker_monotonic"), sessionMarker },
        { QStringLiteral("pre_seconds"), 60.0 }, { QStringLiteral("post_seconds"), 15.0 },
        { QStringLiteral("log_pre_seconds"), 120.0 },
    };
    renderIncidentCard(latestIncident_);
    incidentStatusValue_->setText(
        QStringLiteral("собирается окно после события · инцидент %1").arg(incidentId));
    diagnosticIncidentCard_->setSubtitle(QStringLiteral(
        "Не закрывайте O.R.I.O.N.: собираются новые системные события и показатели восстановления."));
    incidentWorker_->capture(IncidentOptions {
        incidentId,
        timestamp,
        sessionMarker,
        60.0,
        15.0,
        steadyMarker,
    });
}

void MainWindow::applySettings()
{
    const bool requestedGamerMode = gamerMode_->isChecked();
    settings_.themeKey = themeCombo_->currentData().toString();
    settings_.trayIconEnabled = trayEnabled_->isChecked();
    settings_.trayNotificationsEnabled = notificationsEnabled_->isChecked();
    settings_.alwaysOnTop = alwaysOnTop_->isChecked();
    settings_.freeFormResize = freeFormResize_->isChecked();
    settings_.windowOpacity = static_cast<double>(opacitySlider_->value()) / 100.0;
    settings_.minimumWidth = minimumWidth_->value();
    settings_.minimumHeight = minimumHeight_->value();
    settings_.maximumWidth = qMax(maximumWidth_->value(), settings_.minimumWidth);
    settings_.maximumHeight = qMax(maximumHeight_->value(), settings_.minimumHeight);
    maximumWidth_->setValue(settings_.maximumWidth);
    maximumHeight_->setValue(settings_.maximumHeight);
    settings_.cardCpuVisible = cardCpuVisible_->isChecked();
    settings_.cardGpuVisible = cardGpuVisible_->isChecked();
    settings_.cardRamVisible = cardRamVisible_->isChecked();
    settings_.cardDiskVisible = cardDiskVisible_->isChecked();
    settings_.cardNetVisible = cardNetVisible_->isChecked();
    settings_.cardDragDropEnabled = cardDragDropEnabled_->isChecked();
    settings_.terminalEnabled = terminalEnabled_->isChecked();
    settings_.serverTabEnabled = serverTabEnabled_->isChecked();
    settings_.alertFreezeDisabled = alertFreezeDisabled_->isChecked();
    settings_.pingTarget = pingTarget_->text().trimmed();
    if (settings_.pingTarget.isEmpty())
    {
        settings_.pingTarget = QStringLiteral("8.8.8.8");
        pingTarget_->setText(settings_.pingTarget);
    }
    settings_.customThemeColors = QJsonObject {
        { QStringLiteral("bg"), customBackground_->property("selectedColor").toString() },
        { QStringLiteral("text"), customText_->property("selectedColor").toString() },
        { QStringLiteral("accent"), customAccent_->property("selectedColor").toString() },
        { QStringLiteral("graph"), customGraph_->property("selectedColor").toString() },
    };
    applyStyle();
    syncOptionalTabs();
    setWindowOpacity(settings_.windowOpacity);
    applyWindowConstraints();
    applyOverviewSettings();
    if (pingWorker_ != nullptr)
    {
        pingWorker_->setTarget(settings_.pingTarget);
    }
    latestPing_.target = settings_.pingTarget;
    latestPing_.quality = QStringLiteral("unavailable");
    latestPing_.latencyMs = -1.0;
    latestPing_.reason = QStringLiteral("ожидание пробы новой цели");
    updateNetworkPresentation();
    const bool wasVisible = isVisible();
    if (wasVisible)
        hide();
    setWindowFlag(Qt::WindowStaysOnTopHint, settings_.alwaysOnTop);
    if (wasVisible)
    {
        show();
        raise();
        activateWindow();
    }
    updateTrayVisibility();
    saveSettings();
    collectionStatus_->setText(QStringLiteral("Настройки сохранены"));
    syncGamerMode(requestedGamerMode);
}

void MainWindow::applyWindowConstraints()
{
    if (settings_.freeFormResize)
    {
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        return;
    }
    setMinimumSize(settings_.minimumWidth, settings_.minimumHeight);
    setMaximumSize(settings_.maximumWidth, settings_.maximumHeight);
}

void MainWindow::applyOverviewSettings()
{
    const QHash<QString, bool> visibility {
        { QStringLiteral("OverviewDock_cpu"), settings_.cardCpuVisible },
        { QStringLiteral("OverviewDock_gpu"), settings_.cardGpuVisible },
        { QStringLiteral("OverviewDock_ram"), settings_.cardRamVisible },
        { QStringLiteral("OverviewDock_disk"), settings_.cardDiskVisible },
        { QStringLiteral("OverviewDock_net"), settings_.cardNetVisible },
    };
    const auto features = settings_.cardDragDropEnabled
        ? QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
        : QDockWidget::NoDockWidgetFeatures;
    for (auto* dock : overviewDocks_)
    {
        if (dock == nullptr)
            continue;
        dock->setFeatures(features);
        dock->setVisible(visibility.value(dock->objectName(), true));
    }
}

void MainWindow::applyThemeBehavior()
{
    const bool gaugeStyle = settings_.themeKey == QStringLiteral("quantum_cyan");
    const bool denseLayout = settings_.themeKey == QStringLiteral("slate_minimal");
    const auto palette = paletteFor(settings_);
    const auto percentageMode = gaugeStyle ? MetricCard::PresentationMode::Gauge
        : denseLayout                      ? MetricCard::PresentationMode::Dense
                                           : MetricCard::PresentationMode::Standard;
    const auto textMode
        = denseLayout ? MetricCard::PresentationMode::Dense : MetricCard::PresentationMode::Standard;

    for (auto* card : { cpuCard_, gpuCard_, ramCard_ })
    {
        if (card == nullptr)
            continue;
        card->setThemeColors(QColor(palette.accent), QColor(palette.border), QColor(palette.text));
        card->setPresentationMode(percentageMode);
    }
    for (auto* card : { diskCard_, networkCard_ })
    {
        if (card == nullptr)
            continue;
        card->setThemeColors(QColor(palette.accent), QColor(palette.border), QColor(palette.text));
        card->setPresentationMode(textMode);
    }

    overviewPage_->setProperty("themeLayoutMode",
        gaugeStyle        ? QStringLiteral("gauge")
            : denseLayout ? QStringLiteral("dense")
                          : QStringLiteral("standard"));
    networkPage_->setProperty("denseLayout", denseLayout);
    if (networkTrafficChart_ != nullptr)
        networkTrafficChart_->setVisible(!denseLayout);
    tabWidget_->setProperty("denseLayout", denseLayout);
    for (auto* table : findChildren<QTableWidget*>())
    {
        table->verticalHeader()->setDefaultSectionSize(denseLayout ? 22 : 30);
    }

    for (auto* button : findChildren<QPushButton*>(QStringLiteral("ToolbarButton")))
    {
        if (button == pauseButton_)
            continue;
        if (!button->property("expandedThemeText").isValid())
        {
            button->setProperty("expandedThemeText", button->text());
        }
        const QString expanded = button->property("expandedThemeText").toString();
        button->setText(denseLayout && expanded.contains(QLatin1Char(' '))
                ? expanded.section(QLatin1Char(' '), 1)
                : expanded);
    }
    if (!problemButton_->property("expandedThemeText").isValid())
    {
        problemButton_->setProperty("expandedThemeText", problemButton_->text());
    }
    const QString problemText = problemButton_->property("expandedThemeText").toString();
    problemButton_->setText(denseLayout && problemText.contains(QLatin1Char(' '))
            ? problemText.section(QLatin1Char(' '), 1)
            : problemText);
    setPaused(paused_);
    pauseBanner_->setText(denseLayout
            ? QStringLiteral("Мониторинг приостановлен — фоновые измерения и тревоги не выполняются")
            : QStringLiteral("⏸ Мониторинг приостановлен — фоновые измерения и тревоги не выполняются"));
}

void MainWindow::resetOverviewLayout()
{
    if (overviewDockHost_ == nullptr || overviewDocks_.size() != 5)
        return;
    returnFloatingOverviewCards();
    for (auto* dock : overviewDocks_)
        overviewDockHost_->removeDockWidget(dock);
    QDockWidget* cpu = nullptr;
    QDockWidget* gpu = nullptr;
    QDockWidget* ram = nullptr;
    QDockWidget* disk = nullptr;
    QDockWidget* network = nullptr;
    for (auto* dock : overviewDocks_)
    {
        if (dock->objectName() == QStringLiteral("OverviewDock_cpu"))
            cpu = dock;
        else if (dock->objectName() == QStringLiteral("OverviewDock_gpu"))
            gpu = dock;
        else if (dock->objectName() == QStringLiteral("OverviewDock_ram"))
            ram = dock;
        else if (dock->objectName() == QStringLiteral("OverviewDock_disk"))
            disk = dock;
        else if (dock->objectName() == QStringLiteral("OverviewDock_net"))
            network = dock;
    }
    if (cpu == nullptr || gpu == nullptr || ram == nullptr || disk == nullptr || network == nullptr)
        return;
    overviewDockHost_->addDockWidget(Qt::TopDockWidgetArea, cpu);
    overviewDockHost_->splitDockWidget(cpu, network, Qt::Vertical);
    overviewDockHost_->splitDockWidget(cpu, gpu, Qt::Horizontal);
    overviewDockHost_->splitDockWidget(cpu, ram, Qt::Vertical);
    overviewDockHost_->splitDockWidget(gpu, disk, Qt::Vertical);
    settings_.cardDockState.clear();
    applyOverviewSettings();
}

void MainWindow::resetDiagnosticLayout()
{
    if (!diagnosticDockHost_ || diagnosticDocks_.size() != 3) return;
    resetDiagnosticDockLayout(*diagnosticDockHost_,
        {diagnosticDocks_[0], diagnosticDocks_[1], diagnosticDocks_[2]});
    settings_.diagnosticHubDockState.clear();
}

void MainWindow::updateTrayVisibility()
{
    const bool enabled = settings_.trayIconEnabled && QSystemTrayIcon::isSystemTrayAvailable();
    trayIcon_->setVisible(enabled);
    QApplication::setQuitOnLastWindowClosed(!enabled);
}

void MainWindow::saveSettings()
{
    if (overviewDockHost_ != nullptr)
    {
        settings_.cardDockState = QString::fromLatin1(overviewDockHost_->saveState(1).toBase64());
    }
    if (diagnosticDockHost_ != nullptr)
        settings_.diagnosticHubDockState = QString::fromLatin1(diagnosticDockHost_->saveState(1).toBase64());
    QString error;
    if (!settings_.save(settingsPath_, &error))
    {
        qWarning().noquote() << QStringLiteral("Настройки не сохранены: %1").arg(error);
    }
}

void MainWindow::returnFloatingOverviewCards()
{
    for (auto* dock : overviewDocks_)
    {
        if (dock != nullptr && dock->isFloating())
        {
            dock->setFloating(false);
        }
    }
}

void MainWindow::returnFloatingDiagnosticPanels()
{
    for (auto* dock : diagnosticDocks_)
    {
        if (dock != nullptr && dock->isFloating())
        {
            dock->setFloating(false);
        }
    }
}

void MainWindow::showFromTray()
{
    if (gamerModeActive_ && gamerOverlay_ != nullptr)
    {
        gamerOverlay_->show();
        gamerOverlay_->raise();
        gamerOverlay_->activateWindow();
        return;
    }
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::requestQuit()
{
    quitting_ = true;
    returnFloatingOverviewCards();
    returnFloatingDiagnosticPanels();
    saveSettings();
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!quitting_ && trayIcon_ != nullptr && trayIcon_->isVisible())
    {
        saveSettings();
        hide();
        event->ignore();
        return;
    }
    returnFloatingOverviewCards();
    returnFloatingDiagnosticPanels();
    saveSettings();
    event->accept();
}

void MainWindow::maybeNotify(const double cpuPercent, const double ramPercent)
{
    const bool cpuHigh = cpuPercent >= 95.0;
    const bool ramHigh = ramPercent >= 95.0;
    if (settings_.trayNotificationsEnabled && trayIcon_->isVisible())
    {
        if (cpuHigh && !cpuAlarmActive_)
        {
            trayIcon_->showMessage(QStringLiteral("Высокая загрузка CPU"),
                QStringLiteral("CPU достиг %1%. Сбор данных продолжается.").arg(cpuPercent, 0, 'f', 1),
                QSystemTrayIcon::Warning, 5000);
        }
        if (ramHigh && !ramAlarmActive_)
        {
            trayIcon_->showMessage(QStringLiteral("Высокое использование RAM"),
                QStringLiteral("RAM достигла %1%. Проверьте процессы-лидеры.").arg(ramPercent, 0, 'f', 1),
                QSystemTrayIcon::Warning, 5000);
        }
    }
    cpuAlarmActive_ = cpuHigh;
    ramAlarmActive_ = ramHigh;
}

void MainWindow::applyStyle()
{
    const ThemePalette palette = paletteFor(settings_);
    QString styleSheet = QStringLiteral(R"(
        QMainWindow, QWidget { background: @BG@; color: @TEXT@; font-family: @FONT@; font-size: 13px; }
        QLabel, QFrame#CpuCoreChart, QWidget#MetricSparkline { background: transparent; }
        QLabel#Header { font-size: 26px; font-weight: 700; color: @ACCENT@; padding: 0px; }
        QWidget#OverviewPage QLabel#Header, QWidget#NetworkPage QLabel#Header { font-size: 15px; margin-bottom: 8px; }
        QMainWindow#DiagnosticsHubHost QLabel#Header { font-size: 15px; }
        QLabel#DeepScanVerdict { background: @PANEL@; color: @TEXT@; border-left: 4px solid @ACCENT@; padding: 8px; font-weight: 600; }
        QPlainTextEdit#DeepScanLog { background: @PANEL@; color: @TEXT@; border: 1px solid @BORDER@; }
        QLabel#PageSubtitle, QLabel#FieldLabel, QLabel#SensorEmptyState, QLabel#DiskEmptyState { color: @MUTED@; }
        QLabel#SectionHeading { font-size: 13px; font-weight: 700; color: @ACCENT@; }
        QLabel#CollectionStatus, QLabel#SettingsNote, QLabel#MetricCardDetail, QLabel#CardDetail { color: @MUTED@; }
        QLabel#DetailsTitle { font-size: 18px; font-weight: 700; }
        QLabel#FieldValue, QLabel[networkValue="true"] { color: @TEXT@; font-size: 14px; font-weight: 700; }
        QLabel#CardTitle { color: @MUTED@; font-size: 15px; font-weight: 700; letter-spacing: 1.5px; }
        QLabel#FieldLabel, QLabel#CardDetail, QLabel#MetricCardDetail { font-size: 14px; }
        QFrame#MetricCard { background: @PANEL@; border: 1px solid @BORDER@; border-radius: 12px; }
        QLabel#MetricCardTitle { color: @MUTED@; font-size: 13px; font-weight: 700; letter-spacing: 1px; }
        QLabel#MetricCardValue { font-size: 34px; font-weight: 700; }
        QMainWindow#OverviewDockHost, QMainWindow#DiagnosticsHubHost { background: transparent; }
        QMainWindow#OverviewDockHost::separator { width: 10px; height: 10px; background: @BG@; }
        QMainWindow#OverviewDockHost::separator:hover { background: @BORDER@; }
        QDockWidget { color: @MUTED@; font-size: 13px; font-weight: 700; }
        QDockWidget::title { background: @PANEL@; border: 1px solid @BORDER@; border-top-left-radius: 10px; border-top-right-radius: 10px; padding: 6px 10px; }
        QDockWidget::float-button, QDockWidget::close-button { background: transparent; border: none; }
        QToolBar#MainToolbar { background: @BG@; border: none; border-bottom: 1px solid @BORDER@; spacing: 6px; padding: 5px 8px; }
        QToolBar#MainToolbar::separator { background: @BORDER@; width: 1px; margin: 5px 8px; }
        QLabel#AlarmBanner { background: #FF3B30; color: white; padding: 8px 12px; font-weight: 700; }
        QLabel#PauseBanner { background: @PANEL@; border-bottom: 1px solid @BORDER@; padding: 7px 12px; font-weight: 600; }
        QPushButton { background: @PANEL@; color: @TEXT@; border: 1px solid @ACCENT@; border-radius: 5px; padding: 6px 14px; font-weight: 600; }
        QPushButton:hover { background: @ACCENT@; color: @BG@; }
        QPushButton:disabled { color: @MUTED@; background: @BG@; border-color: @BORDER@; }
        QPushButton#ToolbarButton { background: transparent; border-color: transparent; padding: 5px 10px; }
        QPushButton#ToolbarButton:hover { background: @PANEL@; border-color: @BORDER@; color: @ACCENT@; }
        QPushButton#ToolbarButton:checked { background: @ACCENT@; color: @BG@; }
        QPushButton#DangerButton { background: transparent; color: #FF3B30; border-color: #FF3B30; }
        QPushButton#DangerButton:hover { background: #FF3B30; color: white; }
        QPushButton#ErrorsBadge { color: @TEXT@; border-color: @BORDER@; border-radius: 10px; padding: 5px 12px; }
        QPushButton#CardBadge { color: @MUTED@; font-size: 11px; border-color: @BORDER@; border-radius: 6px; padding: 1px 6px; }
        QPushButton#CardBadge:hover { color: @TEXT@; background: @PANEL@; border-color: @ACCENT@; }
        QPushButton#PrimaryButton, QPushButton#SettingsApplyButton { background: @ACCENT@; color: @BG@; font-weight: 700; }
        QGroupBox { border: 1px solid @BORDER@; border-radius: 7px; margin-top: 10px; padding-top: 10px; }
        QGroupBox::title { color: @ACCENT@; subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        QTabWidget::pane { border: 1px solid @BORDER@; background: @BG@; }
        QTabBar::tab { background: @BG@; color: @MUTED@; min-height: 22px; padding: 8px 12px; border: none; border-bottom: 2px solid transparent; font-weight: 600; }
        QTabBar::tab:hover:!selected { color: @TEXT@; border-color: @ACCENT@; }
        QTabBar::tab:selected { color: @ACCENT@; background: @BG@; border-bottom: 2px solid @ACCENT@; }
        QTableWidget, QLineEdit, QComboBox, QSpinBox, QTextEdit { background: @PANEL@; color: @TEXT@; border: 1px solid @BORDER@; border-radius: 5px; padding: 5px; }
        QTableWidget { alternate-background-color: @BG@; gridline-color: @BORDER@; selection-background-color: @ACCENT@; selection-color: @BG@; }
        QHeaderView::section { background: @PANEL@; color: @MUTED@; padding: 7px 8px; border: none; border-bottom: 1px solid @BORDER@; font-weight: 600; }
        QTableWidget#NetworkDeviceTable QHeaderView::section { font-size: 12px; padding: 3px 4px; }
        QScrollBar:vertical { background: @BG@; width: 10px; margin: 0px; }
        QScrollBar::handle:vertical { background: @BORDER@; min-height: 28px; border-radius: 5px; }
        QScrollBar::handle:vertical:hover { background: @MUTED@; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QProgressBar { min-height: 18px; background: @BG@; border: 1px solid @BORDER@; border-radius: 4px; text-align: center; }
        QProgressBar::chunk { background: @ACCENT@; border-radius: 3px; }
        QCheckBox { spacing: 8px; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 2px solid @ACCENT@; border-radius: 3px; background: @BG@; }
        QCheckBox::indicator:checked { background: @ACCENT@; }
        QCheckBox::indicator:disabled { border-color: @MUTED@; background: @PANEL@; }
        QSlider::groove:horizontal { background: @BORDER@; height: 6px; border-radius: 3px; }
        QSlider::handle:horizontal { background: @ACCENT@; width: 16px; margin: -6px 0; border-radius: 8px; }
        QSlider::sub-page:horizontal { background: @ACCENT@; border-radius: 3px; }
    )");
    if (settings_.themeKey == QStringLiteral("quantum_cyan"))
    {
        styleSheet += QStringLiteral(R"(
            QFrame#MetricCard { border-color: @ACCENT@; }
            QProgressBar { min-height: 20px; border: 1px solid @ACCENT@; border-radius: 8px; font-weight: 700; }
            QProgressBar::chunk { border-radius: 7px; background: qlineargradient(
                x1:0, y1:0, x2:1, y2:0, stop:0 @ACCENT@, stop:1 @SECONDARY@); }
        )");
    }
    else if (settings_.themeKey == QStringLiteral("slate_minimal"))
    {
        styleSheet += QStringLiteral(R"(
            QLabel#Header { font-size: 20px; padding: 2px 1px 4px 1px; }
            QFrame#MetricCard, QPushButton, QGroupBox, QTableWidget, QLineEdit,
            QComboBox, QSpinBox, QTextEdit, QProgressBar { border-radius: 0px; }
            QLabel#MetricCardValue { font-size: 24px; }
            QDockWidget { font-size: 12px; }
            QDockWidget::title { padding: 3px 6px; }
            QToolBar#MainToolbar { spacing: 2px; padding: 2px 4px; }
            QPushButton#ToolbarButton { padding: 3px 7px; }
            QTabBar::tab { min-height: 18px; padding: 4px 9px; }
            QHeaderView::section { padding: 3px 5px; }
            QTableWidget { gridline-color: @BORDER@; padding: 2px; }
            QProgressBar { min-height: 14px; border: 1px solid @BORDER@; }
            QProgressBar::chunk { border-radius: 0px; background: @ACCENT@; }
        )");
    }
    styleSheet.replace(QStringLiteral("@BG@"), palette.background);
    styleSheet.replace(QStringLiteral("@PANEL@"), palette.panel);
    styleSheet.replace(QStringLiteral("@TEXT@"), palette.text);
    styleSheet.replace(QStringLiteral("@MUTED@"), palette.muted);
    styleSheet.replace(QStringLiteral("@ACCENT@"), palette.accent);
    styleSheet.replace(QStringLiteral("@SECONDARY@"), palette.secondary);
    styleSheet.replace(QStringLiteral("@BORDER@"), palette.border);
    styleSheet.replace(QStringLiteral("@FONT@"), palette.font);
    setStyleSheet(styleSheet);
    detailsPanel_->setThemeColors(QColor(palette.accent), QColor(palette.muted));
    if (diagnosticDockHost_ != nullptr)
    {
        for (auto* card : diagnosticDockHost_->findChildren<DetailCard*>())
        {
            if (card->property("diagnosticSection").isValid())
                card->setThemeColors(QColor(palette.accent), QColor(palette.muted));
        }
    }
    applyThemeBehavior();
    for (auto* widget : findChildren<QWidget*>())
    {
        if (auto* sparkline = dynamic_cast<SparklineWidget*>(widget))
        {
            sparkline->setAccentColor(QColor(palette.graph));
        }
    }
    if (cpuCoreChart_ != nullptr)
    {
        cpuCoreChart_->setThemeColors(QColor(palette.background), QColor(palette.panel), QColor(palette.text),
            QColor(palette.muted), QColor(palette.graph), QColor(palette.border));
    }
    if (networkTrafficChart_ != nullptr)
    {
        networkTrafficChart_->setThemeColors(QColor(palette.background), QColor(palette.panel),
            QColor(palette.text), QColor(palette.muted), QColor(palette.accent), QColor(palette.border),
            settings_.themeKey == QStringLiteral("quantum_cyan"));
    }
    for (auto* chart : {appCpuChart_, appMemoryChart_})
        if (chart) chart->setThemeColors(QColor(palette.panel), QColor(palette.text), QColor(palette.muted), QColor(palette.border));
    if (settingsDialog_ != nullptr)
        settingsDialog_->setStyleSheet(styleSheet);
    if (customBackground_ != nullptr)
    {
        displayColor(customBackground_, customBackground_->property("selectedColor").toString());
        displayColor(customText_, customText_->property("selectedColor").toString());
        displayColor(customAccent_, customAccent_->property("selectedColor").toString());
        displayColor(customGraph_, customGraph_->property("selectedColor").toString());
    }
}

} // namespace orion::app
