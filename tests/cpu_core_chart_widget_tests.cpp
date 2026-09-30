#include "widgets/cpu_core_chart_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    orion::app::CpuCoreChartWidget chart;
    chart.resize(760, 310);
    chart.show();
    QApplication::processEvents();

    auto* mode = chart.findChild<QComboBox*>(QStringLiteral("CpuCoreChartMode"));
    auto* group = chart.findChild<QComboBox*>(QStringLiteral("CpuCoreChartGroup"));
    auto* summary = chart.findChild<QLabel*>(QStringLiteral("CpuCoreChartSummary"));
    auto* plot = chart.findChild<QWidget*>(QStringLiteral("CpuCorePlot"));
    if (chart.objectName() != QStringLiteral("CpuCoreChart")
        || mode == nullptr || group == nullptr || summary == nullptr || plot == nullptr
        || chart.currentMode() != QStringLiteral("profile") || !group->isHidden()
        || mode->count() != 3
        || mode->itemText(2) != QStringLiteral("История 60 секунд")) {
        std::cerr << "CPU chart initial presentation contract is incomplete.\n";
        return EXIT_FAILURE;
    }

    chart.updateCores(QVector<double> {0.0, 50.0, 110.0, -5.0});
    const auto current = chart.currentLoads();
    if (chart.sampleCount() != 1 || chart.coreCount() != 4
        || current.size() != 4 || current[0] != 0.0 || current[1] != 50.0
        || current[2] != 100.0 || current[3] != 0.0
        || !summary->text().contains(QStringLiteral("CPU 3: 100%"))) {
        std::cerr << "CPU chart did not preserve idle cores or clamp values.\n";
        return EXIT_FAILURE;
    }

    chart.setMode(QStringLiteral("history"));
    if (chart.currentMode() != QStringLiteral("history") || group->isHidden()
        || chart.selectedHistoryCores() != QVector<int> {1, 2, 3, 4}) {
        std::cerr << "CPU chart history controls are incomplete.\n";
        return EXIT_FAILURE;
    }

    chart.updateCores(QVector<double> {
        std::numeric_limits<double>::quiet_NaN(), 75.0});
    const auto coreOneHistory = chart.historyForCore(1);
    const auto coreThreeHistory = chart.historyForCore(3);
    if (coreOneHistory.size() != 2 || !std::isnan(coreOneHistory.back())
        || coreThreeHistory.size() != 2 || !std::isnan(coreThreeHistory.back())
        || !summary->text().contains(QStringLiteral("CPU 2: 75%"))) {
        std::cerr << "Missing core telemetry was fabricated instead of stored as a gap.\n";
        return EXIT_FAILURE;
    }

    QVector<double> tenCores;
    for (int index = 0; index < 10; ++index) tenCores.append(index * 7.0);
    chart.updateCores(tenCores);
    if (chart.historyPageCount() != 2 || group->count() != 2
        || group->itemText(0) != QStringLiteral("CPU 1–8")
        || group->itemText(1) != QStringLiteral("CPU 9–10")) {
        std::cerr << "CPU history was not paged in groups of eight.\n";
        return EXIT_FAILURE;
    }
    chart.setHistoryPage(1);
    if (chart.selectedHistoryCores() != QVector<int> {9, 10}) {
        std::cerr << "CPU history page selection is incorrect.\n";
        return EXIT_FAILURE;
    }

    for (int sample = 0; sample < 70; ++sample) {
        chart.updateCores(tenCores);
    }
    if (chart.sampleCount() != orion::app::CpuCoreChartWidget::historyPointLimit
        || chart.historyForCore(1).size()
            != orion::app::CpuCoreChartWidget::historyPointLimit) {
        std::cerr << "CPU history is not bounded to 60 samples.\n";
        return EXIT_FAILURE;
    }

    chart.setMode(QStringLiteral("invalid"));
    if (chart.currentMode() != QStringLiteral("profile") || !group->isHidden()
        || chart.grab().isNull()) {
        std::cerr << "CPU chart fallback mode or rendering is broken.\n";
        return EXIT_FAILURE;
    }
    std::cout << "CPU core chart modes, gaps, paging and 60-sample history passed.\n";
    return EXIT_SUCCESS;
}
