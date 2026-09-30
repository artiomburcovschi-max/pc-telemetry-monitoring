#include "widgets/app_monitor_chart.h"
#include <QApplication>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <iostream>
#include <limits>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    using orion::app::AppMonitorChart;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { passed = false; std::cerr << message << '\n'; }
    };
    AppMonitorChart cpu(AppMonitorChart::Mode::Cpu), memory(AppMonitorChart::Mode::Memory);
    cpu.resize(700, 230); memory.resize(700, 230);
    check(cpu.axisMaximum() == 100 && cpu.lineSegments().isEmpty(), "Empty chart invalid.");
    QJsonObject sample{{"elapsed", 0}, {"cpu_percent", 145}, {"ram_mb", 200}, {"private_mb", 0}, {"sample_interval", 1}};
    for (int t = 0; t < 3; ++t) {
        sample.insert("elapsed", t);
        if (t == 1) sample.insert("ram_mb", QJsonValue::Null);
        else sample.insert("ram_mb", 200);
        cpu.appendSample(sample); memory.appendSample(sample);
    }
    check(cpu.axisMaximum() > 145 && cpu.lineSegments().size() == 1, "CPU >100 clipped or split.");
    check(memory.lineSegments(0).size() == 2 && memory.lineSegments(1).size() == 1,
        "Null memory bridged or real zero private memory lost.");
    cpu.appendSample(sample);
    sample.insert("elapsed", -1); cpu.appendSample(sample);
    sample.remove("elapsed"); cpu.appendSample(sample);
    check(cpu.sampleCount() == 3, "Invalid/duplicate time accepted.");
    cpu.setMonitoringPaused(true);
    sample.insert("elapsed", 3); cpu.appendSample(sample);
    check(cpu.sampleCount() == 3, "Paused delivery accepted.");
    cpu.setMonitoringPaused(false); cpu.appendSample(sample);
    check(cpu.lineSegments().size() == 2, "Pause bridged.");
    sample.insert("elapsed", 10); cpu.appendSample(sample);
    check(cpu.lineSegments().size() == 3, "Missing interval bridged.");
    sample.insert("elapsed", 11); sample.insert("cpu_percent", -4); cpu.appendSample(sample);
    check(cpu.lineSegments().size() == 3, "Invalid negative value graphed.");
    cpu.show(); QCoreApplication::processEvents();
    const auto area = cpu.plotArea();
    QMouseEvent move(QEvent::MouseMove, area.bottomRight() - QPointF(0, 20), area.bottomRight(),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&cpu, &move);
    check(cpu.hoverIndex() == 5 && cpu.hoverText().contains(QStringLiteral("н/д")), "Hover lost nearest/missing point.");
    cpu.hide(); check(cpu.hoverIndex() == -1 && cpu.sampleCount() == 6, "Hide erased history or retained hover.");
    cpu.clear(); check(cpu.sampleCount() == 0 && cpu.hoverText().isEmpty(), "Clear retained history.");
    for (int t = 0; t < 6050; ++t) {
        cpu.appendSample(QJsonObject{{"elapsed", t}, {"cpu_percent", t == 3000 ? 700 : 30}});
    }
    check(cpu.sampleCount() == 6000 && cpu.axisMaximum() >= 700, "Bounded history lost a spike.");
    const auto path = cpu.lineSegments().first();
    check(path.size() == 6000 && path[2950].y() < path[2949].y(), "Graph dropped peak vertex.");
    QElapsedTimer timer; timer.start();
    for (int i = 0; i < 20; ++i) check(!cpu.grab().isNull(), "Chart render failed.");
    check(timer.elapsed() < 5000, "Bounded graph paint unexpectedly slow.");
    std::cout << "App-monitor chart contracts " << (passed ? "passed" : "failed") << ".\n";
    return passed ? 0 : 1;
}
