#include "network_scanner_worker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>

namespace {

[[nodiscard]] bool waitUntil(
    const std::function<bool()>& predicate,
    const int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return predicate();
}

[[nodiscard]] orion::app::NetworkInterfaceInfo smallInterface()
{
    orion::app::NetworkInterfaceInfo info;
    info.name = QStringLiteral("fixture0");
    info.displayName = QStringLiteral("Fixture Ethernet");
    info.address = QStringLiteral("192.168.50.10");
    info.netmask = QStringLiteral("255.255.255.252");
    info.mac = QStringLiteral("AC:FD:CE:00:00:01");
    info.prefixLength = 30;
    return info;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);

    const auto small = smallInterface();
    bool truncated = false;
    QString effectiveNetwork;
    QString originalNetwork;
    const QStringList smallTargets = orion::app::NetworkScannerWorker::scanTargetsFor(
        small, &truncated, &effectiveNetwork, &originalNetwork);
    if (truncated || effectiveNetwork != QStringLiteral("192.168.50.8/30")
        || originalNetwork != effectiveNetwork
        || smallTargets != QStringList {QStringLiteral("192.168.50.9")}) {
        std::cerr << "The scanner built an incorrect bounded IPv4 scope.\n";
        return EXIT_FAILURE;
    }

    auto large = small;
    large.address = QStringLiteral("10.20.30.40");
    large.netmask = QStringLiteral("255.255.0.0");
    large.prefixLength = 16;
    const QStringList largeTargets = orion::app::NetworkScannerWorker::scanTargetsFor(
        large, &truncated, &effectiveNetwork, &originalNetwork);
    if (!truncated || originalNetwork != QStringLiteral("10.20.0.0/16")
        || effectiveNetwork != QStringLiteral("10.20.30.0/24")
        || largeTargets.size() != 253 || largeTargets.contains(large.address)) {
        std::cerr << "A large subnet was not safely constrained to the local /24.\n";
        return EXIT_FAILURE;
    }

    if (orion::app::NetworkScannerWorker::vendorForMac(
            QStringLiteral("ac-fd-ce-12-34-56")) != QStringLiteral("Intel")
        || orion::app::NetworkScannerWorker::vendorForMac(
            QStringLiteral("02:11:22:33:44:55"))
            != QStringLiteral("Приватный/случайный MAC")
        || orion::app::NetworkScannerWorker::vendorForMac({})
            != QStringLiteral("Неизвестное устройство")) {
        std::cerr << "MAC vendor classification is not deterministic.\n";
        return EXIT_FAILURE;
    }

    int probeCalls = 0;
    orion::app::NetworkScannerWorker successWorker(nullptr,
        [&](const QString& address, const orion::app::NetworkInterfaceInfo&,
            const int, const std::atomic_bool&) -> std::optional<orion::app::NetworkDevice> {
            ++probeCalls;
            orion::app::NetworkDevice device;
            device.ip = address;
            device.mac = QStringLiteral("00:50:56:AA:BB:CC");
            device.name = QStringLiteral("fixture-host");
            device.typeOrVendor = QStringLiteral("VMware");
            device.confidence = QStringLiteral("высокая");
            device.identificationEvidence = {QStringLiteral("детерминированный тест")};
            device.latencyMs = 4.0;
            return device;
        });
    orion::app::NetworkScanResult successResult;
    int finalProgress = -1;
    bool successCompleted = false;
    QObject::connect(&successWorker, &orion::app::NetworkScannerWorker::progressChanged,
        &application, [&](const int value, const QString&) { finalProgress = value; });
    QObject::connect(&successWorker, &orion::app::NetworkScannerWorker::scanCompleted,
        &application, [&](const auto& result) {
            successResult = result;
            successCompleted = true;
        });
    if (!successWorker.setInterface(small)) {
        std::cerr << "The worker rejected a valid fixture interface.\n";
        return EXIT_FAILURE;
    }
    successWorker.start();
    if (!waitUntil([&] { return successCompleted; }, 2000)
        || !successWorker.wait(1000) || successResult.cancelled
        || probeCalls != 1 || successResult.devices.size() != 2
        || successResult.identified != 2 || successResult.identityLimited
        || successResult.identityLimit != 64
        || successResult.devices.at(0).ip != QStringLiteral("192.168.50.9")
        || successResult.devices.at(1).ip != small.address
        || finalProgress != 100) {
        successWorker.requestStop();
        successWorker.wait(1000);
        std::cerr << "The worker did not publish a complete deterministic scan.\n";
        return EXIT_FAILURE;
    }

    orion::app::NetworkScannerWorker cancelWorker(nullptr,
        [](const QString&, const orion::app::NetworkInterfaceInfo&,
            const int, const std::atomic_bool& stopped)
            -> std::optional<orion::app::NetworkDevice> {
            for (int step = 0; step < 20 && !stopped.load(); ++step) QThread::msleep(5);
            return std::nullopt;
        });
    orion::app::NetworkScanResult cancelResult;
    bool cancelCompleted = false;
    QObject::connect(&cancelWorker, &orion::app::NetworkScannerWorker::scanCompleted,
        &application, [&](const auto& result) {
            cancelResult = result;
            cancelCompleted = true;
        });
    large.prefixLength = 24;
    large.netmask = QStringLiteral("255.255.255.0");
    if (!cancelWorker.setInterface(large)) return EXIT_FAILURE;
    cancelWorker.start();
    QThread::msleep(30);
    cancelWorker.requestStop();
    if (!waitUntil([&] { return cancelCompleted; }, 2000)
        || !cancelWorker.wait(1000) || !cancelResult.cancelled) {
        cancelWorker.requestStop();
        cancelWorker.wait(1000);
        std::cerr << "Cancellation did not stop the scanner within its bounded probe window.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Native network scanner scope, identity, completion and cancellation tests passed.\n";
    return EXIT_SUCCESS;
}
