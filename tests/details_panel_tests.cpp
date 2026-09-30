#include "widgets/details_panel.h"
#include "widgets/cpu_core_chart_widget.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QScrollBar>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace orion::app;

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    DetailsPanel panel;
    const auto find = [&panel](const QString& key) -> DetailCard*
    {
        for (auto* card : panel.findChildren<DetailCard*>())
        {
            if (card->property("detailSection").toString() == key)
                return card;
        }
        return nullptr;
    };
    bool ok = true;
    const auto check = [&ok](bool condition, const char* reason)
    {
        if (!condition)
        {
            std::cerr << reason << '\n';
            ok = false;
        }
    };
    auto* cpu = find(QStringLiteral("cpu"));
    auto* gpu = find(QStringLiteral("gpu"));
    auto* ram = find(QStringLiteral("ram"));
    auto* os = find(QStringLiteral("os"));
    check(cpu && gpu && ram && os, "Original detail cards are missing.");
    if (!ok)
        return EXIT_FAILURE;
    auto* columns = qobject_cast<QHBoxLayout*>(panel.widget()->layout());
    check(columns && columns->count() == 2 && columns->stretch(0) == 5 && columns->stretch(1) == 4,
        "Details must preserve the original 5:4 column layout.");
    check(cpu->parentWidget() == gpu->parentWidget() && os->parentWidget() == ram->parentWidget()
            && cpu->parentWidget() != ram->parentWidget(),
        "CPU/GPU must be left, OS/RAM/disks right.");
    check(gpu->fieldValue(QStringLiteral("vram")).contains(QStringLiteral("н/д")),
        "Unavailable GPU memory must not become zero.");

    DetailsSnapshot sample;
    sample.operatingSystem = QStringLiteral("Windows fixture");
    sample.cpuName = QStringLiteral("CPU fixture");
    sample.gpuName = QStringLiteral("GPU fixture");
    sample.cpuPercent = 45;
    sample.cpuFrequencyMhz = 3500;
    sample.cpuTemperatureC = 50;
    sample.logicalCpus = 20;
    sample.gpuPercent = 37;
    sample.gpuTemperatureC = 63;
    sample.gpuMemoryTotalGiB = 8;
    sample.gpuMemoryUsedGiB = 2;
    sample.gpuMemoryPercent = 25;
    sample.ramPercent = 50;
    sample.ramTotalGiB = 32;
    DiskTelemetry disk;
    disk.name = QStringLiteral("disk0");
    disk.mountPoint = QStringLiteral("C:\\");
    disk.storageType = QStringLiteral("NVMe SSD");
    disk.fileSystem = QStringLiteral("NTFS");
    disk.totalGiB = 100;
    disk.usedGiB = 50;
    disk.freeGiB = 50;
    disk.usedPercent = 50;
    disk.readMiBPerSecond = 1.25;
    disk.writeMiBPerSecond = 2.5;
    disk.totalReadGiB = 123.4;
    disk.totalWrittenGiB = 456.7;
    sample.disks = { disk };
    orion::core::SessionPeaks peaks;
    peaks.cpu = 91;
    peaks.gpu = 82;
    peaks.ram = 75;
    peaks.cpuTemperatureC = 79;
    peaks.gpuTemperatureC = 84;
    panel.updateSnapshot(sample, peaks);
    auto* storage = find(QStringLiteral("storage"));
    check(storage != nullptr, "Disk card was not created.");
    if (!storage)
        return EXIT_FAILURE;
    check(os->fieldValue(QStringLiteral("name")) == sample.operatingSystem, "OS version is absent.");
    check(cpu->fieldValue(QStringLiteral("usage")).contains(QStringLiteral("91.0%"))
            && gpu->fieldValue(QStringLiteral("usage")).contains(QStringLiteral("82.0%"))
            && ram->fieldValue(QStringLiteral("usage")).contains(QStringLiteral("75.0%")),
        "Session usage peaks are absent.");
    check(cpu->fieldValue(QStringLiteral("temp")).contains(QStringLiteral("79.0°C"))
            && gpu->fieldValue(QStringLiteral("temp")).contains(QStringLiteral("84.0°C")),
        "Temperature peaks are absent.");
    check(cpu->fieldValue(QStringLiteral("logical_cores")) == QStringLiteral("20")
            && cpu->fieldValue(QStringLiteral("freq")) == QStringLiteral("3500 МГц"),
        "CPU detail fields are absent.");
    check(ram->fieldValue(QStringLiteral("total")) == QStringLiteral("16.0 / 32.0 ГБ"),
        "RAM used/total is incorrect.");
    check(storage->fieldValue(QStringLiteral("read")).contains(QStringLiteral("123.4 ГБ"))
            && storage->fieldValue(QStringLiteral("write")).contains(QStringLiteral("456.7 ГБ")),
        "Cumulative disk I/O is missing.");
    const int fieldCount = storage->findChildren<QLabel*>().size();
    panel.updateSnapshot(sample, peaks);
    check(storage == find(QStringLiteral("storage")) && storage->findChildren<QLabel*>().size() == fieldCount,
        "A telemetry tick recreated existing disk rows.");

    sample.cpuTemperatureC = 86;
    sample.gpuPercent = 80;
    sample.disks[0].freeGiB = 4;
    panel.updateSnapshot(sample, peaks);
    check(cpu->property("statusLevel") == QStringLiteral("critical")
            && gpu->property("statusLevel") == QStringLiteral("warn")
            && storage->property("statusLevel") == QStringLiteral("critical"),
        "Python status thresholds were not retained.");
    panel.setThemeColors(QColor("#00E5FF"), QColor("#99AABB"));
    check(gpu->findChild<QFrame*>(QStringLiteral("DetailStatusStripe"))
              ->styleSheet()
              .contains(QStringLiteral("#00e5ff")),
        "Warning stripe did not follow a hot theme change.");
    sample.cpuPercent = -1;
    sample.cpuTemperatureC = std::numeric_limits<double>::quiet_NaN();
    sample.gpuMemoryUsedGiB = -1;
    sample.disks[0].readMiBPerSecond = -1;
    sample.disks[0].totalReadGiB = -1;
    panel.updateSnapshot(sample, peaks);
    check(cpu->fieldValue(QStringLiteral("usage")).startsWith(QStringLiteral("н/д"))
            && cpu->fieldValue(QStringLiteral("usage")).contains(QStringLiteral("91.0%")),
        "Missing current value erased the known session peak.");
    check(storage->fieldValue(QStringLiteral("read")) == QStringLiteral("н/д  (всего н/д)"),
        "Unknown I/O was fabricated.");
    check(gpu->fieldValue(QStringLiteral("vram")).startsWith(QStringLiteral("н/д")),
        "Unknown VRAM was fabricated.");
    QPointer<DetailCard> oldDisk(storage);
    sample.disks.clear();
    panel.updateSnapshot(sample, peaks);
    check(oldDisk.isNull() && find(QStringLiteral("storage")) == nullptr
            && !find(QStringLiteral("storage_empty"))->isHidden(),
        "Removed disks left stale cards behind.");
    sample.disks = { disk, disk };
    sample.disks[1].mountPoint = QStringLiteral("D:\\");
    panel.updateSnapshot(sample, peaks);
    int diskCount = 0;
    for (auto* card : panel.findChildren<DetailCard*>())
    {
        if (card->property("detailSection") == QStringLiteral("storage"))
            ++diskCount;
    }
    check(diskCount == 2, "Distinct volumes on one named disk were collapsed.");
    panel.resize(830, 400);
    panel.show();
    application.processEvents();
    check(panel.verticalScrollBar()->maximum() > 0, "Long details are not scrollable.");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
