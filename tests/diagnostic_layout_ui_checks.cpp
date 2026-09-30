#include "main_window.h"
#include "telemetry_worker.h"
#include <QApplication>
#include <QDockWidget>
#include <QDialog>
#include <QEventLoop>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <iostream>

bool verifyDiagnosticLayout(orion::app::MainWindow& window, const QString& settingsPath)
{
    bool passed = true;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { passed = false; std::cerr << message << '\n'; }
    };
    const auto settle = [] {
        QEventLoop loop; QTimer::singleShot(80, &loop, &QEventLoop::quit); loop.exec();
    };
    auto* host = window.findChild<QMainWindow*>("DiagnosticsHubHost");
    auto* diagnostics = window.findChild<QDockWidget*>("DiagnosticsHubDock_diagnostics");
    auto* stress = window.findChild<QDockWidget*>("DiagnosticsHubDock_stress_test");
    auto* report = window.findChild<QDockWidget*>("DiagnosticsHubDock_report");
    auto* preview = window.findChild<QTextEdit*>("DiagnosticReportPreview");
    auto* cpu = window.findChild<QDockWidget*>("OverviewDock_cpu");
    auto* reset = window.findChild<QPushButton*>("ResetDiagnosticLayoutButton");
    auto* apply = window.findChild<QPushButton*>("SettingsApplyButton");
    if (!host || !diagnostics || !stress || !report || !preview || !cpu || !reset || !apply) return false;
    const auto reportText = preview->toPlainText();
    for (auto* dock : {diagnostics, stress, report}) host->removeDockWidget(dock);
    host->addDockWidget(Qt::RightDockWidgetArea, diagnostics);
    host->addDockWidget(Qt::LeftDockWidgetArea, stress);
    host->splitDockWidget(stress, report, Qt::Vertical);
    for (auto* dock : {diagnostics, stress, report}) dock->show();
    settle(); apply->click();
    orion::storage::AppSettings saved;
    QString error;
    if (!orion::storage::AppSettings::load(settingsPath, saved, &error)) return false;
    check(!saved.diagnosticHubDockState.isEmpty(), "Production settings omitted diagnostic state");
    // Exercise an actual second constructor with settings read from disk, no online lookup.
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    saved.trayIconEnabled = false; saved.pingTarget = "127.0.0.1";
    saved.cardDockState.clear(); saved.themeKey = "eclipse";
    saved.minimumWidth = 730; saved.maximumWidth = 830;
    saved.minimumHeight = 800; saved.maximumHeight = 900;
    const auto restartedPath = temporary.filePath("restarted.json");
    {
        orion::app::MainWindow restarted(saved, restartedPath, false);
        QMetaObject::invokeMethod(&restarted, "togglePaused", Qt::DirectConnection);
        auto* worker = restarted.findChild<orion::app::TelemetryWorker*>();
        if (!worker) return false;
        worker->stop(); check(worker->wait(2500), "Restart fixture worker did not stop");
        auto* restoredHost = restarted.findChild<QMainWindow*>("DiagnosticsHubHost");
        auto* restoredDiagnostic = restarted.findChild<QDockWidget*>("DiagnosticsHubDock_diagnostics");
        auto* restoredStress = restarted.findChild<QDockWidget*>("DiagnosticsHubDock_stress_test");
        auto* restoredReport = restarted.findChild<QDockWidget*>("DiagnosticsHubDock_report");
        if (!restoredHost || !restoredDiagnostic || !restoredStress || !restoredReport) return false;
        check(restoredHost->dockWidgetArea(restoredDiagnostic) == Qt::RightDockWidgetArea
            && restoredHost->dockWidgetArea(restoredStress) == Qt::LeftDockWidgetArea
            && restoredHost->dockWidgetArea(restoredReport) == Qt::LeftDockWidgetArea,
            "Production restart ignored persisted diagnostic layout");
        restarted.show(); restarted.findChild<QTabWidget*>()->setCurrentIndex(6); settle();
        const auto capture = qEnvironmentVariable("ORION_DIAGNOSTIC_LAYOUT_CAPTURE");
        if (!capture.isEmpty()) {
            restarted.findChild<QTextEdit*>("DiagnosticReportPreview")->setPlainText(QStringLiteral(
                "УЧЕБНЫЙ ПРИМЕР РАСКЛАДКИ\n\nПанель отчёта слева, диагностика справа.\n"
                "Положение восстановлено из настроек.\n\nЭто не результат проверки компьютера."));
            settle(); check(restarted.grab().save(capture), "Restored layout capture failed");
        }
        restoredReport->setFloating(true); settle();
        QMetaObject::invokeMethod(&restarted, "openSettings", Qt::DirectConnection);
        auto* restartedApply = restarted.findChild<QPushButton*>("SettingsApplyButton");
        auto* settingsDialog = restarted.findChild<QDialog*>("SettingsDialog");
        auto* settingsTabs = restarted.findChild<QTabWidget*>("SettingsTabs");
        if (!restartedApply || !settingsDialog || !settingsTabs) return false;
        settingsTabs->setCurrentIndex(2); settle();
        const auto settingsCapture = qEnvironmentVariable("ORION_DIAGNOSTIC_SETTINGS_CAPTURE");
        if (!settingsCapture.isEmpty())
            check(settingsDialog->grab().save(settingsCapture), "Layout settings capture failed");
        restartedApply->click();
        check(restoredReport->isFloating(), "Saving settings redocked a live panel");
        orion::storage::AppSettings liveSaved;
        check(orion::storage::AppSettings::load(restartedPath, liveSaved, &error)
            && liveSaved.diagnosticHubDockState == QString::fromLatin1(restoredHost->saveState(1).toBase64()),
            "Live floating snapshot not saved");
        settingsDialog->hide(); restarted.close();
        check(!restoredReport->isFloating(), "Full exit did not safely return a floating panel");
        orion::storage::AppSettings closedSaved;
        check(orion::storage::AppSettings::load(restartedPath, closedSaved, &error)
            && closedSaved.diagnosticHubDockState != liveSaved.diagnosticHubDockState,
            "Full exit saved stale floating state");
    }
    cpu->setFloating(true); report->setFloating(true); settle();
    reset->click(); settle();
    check(cpu->isFloating(), "Diagnostic reset changed Overview layout");
    check(!report->isFloating() && host->dockWidgetArea(diagnostics) == Qt::LeftDockWidgetArea
        && host->dockWidgetArea(stress) == Qt::RightDockWidgetArea
        && host->dockWidgetArea(report) == Qt::RightDockWidgetArea, "Diagnostic reset button failed");
    check(preview->toPlainText() == reportText, "Diagnostic reset erased report content");
    check(host->findChildren<QDockWidget*>().size() == 3, "Diagnostic reset duplicated panels");
    check(orion::storage::AppSettings::load(settingsPath, saved, &error)
        && saved.diagnosticHubDockState == QString::fromLatin1(host->saveState(1).toBase64()),
        "Diagnostic reset was not persisted");
    cpu->setFloating(false);
    return passed;
}
