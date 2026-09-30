#include "diagnostic_dock_layout.h"
#include "widgets/independent_dock_widget.h"
#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QMainWindow>
#include <QScreen>
#include <QTimer>
#include <iostream>

using namespace orion::app;
namespace {
int failures = 0;
void check(bool ok, const char* message)
{
    if (!ok) { ++failures; std::cerr << message << '\n'; }
}
void settle()
{
    QEventLoop loop; QTimer::singleShot(40, &loop, &QEventLoop::quit); loop.exec();
}
struct Fixture {
    QMainWindow host;
    DiagnosticDocks docks;
    Fixture() {
        host.resize(1000, 700);
        host.setDockNestingEnabled(true);
        host.setDockOptions(QMainWindow::AllowNestedDocks);
        const QStringList keys {"diagnostics", "stress_test", "report"};
        for (int i = 0; i < 3; ++i) {
            auto* dock = new IndependentDockWidget(keys[i], &host);
            dock->setObjectName("DiagnosticsHubDock_" + keys[i]);
            dock->setMinimumSize(100, 100);
            dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
            dock->setWidget(new QLabel("Retained content " + keys[i], dock));
            docks[i] = dock;
        }
        resetDiagnosticDockLayout(host, docks);
    }
    void customLayout() {
        for (auto* dock : docks) host.removeDockWidget(dock);
        host.addDockWidget(Qt::RightDockWidgetArea, docks[0]);
        host.addDockWidget(Qt::LeftDockWidgetArea, docks[1]);
        host.splitDockWidget(docks[1], docks[2], Qt::Vertical);
        host.resizeDocks({docks[0], docks[1]}, {350, 650}, Qt::Horizontal);
        host.resizeDocks({docks[1], docks[2]}, {400, 250}, Qt::Vertical);
        for (auto* dock : docks) dock->show();
        settle();
    }
    void checkDefaults() {
        check(host.dockWidgetArea(docks[0]) == Qt::LeftDockWidgetArea, "Diagnostics default area lost");
        check(host.dockWidgetArea(docks[1]) == Qt::RightDockWidgetArea, "Stress default area lost");
        check(host.dockWidgetArea(docks[2]) == Qt::RightDockWidgetArea, "Report default area lost");
        for (auto* dock : docks) check(!dock->isFloating() && !dock->isHidden(), "Reset lost a panel");
    }
};
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QList<QRect> screens;
    for (const auto* screen : QGuiApplication::screens()) screens.append(screen->availableGeometry());
    const QList<QRect> geometryFixture {{0, 0, 1920, 1040}, {-1280, -200, 1280, 1000}};
    check(hasReachableDockTitle({100, 100, 500, 300}, geometryFixture), "On-screen dock rejected");
    check(hasReachableDockTitle({-1200, -150, 500, 300}, geometryFixture), "Negative monitor origin rejected");
    check(!hasReachableDockTitle({5000, 100, 500, 300}, geometryFixture), "Missing monitor accepted");
    check(!hasReachableDockTitle({100, -100, 500, 400}, geometryFixture), "Visible body with missing title accepted");
    check(!hasReachableDockTitle({1910, 100, 500, 300}, geometryFixture), "Unusable title sliver accepted");
    check(!hasReachableDockTitle({100, 1030, 500, 300}, geometryFixture), "Title hidden behind taskbar accepted");
    check(!hasReachableDockTitle({}, geometryFixture), "Empty frame accepted");
    check(!hasReachableDockTitle({100, 100, 500, 300}, {}), "No screens accepted");

    Fixture source;
    source.host.show(); settle(); source.customLayout();
    const auto state = QString::fromLatin1(source.host.saveState(1).toBase64());
    const auto legacyState = QString::fromLatin1(source.host.saveState(0).toBase64());
    const auto reportSize = source.docks[2]->size();
    Fixture target;
    target.host.show(); settle();
    auto* retained = target.docks[2]->widget();
    for (const auto& saved : {state, legacyState}) {
        const auto result = restoreDiagnosticDockLayout(target.host, target.docks, saved, screens);
        settle();
        check(result.restored && !result.redockedPanels, "Valid native/Python layout rejected");
        check(target.host.dockWidgetArea(target.docks[0]) == Qt::RightDockWidgetArea
            && target.host.dockWidgetArea(target.docks[1]) == Qt::LeftDockWidgetArea
            && target.host.dockWidgetArea(target.docks[2]) == Qt::LeftDockWidgetArea, "Saved areas not restored");
        check(target.docks[2]->y() > target.docks[1]->y(), "Saved vertical split lost");
        check(std::abs(target.docks[2]->width() - reportSize.width()) <= 8
            && std::abs(target.docks[2]->height() - reportSize.height()) <= 8, "Saved panel sizes not restored");
        check(target.docks[2]->widget() == retained, "Restore replaced report content");
    }
    const QStringList invalidStates {
        {}, "not base64!", QString(QChar(0x2603)), state + "!", state + "\n",
        QString(64 * 1024 + 1, 'A'), QString::fromLatin1(QByteArray("not a Qt state").toBase64()),
        QString::fromLatin1(source.host.saveState(99).toBase64()),
        QString::fromLatin1(source.host.saveState(1).left(30).toBase64())
    };
    for (const auto& invalid : invalidStates) {
        target.docks[2]->setFloating(true); target.docks[0]->hide();
        const auto result = restoreDiagnosticDockLayout(target.host, target.docks, invalid, screens);
        settle(); check(!result.restored, "Invalid/empty layout accepted"); target.checkDefaults();
        check(target.docks[2]->widget() == retained, "Invalid-state fallback lost content");
    }
    source.docks[0]->hide();
    check(restoreDiagnosticDockLayout(target.host, target.docks,
        QString::fromLatin1(source.host.saveState(1).toBase64()), screens).restored, "Hidden state failed restore");
    check(!target.docks[0]->isHidden(), "Stale hidden flag lost a non-closable panel");
    source.docks[0]->show();
    source.docks[2]->setFloating(true);
    source.docks[2]->setGeometry(screens.first().topLeft().x() + 50, screens.first().topLeft().y() + 50, 400, 300);
    settle();
    const auto floatingState = QString::fromLatin1(source.host.saveState(1).toBase64());
    check(restoreDiagnosticDockLayout(target.host, target.docks, floatingState, screens).restored,
        "Floating snapshot not restored");
    settle(); check(target.docks[2]->isFloating() && !target.docks[2]->isHidden(), "Reachable floating panel redocked");
    const auto recovery = restoreDiagnosticDockLayout(target.host, target.docks, floatingState,
        {{-100000, -100000, 800, 600}});
    check(recovery.restored && recovery.redockedPanels == 1 && !target.docks[2]->isFloating(),
        "Unavailable-screen fallback failed");
    check(target.docks[2]->widget() == retained, "Recovery discarded report");
    for (int pass = 0; pass < 4; ++pass) {
        target.docks[2]->setFloating(true); resetDiagnosticDockLayout(target.host, target.docks);
        target.checkDefaults();
        check(target.host.findChildren<QDockWidget*>().size() == 3, "Reset duplicated panels");
    }
    std::cout << "Diagnostic layout failures: " << failures << '\n';
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
