#include "widgets/network_traffic_chart.h"

#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QPixmap>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void movePointer(orion::app::NetworkTrafficChart& chart, double ageMs)
{
    const auto area = chart.plotArea();
    const QPointF point(area.right() - ageMs / 60000.0 * area.width(), area.center().y());
    QMouseEvent event(QEvent::MouseMove, point, chart.mapToGlobal(point),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&chart, &event);
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    using Chart = orion::app::NetworkTrafficChart;
    Chart chart;
    chart.resize(390, 280);
    chart.show();
    QApplication::processEvents();
    auto require = [](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; std::exit(EXIT_FAILURE); }
    };
    require(chart.sampleCount() == 0 && chart.lineSegments(true).isEmpty(), "History fabricated startup zeroes");
    chart.appendSample(0, 0, 1024);
    chart.appendSample(1000, 1048576, 2048);
    chart.appendSample(2000, -1, std::numeric_limits<double>::infinity());
    chart.appendSample(3000, 2097152, 0);
    require(chart.sampleCount() == 4 && chart.samples().front().downloadBytesPerSecond == 0,
        "Idle zero was dropped");
    require(std::isnan(chart.samples().at(2).downloadBytesPerSecond)
        && std::isnan(chart.samples().at(2).uploadBytesPerSecond), "Missing rates fabricated");
    require(chart.lineSegments(true).size() == 2 && chart.lineSegments(true).front().size() == 2
        && chart.lineSegments(false).size() == 2, "Missing measurements connected by a line");
    chart.appendSample(3000, 123, 123);
    chart.appendSample(100, 123, 123);
    chart.appendSample(-1, 123, 123);
    require(chart.sampleCount() == 4, "Invalid timestamp accepted");
    movePointer(chart, 2000);
    require(chart.hoveredSampleIndex() == 1 && chart.hoverText().contains(QStringLiteral("1.000 МиБ/с"))
        && chart.hoverText().contains(QStringLiteral("2.000 КиБ/с")), "Hover rates or binary units incorrect");
    movePointer(chart, 1000);
    require(chart.hoverText().count(QStringLiteral("н/д")) == 2, "Missing hover values invented");
    movePointer(chart, 45000);
    require(chart.hoveredSampleIndex() == -1, "Hover before recorded history fabricated");
    chart.setMonitoringPaused(true);
    chart.appendSample(4000, 10, 10);
    require(chart.sampleCount() == 4 && chart.isMonitoringPaused(), "Paused delivery changed history");
    chart.setMonitoringPaused(false);
    chart.appendSample(4000, 1024, 2048);
    require(chart.samples().back().breakBefore && chart.lineSegments(true).size() == 3,
        "Short pause bridged with a fabricated line");
    chart.appendSample(9000, 1024, 2048);
    require(chart.samples().back().breakBefore && chart.lineSegments(true).size() == 4,
        "Long collection gap bridged");
    chart.appendSample(10000, std::numeric_limits<double>::quiet_NaN(), 4096);
    require(chart.lineSegments(true).back().size() == 1 && chart.lineSegments(false).back().size() == 2,
        "One unavailable direction erased the other");
    const auto right = chart.plotArea().right();
    const auto lastUpload = chart.lineSegments(false).back().back();
    require(std::abs(lastUpload.x() - right) < 0.001, "Latest sample is not at zero age");
    chart.appendSample(70001, 2048, 1024);
    require(chart.sampleCount() == 1, "History is bounded by sample count instead of real 60-second time");
    for (int index = 1; index <= 150; ++index) chart.appendSample(70001 + index * 10, 0, 0);
    require(chart.sampleCount() == Chart::historyPointLimit, "Fast history exceeded its memory bound");
    movePointer(chart, 0);
    require(chart.hoveredSampleIndex() == chart.sampleCount() - 1, "Right-edge hover missed last sample");
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&chart, &leave);
    require(chart.hoveredSampleIndex() == -1, "Crosshair remained after mouse leave");
    movePointer(chart, 0);
    chart.hide();
    require(chart.hoveredSampleIndex() == -1 && chart.sampleCount() == Chart::historyPointLimit,
        "Hidden chart retained cursor or erased data");

    // Optional visual fixture, explicitly synthetic and never used in the application.
    Chart preview;
    preview.resize(390, 310);
    for (int second = 0; second <= 60; ++second) {
        const double download = (second >= 24 && second <= 27) ? -1
            : 1048576.0 * (1.8 + 1.4 * std::sin(second * 0.21));
        preview.appendSample(second * 1000, download, 1048576.0 * (0.35 + 0.3 * std::cos(second * 0.17)));
    }
    preview.setThemeColors(QColor("#0A0A0C"), QColor("#141417"), QColor("#E8E8EC"),
        QColor("#8E8E96"), QColor("#00E5FF"), QColor("#2A2A2E"), true);
    preview.show();
    QApplication::processEvents();
    movePointer(preview, 15000);
    require(!preview.grab().isNull() && preview.sampleCount() == 61, "Theme or rendering lost history");
    const auto arguments = application.arguments();
    const int output = arguments.indexOf(QStringLiteral("--render-fixture"));
    if (output >= 0 && output + 1 < arguments.size())
        require(preview.grab().save(arguments.at(output + 1)), "Could not save synthetic chart fixture");
    preview.resize(260, 260);
    QApplication::processEvents();
    movePointer(preview, 60000);
    require(preview.hoveredSampleIndex() == 0 && !preview.grab().isNull(),
        "Left-edge hover or minimum-size rendering failed");
    std::cout << "Network chart time window, gaps, units, hover, pause, memory bound and rendering passed.\n";
    return EXIT_SUCCESS;
}
