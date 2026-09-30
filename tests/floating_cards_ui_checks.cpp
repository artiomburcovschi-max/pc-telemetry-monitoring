#include "main_window.h"
#include "telemetry_worker.h"
#include "widgets/independent_dock_widget.h"
#include "widgets/metric_card.h"

#include <QApplication>
#include <QCheckBox>
#include <QEventLoop>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <iostream>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

bool verifyFloatingCards(orion::app::MainWindow& window)
{
    bool passed = true;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { passed = false; std::cerr << message << '\n'; }
    };
    const auto settle = [] {
        QEventLoop loop; QTimer::singleShot(100, &loop, &QEventLoop::quit); loop.exec();
    };
    const auto docks = window.findChildren<QDockWidget*>();
    for (auto* dock : docks)
        check(dynamic_cast<orion::app::IndependentDockWidget*>(dock), "A production dock lacks independent ownership");
    auto* cpu = window.findChild<QDockWidget*>("OverviewDock_cpu");
    auto* ram = window.findChild<QDockWidget*>("OverviewDock_ram");
    auto* report = window.findChild<QDockWidget*>("DiagnosticsHubDock_report");
    auto* tabs = window.findChild<QTabWidget*>();
    auto* worker = window.findChild<orion::app::TelemetryWorker*>();
    auto* gamer = window.findChild<QCheckBox*>("GamerMode");
    auto* apply = window.findChild<QPushButton*>("SettingsApplyButton");
    auto* reset = window.findChild<QPushButton*>("ResetOverviewLayoutButton");
    if (!cpu || !ram || !report || !tabs || !worker || !gamer || !apply || !reset) return false;
    auto* cpuCard = cpu->findChild<orion::app::MetricCard*>();
    auto* value = cpu->findChild<QLabel*>("MetricCardValue");
    if (!cpuCard || !value) return false;
    const auto assertFloating = [&](QDockWidget* dock) {
        check(dock->isFloating() && dock->isVisible() && !dock->isMinimized(), "Production floating dock lost visibility");
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "windows") {
            check(dock->windowHandle() && !dock->windowHandle()->transientParent(), "Production dock regained transient ownership");
            const auto hwnd = reinterpret_cast<HWND>(dock->winId());
            check(!GetWindow(hwnd, GW_OWNER) && IsWindowVisible(hwnd) && !IsIconic(hwnd),
                "Native production card hidden/owned while main minimized");
        }
#endif
    };
    const auto publish = [&](double percent) {
        worker->sampleReady("fixture-only", "Fixture OS", "Fixture CPU", "Fixture GPU", percent,
            {percent}, {3000}, 3000, 40, 30, 45, 8, 1, 12.5, 55, 16, {}, {}, {}, {}, 1024, 2048);
    };
    tabs->setCurrentIndex(0); window.showNormal(); settle();
    for (auto* dock : {cpu, ram, report}) { dock->setFloating(true); dock->show(); }
    cpu->resize(330, 230); ram->resize(330, 230); report->resize(500, 400); settle();
    for (int pass = 0; pass < 3; ++pass) {
        window.showMinimized(); settle();
        check(window.isMinimized(), "Main window did not stay minimized");
        for (auto* dock : {cpu, ram, report}) assertFloating(dock);
        publish(37 + pass);
        check(value->text() == QString("%1%").arg(37.0 + pass, 0, 'f', 1), "Live card stopped updating while main minimized");
        window.showNormal(); settle();
    }
    window.showMinimized(); settle();
    QMetaObject::invokeMethod(&window, "togglePaused", Qt::DirectConnection);
    publish(83);
    check(value->text() == "39.0%", "Floating card bypassed global pause");
    QMetaObject::invokeMethod(&window, "togglePaused", Qt::DirectConnection);
    publish(42);
    check(value->text() == "42.0%", "Floating card failed to resume telemetry");
    const auto capture = qEnvironmentVariable("ORION_FLOATING_UI_CAPTURE");
    if (!capture.isEmpty()) {
        cpuCard->setPresentationMode(orion::app::MetricCard::PresentationMode::Standard);
        cpuCard->setDetailText(QStringLiteral("УЧЕБНЫЕ ДАННЫЕ · главное окно свёрнуто; это не проверка ПК"));
        cpu->layout()->activate();
        settle();
        assertFloating(cpu);
        check(cpu->grab().save(capture), "Floating card capture failed");
    }
    window.showNormal(); tabs->setCurrentIndex(1); settle();
    for (auto* dock : {cpu, ram, report}) assertFloating(dock);
    window.hide(); settle(); // Same hide operation used by close-to-tray.
    for (auto* dock : {cpu, ram, report}) assertFloating(dock);
    QMetaObject::invokeMethod(&window, "showFromTray", Qt::DirectConnection); settle();
    check(window.isVisible(), "Tray restore failed with independent docks");
    gamer->setChecked(true); apply->click(); settle();
    check(!window.isVisible(), "Gamer Mode did not hide main");
    for (auto* dock : {cpu, ram, report}) assertFloating(dock);
    publish(46);
    check(value->text() == "46.0%", "Floating card became stale in Gamer Mode");
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "GamerOverlay") widget->close();
    settle();
    tabs->setCurrentIndex(0);
    reset->click(); settle();
    check(!cpu->isFloating() && !ram->isFloating() && cpu->widget() == cpuCard,
        "Reset lost live card or left it floating");
    check(report->isFloating(), "Overview reset unexpectedly redocked diagnostic panel");
    cpu->setFloating(true); cpu->show(); settle();
    window.close(); settle(); // Existing non-tray close path must reclaim all top levels.
    check(!window.isVisible(), "Main close failed");
    for (auto* dock : docks) check(!dock->isFloating() && !dock->isVisible(), "Application exit left a floating orphan");
    return passed;
}
