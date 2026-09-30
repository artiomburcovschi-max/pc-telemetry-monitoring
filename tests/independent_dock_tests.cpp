#include "widgets/independent_dock_widget.h"
#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QTabWidget>
#include <QTimer>
#include <QWindow>
#include <iostream>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << message << '\n'; }
}
void settle()
{
    QEventLoop loop;
    QTimer::singleShot(80, &loop, &QEventLoop::quit);
    loop.exec();
}
void checkIndependent(QDockWidget* dock)
{
    check(dock->isFloating() && dock->isVisible() && !dock->isMinimized(), "Floating card is hidden/minimized/redocked");
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == "windows") {
        check(dock->windowHandle() && !dock->windowHandle()->transientParent(), "Qt transient owner retained");
        const auto handle = reinterpret_cast<HWND>(dock->winId());
        check(GetWindow(handle, GW_OWNER) == nullptr, "Native owner retained: minimizing main hides this card");
        check(IsWindowVisible(handle) && !IsIconic(handle), "Windows actually hid/minimized the floating card");
    }
#endif
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    auto* main = new QMainWindow;
    main->resize(650, 420);
    auto* tabs = new QTabWidget(main);
    auto* host = new QMainWindow(tabs);
    host->setWindowFlags(Qt::Widget);
    main->setCentralWidget(tabs);
    tabs->addTab(host, "Fixture overview");
    tabs->addTab(new QWidget, "Fixture other tab");
    // Opt-in plain-Qt control for reproducing the pre-fix ownership behavior.
    const bool baseline = qEnvironmentVariableIsSet("ORION_DOCK_BASELINE");
    QDockWidget* cpu = baseline ? new QDockWidget("CPU fixture", host)
        : new orion::app::IndependentDockWidget("CPU fixture", host);
    QDockWidget* ram = baseline ? new QDockWidget("RAM fixture", host)
        : new orion::app::IndependentDockWidget("RAM fixture", host);
    cpu->setObjectName("cpu"); ram->setObjectName("ram");
    auto* value = new QLabel("Fixture value: 0", cpu);
    cpu->setWidget(value);
    ram->setWidget(new QLabel("Fixture RAM", ram));
    for (auto* dock : {cpu, ram}) {
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        host->addDockWidget(Qt::LeftDockWidgetArea, dock);
    }
    main->show(); settle();
    cpu->setFloating(true); ram->setFloating(true); settle();
    cpu->move(80, 90); ram->move(480, 90);
    const auto geometry = cpu->geometry();
    checkIndependent(cpu); checkIndependent(ram);
    for (int pass = 0; pass < 3; ++pass) {
        main->showMinimized(); settle();
        check(main->isMinimized(), "Main did not minimize in fixture");
        checkIndependent(cpu); checkIndependent(ram);
        value->setText(QString("Fixture value: %1").arg(pass + 1));
        check(cpu->findChild<QLabel*>()->text().endsWith(QString::number(pass + 1)), "Detached content cannot update");
        check(cpu->geometry() == geometry, "Main minimize moved/resized the floating card");
        main->showNormal(); settle();
        checkIndependent(cpu); checkIndependent(ram);
    }
    tabs->setCurrentIndex(1); settle();
    checkIndependent(cpu); checkIndependent(ram);
    main->hide(); settle();
    checkIndependent(cpu); checkIndependent(ram);
    main->show(); tabs->setCurrentIndex(0); settle();
    ram->hide(); main->showMinimized(); settle();
    check(ram->isHidden(), "Explicitly hidden card was resurrected");
    main->showNormal(); ram->show(); settle();
    checkIndependent(ram);
    const auto saved = host->saveState(1);
    cpu->setWindowFlag(Qt::FramelessWindowHint, true); cpu->show(); settle(); checkIndependent(cpu);
    cpu->setWindowFlag(Qt::FramelessWindowHint, false); cpu->show(); settle(); checkIndependent(cpu);
    for (int pass = 0; pass < 4; ++pass) {
        cpu->setFloating(false); settle();
        check(!cpu->isFloating() && cpu->parentWidget() == host && cpu->widget() == value, "Redock lost host/content");
        cpu->setFloating(true); settle(); checkIndependent(cpu);
    }
    cpu->setFloating(false); ram->setFloating(false);
    check(host->restoreState(saved, 1), "Saved floating layout could not restore");
    settle(); checkIndependent(cpu); checkIndependent(ram);
    // Changing the main native flags can recreate native handles.
    main->setWindowFlag(Qt::WindowStaysOnTopHint, true); main->show(); settle();
    main->showMinimized(); settle(); checkIndependent(cpu); checkIndependent(ram);
    main->showNormal(); settle();
    QPointer<QDockWidget> cpuLifetime = cpu, ramLifetime = ram;
    QPointer<QLabel> contentLifetime = value;
#ifdef Q_OS_WIN
    const auto cpuHandle = reinterpret_cast<HWND>(cpu->winId());
#endif
    delete main;
    settle();
    check(cpuLifetime.isNull() && ramLifetime.isNull() && contentLifetime.isNull(), "Detached windows/content leaked after owner destruction");
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == "windows") check(!IsWindow(cpuHandle), "Orphan HWND survived shutdown");
#endif
    if (failures) return 1;
    std::cout << "Independent floating dock lifecycle passed.\n";
    return 0;
}
