#include "hardware_inventory_worker.h"
#include "hardware_inventory_details.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

bool expect(const bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

QJsonObject report(const double cores, const double ram, const QString& gpu,
                   const QJsonValue& memory)
{
    return {
        {QStringLiteral("cpu"), QJsonObject {{QStringLiteral("physical_cores"), cores}}},
        {QStringLiteral("ram"), QJsonObject {{QStringLiteral("total_gb"), ram}}},
        {QStringLiteral("gpu"), QJsonArray {QJsonObject {
            {QStringLiteral("model"), gpu}, {QStringLiteral("memory_mb"), memory}}}},
    };
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    using orion::app::HardwareInventoryWorker;

    bool ok = true;
    using orion::app::groupHardwareDisks;
    using orion::app::hardwareLinkMbps;
    ok &= expect(hardwareLinkMbps(2500000000ULL).toDouble() == 2500.0
        && hardwareLinkMbps(10000000).toDouble() == 10.0,
        "Link speed must convert decimal bits/s to Mbps without integer overflow.");
    ok &= expect(hardwareLinkMbps(0).isNull()
        && hardwareLinkMbps(std::numeric_limits<quint64>::max()).isNull(),
        "Unknown link rates must remain null, not zero or a giant invented speed.");
    const QJsonArray physical {
        QJsonObject {{QStringLiteral("Index"), QStringLiteral("2")}, {QStringLiteral("Model"), QStringLiteral("Same model")}},
        QJsonObject {{QStringLiteral("Index"), 0}, {QStringLiteral("Model"), QStringLiteral("Same model")}},
    };
    const auto volume = [](const QString& mount, const QJsonArray& ids, const QString& quality = QStringLiteral("valid")) {
        return QJsonObject {{QStringLiteral("mountpoint"), mount},
            {QStringLiteral("physical_disk_numbers"), ids}, {QStringLiteral("mapping_quality"), quality}};
    };
    const QJsonArray volumes {
        volume(QStringLiteral("C:\\"), {0}), volume(QStringLiteral("D:\\"), {2}),
        volume(QStringLiteral("E:\\"), {0, 0}), volume(QStringLiteral("F:\\"), {0, 2}),
        volume(QStringLiteral("G:\\"), {}, QStringLiteral("unavailable")),
        volume(QStringLiteral("H:\\"), {7}),
        volume(QStringLiteral("I:\\"), {-1, QStringLiteral("invalid")}),
        volume(QStringLiteral("J:\\"), {0}, QStringLiteral("unavailable")),
    };
    const auto grouped = groupHardwareDisks(physical, volumes);
    const auto groups = grouped.value(QStringLiteral("groups")).toArray();
    ok &= expect(groups.size() == 3 && grouped.value(QStringLiteral("unmapped_volumes")).toArray().size() == 3,
        "Unknown disk identities must stay explicit; invalid mapping must not become disk zero.");
    if (groups.size() == 3) {
        const auto disk2 = groups.at(0).toObject().value(QStringLiteral("volumes")).toArray();
        const auto disk0 = groups.at(1).toObject().value(QStringLiteral("volumes")).toArray();
        ok &= expect(disk2.size() == 2 && disk0.size() == 3,
            "Volumes must map by native disk number, not WMI list order or identical model name.");
        ok &= expect(disk0.at(0).toObject().value(QStringLiteral("mountpoint")) == QStringLiteral("C:\\")
            && disk2.at(0).toObject().value(QStringLiteral("mountpoint")) == QStringLiteral("D:\\")
            && disk0.at(2).toObject().value(QStringLiteral("spanned")).toBool()
            && disk2.at(1).toObject().value(QStringLiteral("spanned")).toBool(),
            "Spanned volumes must retain each backing disk without guessing a single owner.");
    }
    const auto absentIndex = groupHardwareDisks({QJsonObject {{QStringLiteral("Model"), QStringLiteral("No index")}}},
        {volume(QStringLiteral("C:\\"), {0})}).value(QStringLiteral("groups")).toArray();
    ok &= expect(absentIndex.size() == 2
        && absentIndex.at(0).toObject().value(QStringLiteral("volumes")).toArray().isEmpty(),
        "A missing WMI disk index must not alias disk 0.");
    ok &= expect(HardwareInventoryWorker::memoryTypeName(20) == QStringLiteral("DDR")
        && HardwareInventoryWorker::memoryTypeName(21) == QStringLiteral("DDR2"),
        "Legacy DDR module types must retain their SMBIOS identity.");
    ok &= expect(HardwareInventoryWorker::ratePc({}).value(QStringLiteral("key")).toString()
        == QStringLiteral("insufficient"), "Missing CPU/RAM must not receive a hardware rating.");
    ok &= expect(HardwareInventoryWorker::memoryTypeName(24) == QStringLiteral("DDR3"),
        "SMBIOS DDR3 mapping differs from the Python contract.");
    ok &= expect(HardwareInventoryWorker::memoryTypeName(26) == QStringLiteral("DDR4"),
        "SMBIOS DDR4 mapping differs from the Python contract.");
    ok &= expect(HardwareInventoryWorker::memoryTypeName(34) == QStringLiteral("DDR5"),
        "SMBIOS DDR5 mapping differs from the Python contract.");
    ok &= expect(HardwareInventoryWorker::memoryTypeName(0) == QStringLiteral("н/д"),
        "Unknown SMBIOS memory types must remain unavailable.");
    ok &= expect(HardwareInventoryWorker::normaliseFirmwareDate(
        QStringLiteral("20240517000000.000000+000")) == QStringLiteral("2024-05-17"),
        "WMI firmware date was not normalised.");
    ok &= expect(HardwareInventoryWorker::normaliseFirmwareDate(
        QStringLiteral("2024-05-17T00:00:00")) == QStringLiteral("2024-05-17"),
        "ISO firmware date was not normalised.");

    ok &= expect(HardwareInventoryWorker::isNpuDeviceName(
        QStringLiteral("Intel(R) AI Boost")), "Intel AI Boost should be recognised as an NPU.");
    ok &= expect(HardwareInventoryWorker::isNpuDeviceName(
        QStringLiteral("AMD XDNA NPU Device")), "AMD XDNA should be recognised as an NPU.");
    ok &= expect(!HardwareInventoryWorker::isNpuDeviceName(
        QStringLiteral("NVIDIA GeForce RTX 4070")), "A normal GPU must not be called an NPU.");
    ok &= expect(!HardwareInventoryWorker::isNpuDeviceName(
        QStringLiteral("Generic PCI Accelerator")), "A generic accelerator must not be called an NPU.");

    const auto gaming = HardwareInventoryWorker::ratePc(report(
        8.0, 32.0, QStringLiteral("NVIDIA GeForce RTX 4070"), QJsonValue::Null));
    ok &= expect(gaming.value(QStringLiteral("key")).toString() == QStringLiteral("gaming"),
        "Gaming rating thresholds differ from Python.");
    const auto gamingReasons = gaming.value(QStringLiteral("reasons")).toArray();
    ok &= expect(!gamingReasons.isEmpty()
            && gamingReasons.at(gamingReasons.size() - 1).toString()
                .contains(QStringLiteral("только по имени")),
        "Name-only GPU confidence must be disclosed.");
    ok &= expect(HardwareInventoryWorker::ratePc(report(
        2.0, 16.0, QStringLiteral("Intel UHD Graphics"), 1024.0))
            .value(QStringLiteral("key")).toString() == QStringLiteral("weak"),
        "Two-core hardware must remain weak.");
    ok &= expect(HardwareInventoryWorker::ratePc(report(
        6.0, 16.0, QStringLiteral("Intel UHD Graphics"), 1024.0))
            .value(QStringLiteral("key")).toString() == QStringLiteral("ordinary"),
        "Integrated balanced hardware must remain ordinary.");
    const auto knownVram = HardwareInventoryWorker::ratePc(report(
        8.0, 32.0, QStringLiteral("Professional Discrete Adapter"), 8192.0));
    ok &= expect(knownVram.value(QStringLiteral("key")).toString() == QStringLiteral("gaming")
            && knownVram.value(QStringLiteral("gpu_confidence")).toString()
                == QStringLiteral("known"),
        "A discrete GPU with at least 4 GB of confirmed VRAM must meet the Python gaming class.");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
