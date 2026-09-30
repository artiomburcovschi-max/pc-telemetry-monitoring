#pragma once

#include "network_scanner_worker.h"
#include "internet_tools_worker.h"
#include "ping_worker.h"
#include "telemetry_types.h"
#include "startup_sequence.h"
#include "orion/core/session_peaks.h"
#include "orion/core/status_level.h"
#include "orion/storage/app_settings.h"
#include "orion/diagnostics/incident_data.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QVector>

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QSlider;
class QSpinBox;
class QSplitter;
class QSystemTrayIcon;
class QTabWidget;
class QTableWidget;
class QTextEdit;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace orion::app
{

class MetricCard;
class DetailsPanel;
class DetailCard;
class CpuCoreChartWidget;
class NetworkTrafficChart;
class AutostartWorker;
class AppMonitorWorker;
class AppMonitorChart;
class DiagnosticWorker;
class DeepScanWorker;
class FullScanWorker;
class DeepTelemetryDialog;
class IncidentWorker;
class GamerOverlay;
class HardwareInventoryWorker;
class NetworkScannerWorker;
class StressWorker;
class ProcessWorker;
class ProcessActionWorker;
class ServerMonitorWidget;
class TelemetryWorker;
class TerminalWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const orion::storage::AppSettings& settings, QString settingsPath,
        bool startOnlineLookup = true, QWidget* parent = nullptr);
    ~MainWindow() override;
    void beginStartupScan(bool includeOnlineLookup);
    void cancelStartupScan();

signals:
    void startupProgress(int percent, const QString& stage);
    void startupLog(const QString& line);
    void startupFinished(bool withWarnings);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void applyTelemetry(const QString& backend, const QString& operatingSystem, const QString& cpuName,
        const QString& gpuName, double cpuPercent, const QVector<double>& cpuCores,
        const QVector<double>& cpuCoreFrequenciesMhz, double cpuFrequencyMhz, double cpuTemperatureC,
        double gpuPercent, double gpuTemperatureC, double gpuMemoryTotalGiB, double gpuMemoryUsedGiB,
        double gpuMemoryPercent, double ramPercent, double ramTotalGiB, const RuntimeTelemetry& runtime,
        const QVector<DiskTelemetry>& disks, const QVector<TemperatureTelemetry>& temperatures,
        const QVector<FanTelemetry>& fans, double downloadBytesPerSecond, double uploadBytesPerSecond);
    void applyPingTelemetry(const PingTelemetry& sample);
    void togglePaused();
    void applySettings();
    void openSettings();
    void openDeepTelemetry();
    void markProblemNow();
    void showFromTray();
    void requestQuit();
    void applyProcesses(
        const QVector<ProcessTelemetry>& processes, int logicalProcessorCount, const QString& source);
    void applyAutostartEntries(const QVector<AutostartTelemetry>& entries, const QString& source);
    void applyDiagnosticReport(const QJsonObject& report);
    void applyStressReport(const QJsonObject& report);
    void startAppMonitoring();
    void applyAppMonitorSample(const QJsonObject& sample);
    void applyAppMonitorReport(const QJsonObject& report);
    void finalizeIncident(const QJsonObject& partial);

private:
    void requestStartupPhase(StartupSequence::Phase phase);
    StartupSequence* startupSequence_ { nullptr };
    [[nodiscard]] QWidget* createOverviewPage();
    [[nodiscard]] QWidget* createDetailsPage();
    [[nodiscard]] QWidget* createNetworkPage();
    [[nodiscard]] QWidget* createTaskManagerPage();
    [[nodiscard]] QWidget* createAutostartPage();
    [[nodiscard]] QWidget* createDiagnosticsPage();
    [[nodiscard]] QWidget* createHardwarePage();
    [[nodiscard]] QWidget* createAppMonitorPage();
    [[nodiscard]] QWidget* createSettingsPage();
    [[nodiscard]] QWidget* createPlaceholderPage(const QString& title, const QString& description);
    void setPaused(bool paused);
    void updateTrayVisibility();
    void saveSettings();
    void updateNetworkPresentation();
    void refreshNetworkInterfaces();
    void updateSelectedNetworkInterface();
    void toggleNetworkScan();
    void startNetworkScan();
    void stopNetworkScan(bool waitForFinish);
    void upsertNetworkDevice(const NetworkDevice& device);
    void finishNetworkScan(const NetworkScanResult& result);
    void failNetworkScan(const QString& error);
    void restoreNetworkScanControls();
    void startPublicIpLookup();
    void startInternetSpeedTest();
    void applyInternetResult(InternetOperation operation, const QJsonObject& result);
    void restoreInternetControls();
    void renderProcessTable();
    void updateProcessActions();
    void updateAppMonitorControls();
    void terminateSelectedProcess();
    void renderAutostartTable();
    [[nodiscard]] QJsonObject buildHardwareSeed() const;
    void startHardwareScan();
    void applyHardwareReport(const QJsonObject& report);
    void renderHardwareReport();
    void addHardwareSection(const QString& title, const QList<QPair<QString, QString>>& rows);
    [[nodiscard]] QJsonObject buildDiagnosticSnapshot() const;
    void startDiagnosticScan();
    void startDeepScan();
    void startFullScan();
    void renderPassiveDiagnosticCards(const QJsonObject& report);
    void renderSmartCards(const QJsonObject& report);
    void renderLogErrorsCard(const QJsonObject& report);
    void renderPublicIpCard(const QJsonObject& result);
    void renderIncidentCard(const QJsonObject& incident);
    void updateDiagnosticTemperatureCards(double cpuTemperatureC, double gpuTemperatureC);
    void renderDiagnosticDetails();
    void saveDiagnosticReport(bool jsonFormat);
    void saveAppMonitorReport(bool jsonFormat);
    void saveAppMonitorReportSnapshot(QJsonObject report, bool jsonFormat);
    void openAppMonitorReport();
    void startStressTest();
    void renderStressResult(const QJsonObject& result);
    void maybeNotify(double cpuPercent, double ramPercent);
    void applyStyle();
    void applyWindowConstraints();
    void applyOverviewSettings();
    void applyThemeBehavior();
    void resetOverviewLayout();
    void resetDiagnosticLayout();
    void returnFloatingOverviewCards();
    void returnFloatingDiagnosticPanels();
    void syncOptionalTabs();
    void syncGamerMode(bool enabled);
    void restoreFromGamerOverlay();
    void handleCurrentTabChanged();

    orion::storage::AppSettings settings_;
    QString settingsPath_;
    TelemetryWorker* telemetryWorker_ { nullptr };
    PingWorker* pingWorker_ { nullptr };
    NetworkScannerWorker* networkScannerWorker_ { nullptr };
    InternetToolsWorker* internetToolsWorker_ { nullptr };
    ProcessWorker* processWorker_ { nullptr };
    ProcessActionWorker* processActionWorker_ { nullptr };
    AutostartWorker* autostartWorker_ { nullptr };
    DiagnosticWorker* diagnosticWorker_ { nullptr };
    DeepScanWorker* deepScanWorker_ { nullptr };
    FullScanWorker* fullScanWorker_ { nullptr };
    StressWorker* stressWorker_ { nullptr };
    AppMonitorWorker* appMonitorWorker_ { nullptr };
    IncidentWorker* incidentWorker_ { nullptr };
    HardwareInventoryWorker* hardwareWorker_ { nullptr };
    DeepTelemetryDialog* deepTelemetryDialog_ { nullptr };

    QTabWidget* tabWidget_ { nullptr };
    QWidget* overviewPage_ { nullptr };
    QWidget* detailsPage_ { nullptr };
    QWidget* networkPage_ { nullptr };
    QWidget* taskManagerPage_ { nullptr };
    QWidget* hardwarePage_ { nullptr };
    QWidget* autostartPage_ { nullptr };
    QWidget* diagnosticsPage_ { nullptr };
    QWidget* appMonitorPage_ { nullptr };
    ServerMonitorWidget* serverPanel_ { nullptr };
    TerminalWidget* terminalPanel_ { nullptr };
    GamerOverlay* gamerOverlay_ { nullptr };
    QLabel* collectionStatus_ { nullptr };
    QLabel* alarmBanner_ { nullptr };
    QLabel* pauseBanner_ { nullptr };
    QPushButton* pauseButton_ { nullptr };
    QPushButton* errorsBadge_ { nullptr };
    QPushButton* problemButton_ { nullptr };

    QMainWindow* overviewDockHost_ { nullptr };
    QVector<QDockWidget*> overviewDocks_;
    MetricCard* cpuCard_ { nullptr };
    MetricCard* gpuCard_ { nullptr };
    MetricCard* ramCard_ { nullptr };
    MetricCard* diskCard_ { nullptr };
    MetricCard* networkCard_ { nullptr };

    DetailsPanel* detailsPanel_ { nullptr };
    CpuCoreChartWidget* cpuCoreChart_ { nullptr };
    QTableWidget* processTable_ { nullptr };
    QLabel* processCountValue_ { nullptr };
    QLabel* processSourceValue_ { nullptr };
    QLineEdit* processSearch_ { nullptr };
    QPushButton* processShowAllButton_ { nullptr };
    QPushButton* processRefreshButton_ { nullptr };
    QPushButton* processTerminateButton_ { nullptr };
    QLabel* processActionStatus_ { nullptr };
    bool processTerminationPending_ { false };
    QVector<ProcessTelemetry> processRows_;
    bool processShowAll_ { false };
    QTableWidget* autostartTable_ { nullptr };
    QLabel* autostartCountValue_ { nullptr };
    QLabel* autostartSourceValue_ { nullptr };
    QLineEdit* autostartSearch_ { nullptr };
    QComboBox* autostartCategory_ { nullptr };
    QPushButton* autostartRefreshButton_ { nullptr };
    QVector<AutostartTelemetry> autostartRows_;
    bool autostartLoaded_ { false };
    QMainWindow* diagnosticDockHost_ { nullptr };
    QVector<QDockWidget*> diagnosticDocks_;
    QPushButton* diagnosticScanButton_ { nullptr };
    QPushButton* deepScanButton_ { nullptr };
    QPushButton* fullScanButton_ { nullptr };
    QPushButton* diagnosticIncidentButton_ { nullptr };
    QLabel* diagnosticStatusValue_ { nullptr };
    QLabel* diagnosticCoverageValue_ { nullptr };
    DetailCard* diagnosticIncidentCard_ { nullptr };
    DetailCard* diagnosticCpuTemperatureCard_ { nullptr };
    DetailCard* diagnosticGpuTemperatureCard_ { nullptr };
    QLabel* diagnosticSmartStatus_ { nullptr };
    QWidget* diagnosticSmartCardsHost_ { nullptr };
    QVBoxLayout* diagnosticSmartCardsLayout_ { nullptr };
    QVector<DetailCard*> diagnosticSmartCards_;
    DetailCard* diagnosticLogErrorsCard_ { nullptr };
    QTextEdit* diagnosticLogErrorsText_ { nullptr };
    DetailCard* diagnosticPublicIpCard_ { nullptr };
    QTableWidget* diagnosticTable_ { nullptr };
    QTextEdit* diagnosticDetails_ { nullptr };
    QTextEdit* diagnosticReportPreview_ { nullptr };
    QPushButton* diagnosticSaveJsonButton_ { nullptr };
    QPushButton* diagnosticSaveTextButton_ { nullptr };
    QPushButton* diagnosticCopyButton_ { nullptr };
    QJsonObject latestDiagnosticReport_;
    QJsonObject latestStressResult_;
    QJsonObject latestSpeedTest_;
    QJsonObject latestPublicIp_;
    bool diagnosticLoaded_ { false };
    orion::core::StatusLevel diagnosticCpuTemperatureStatus_ { orion::core::StatusLevel::Unknown };
    orion::core::StatusLevel diagnosticGpuTemperatureStatus_ { orion::core::StatusLevel::Unknown };
    qint64 diagnosticCpuTemperatureSinceMs_ { -1 };
    qint64 diagnosticGpuTemperatureSinceMs_ { -1 };
    QCheckBox* stressCpuCheck_ { nullptr };
    QCheckBox* stressGpuCheck_ { nullptr };
    QCheckBox* stressDiskCheck_ { nullptr };
    QCheckBox* stressGpuConfirm_ { nullptr };
    QCheckBox* stressDiskConfirm_ { nullptr };
    QComboBox* stressDuration_ { nullptr };
    QComboBox* stressDiskSize_ { nullptr };
    QPushButton* stressStartButton_ { nullptr };
    QPushButton* stressStopButton_ { nullptr };
    QProgressBar* stressProgress_ { nullptr };
    QLabel* stressStatus_ { nullptr };
    QTextEdit* stressResult_ { nullptr };
    QLabel* incidentStatusValue_ { nullptr };

    QLineEdit* appExecutablePath_ { nullptr };
    QPushButton* appBrowseButton_ { nullptr };
    QLabel* appSamplingHint_ { nullptr };
    DetailCard* appLiveDetails_ { nullptr };
    AppMonitorChart* appCpuChart_ { nullptr };
    AppMonitorChart* appMemoryChart_ { nullptr };
    DetailCard* appVerdictCard_ { nullptr };
    QPushButton* appOpenReportButton_ { nullptr };
    QComboBox* appDuration_ { nullptr };
    QCheckBox* appCloseOnTimeout_ { nullptr };
    QPushButton* appStartButton_ { nullptr };
    QPushButton* appStopButton_ { nullptr };
    QProgressBar* appProgress_ { nullptr };
    QLabel* appStatusValue_ { nullptr };
    QLabel* appCpuValue_ { nullptr };
    QLabel* appRamValue_ { nullptr };
    QLabel* appIoValue_ { nullptr };
    QLabel* appWindowValue_ { nullptr };
    QTableWidget* appSampleTable_ { nullptr };
    QTextEdit* appReportPreview_ { nullptr };
    QPushButton* appSaveJsonButton_ { nullptr };
    QPushButton* appSaveTextButton_ { nullptr };
    QPushButton* appCopyButton_ { nullptr };
    QJsonObject latestAppMonitorReport_;
    QJsonObject latestAppLiveSample_;
    QJsonObject latestIncident_;
    QVector<QJsonObject> runtimeSamples_;

    QString latestBackend_;
    QString latestOperatingSystem_;
    QString latestCpuName_;
    QString latestGpuName_;
    double latestCpuPercent_ { -1.0 };
    QVector<double> latestCpuCores_;
    QVector<double> latestCpuCoreFrequenciesMhz_;
    double latestCpuTemperatureC_ { -1.0 };
    double latestCpuFrequencyMhz_ { -1.0 };
    double latestGpuPercent_ { -1.0 };
    double latestGpuTemperatureC_ { -1.0 };
    double latestGpuMemoryTotalGiB_ { -1.0 };
    double latestGpuMemoryUsedGiB_ { -1.0 };
    double latestGpuMemoryPercent_ { -1.0 };
    double latestRamPercent_ { -1.0 };
    double latestRamTotalGiB_ { -1.0 };
    RuntimeTelemetry latestRuntime_;
    QVector<DiskTelemetry> latestDisks_;
    QVector<TemperatureTelemetry> latestTemperatures_;
    QVector<FanTelemetry> latestFans_;
    double latestDownloadBytesPerSecond_ { -1.0 };
    double latestUploadBytesPerSecond_ { -1.0 };
    PingTelemetry latestPing_;

    NetworkTrafficChart* networkTrafficChart_ { nullptr };
    DetailCard* networkStateCard_ { nullptr };
    QSplitter* networkSplitter_ { nullptr };
    QLabel* networkDownloadValue_ { nullptr };
    QLabel* networkUploadValue_ { nullptr };
    QLabel* networkPingValue_ { nullptr };
    QLabel* networkReceivedValue_ { nullptr };
    QLabel* networkSentValue_ { nullptr };
    QLabel* networkErrorsValue_ { nullptr };
    QLabel* networkDropsValue_ { nullptr };
    QLabel* networkLocalIpValue_ { nullptr };
    QLabel* networkMacValue_ { nullptr };
    QLabel* networkScopeValue_ { nullptr };
    QLabel* networkPublicIpValue_ { nullptr };
    QLabel* networkSpeedTestValue_ { nullptr };
    QComboBox* networkInterfaceCombo_ { nullptr };
    QToolButton* networkRefreshButton_ { nullptr };
    QPushButton* networkScanButton_ { nullptr };
    QProgressBar* networkScanProgress_ { nullptr };
    QLabel* networkScanStatus_ { nullptr };
    QTableWidget* networkDeviceTable_ { nullptr };
    QVector<NetworkInterfaceInfo> networkInterfaces_;
    QPushButton* publicIpRefreshButton_ { nullptr };
    QPushButton* speedTestButton_ { nullptr };
    QProgressBar* internetToolsProgress_ { nullptr };
    QLabel* internetToolsStatus_ { nullptr };
    InternetOperation activeInternetOperation_ { InternetOperation::PublicIpLookup };
    QPushButton* hardwareCopyButton_ { nullptr };
    QPushButton* hardwareRefreshButton_ { nullptr };
    QLabel* hardwareStatus_ { nullptr };
    QProgressBar* hardwareProgress_ { nullptr };
    QWidget* hardwareSectionsHost_ { nullptr };
    QVBoxLayout* hardwareSectionsLayout_ { nullptr };
    QJsonObject latestHardwareReport_;
    QString hardwareCopyText_;
    bool hardwareLoaded_ { false };
    bool telemetryReceived_ { false };
    bool hardwareScanPending_ { false };
    bool hardwareScanCompleted_ { false };

    QDialog* settingsDialog_ { nullptr };
    QComboBox* themeCombo_ { nullptr };
    QCheckBox* trayEnabled_ { nullptr };
    QCheckBox* notificationsEnabled_ { nullptr };
    QCheckBox* alwaysOnTop_ { nullptr };
    QCheckBox* freeFormResize_ { nullptr };
    QSlider* opacitySlider_ { nullptr };
    QLabel* opacityValue_ { nullptr };
    QSpinBox* minimumWidth_ { nullptr };
    QSpinBox* minimumHeight_ { nullptr };
    QSpinBox* maximumWidth_ { nullptr };
    QSpinBox* maximumHeight_ { nullptr };
    QCheckBox* cardCpuVisible_ { nullptr };
    QCheckBox* cardGpuVisible_ { nullptr };
    QCheckBox* cardRamVisible_ { nullptr };
    QCheckBox* cardDiskVisible_ { nullptr };
    QCheckBox* cardNetVisible_ { nullptr };
    QCheckBox* cardDragDropEnabled_ { nullptr };
    QCheckBox* terminalEnabled_ { nullptr };
    QCheckBox* serverTabEnabled_ { nullptr };
    QCheckBox* gamerMode_ { nullptr };
    QCheckBox* alertFreezeDisabled_ { nullptr };
    QLineEdit* pingTarget_ { nullptr };
    QPushButton* customBackground_ { nullptr };
    QPushButton* customText_ { nullptr };
    QPushButton* customAccent_ { nullptr };
    QPushButton* customGraph_ { nullptr };
    QLabel* customThemeStatus_ { nullptr };

    QSystemTrayIcon* trayIcon_ { nullptr };
    QAction* trayPauseAction_ { nullptr };
    QTimer* alarmClearTimer_ { nullptr };
    bool paused_ { false };
    bool quitting_ { false };
    bool gamerModeActive_ { false };
    bool cpuAlarmActive_ { false };
    bool ramAlarmActive_ { false };
    bool cpuCriticalState_ { false };
    bool gpuCriticalState_ { false };
    bool ramCriticalState_ { false };
    QElapsedTimer sessionTimer_;
    orion::diagnostics::IncidentSampleBuilder incidentSampleBuilder_;
    qint64 incidentNotBeforeMs_ {0};
    orion::core::SessionPeaksTracker sessionPeaks_;
};

} // namespace orion::app
