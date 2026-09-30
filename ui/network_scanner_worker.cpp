#include "network_scanner_worker.h"

#include "network_identity.h"

#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QHostInfo>
#include <QMutexLocker>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QTcpSocket>
#include <QTextStream>
#include <QtEndian>

#include <algorithm>
#include <future>
#include <iterator>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#endif

namespace orion::app {
namespace {

constexpr int maximumScanHosts = 1022;
constexpr int maximumConcurrentProbes = 32;
constexpr int perHostTimeoutMs = 700;

struct TcpReachabilityResult {
    double latencyMs {-1.0};
    quint16 openPort {0};
    QString evidence;
};

[[nodiscard]] quint32 prefixMask(const int prefixLength)
{
    if (prefixLength <= 0) return 0;
    if (prefixLength >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32 - prefixLength);
}

[[nodiscard]] QString normalizedMac(QString mac)
{
    mac = mac.trimmed().toUpper();
    mac.replace(QLatin1Char('-'), QLatin1Char(':'));
    return mac;
}

[[nodiscard]] quint32 ipv4Number(const QString& address)
{
    bool ok = false;
    const quint32 value = QHostAddress(address).toIPv4Address(&ok);
    return ok ? value : 0;
}

[[nodiscard]] QString networkText(const quint32 network, const int prefix)
{
    return QStringLiteral("%1/%2").arg(QHostAddress(network).toString()).arg(prefix);
}

[[nodiscard]] QString gatewayForInterface(
    const QString& interfaceName, const QString& interfaceAddress)
{
#ifdef Q_OS_WIN
    ULONG size = 15 * 1024;
    QByteArray storage(static_cast<int>(size), Qt::Uninitialized);
    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    ULONG status = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS,
        nullptr, addresses, &size);
    if (status == ERROR_BUFFER_OVERFLOW) {
        storage.resize(static_cast<int>(size));
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        status = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS,
            nullptr, addresses, &size);
    }
    if (status != NO_ERROR) return {};
    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        bool matches = QString::fromLatin1(adapter->AdapterName) == interfaceName;
        for (auto* unicast = adapter->FirstUnicastAddress; !matches && unicast != nullptr;
             unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr == nullptr
                || unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* socketAddress = reinterpret_cast<const sockaddr_in*>(
                unicast->Address.lpSockaddr);
            matches = QHostAddress(ntohl(socketAddress->sin_addr.s_addr)).toString()
                == interfaceAddress;
        }
        if (!matches) continue;
        for (auto* gateway = adapter->FirstGatewayAddress; gateway != nullptr;
             gateway = gateway->Next) {
            if (gateway->Address.lpSockaddr == nullptr
                || gateway->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* socketAddress = reinterpret_cast<const sockaddr_in*>(
                gateway->Address.lpSockaddr);
            return QHostAddress(ntohl(socketAddress->sin_addr.s_addr)).toString();
        }
    }
#elif defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/net/route"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream stream(&file);
    stream.readLine();
    while (!stream.atEnd()) {
        const QStringList fields = stream.readLine().simplified().split(QLatin1Char(' '));
        if (fields.size() < 4 || fields.at(0) != interfaceName
            || fields.at(1) != QStringLiteral("00000000")) continue;
        bool ok = false;
        const quint32 littleEndian = fields.at(2).toUInt(&ok, 16);
        if (ok) return QHostAddress(qFromLittleEndian(littleEndian)).toString();
    }
#else
    Q_UNUSED(interfaceName)
    Q_UNUSED(interfaceAddress)
#endif
    return {};
}

#ifndef Q_OS_WIN
[[nodiscard]] QString macFromLinuxNeighborTable(const QString& address)
{
    QFile file(QStringLiteral("/proc/net/arp"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream stream(&file);
    stream.readLine();
    while (!stream.atEnd()) {
        const QStringList fields = stream.readLine().simplified().split(QLatin1Char(' '));
        if (fields.size() >= 4 && fields.at(0) == address) {
            const QString mac = normalizedMac(fields.at(3));
            return mac == QStringLiteral("00:00:00:00:00:00") ? QString() : mac;
        }
    }
    return {};
}
#endif

[[nodiscard]] std::optional<TcpReachabilityResult> tcpReachability(
    const QString& address,
    const QString& sourceAddress,
    const int timeoutMs,
    const std::atomic_bool& stopRequested)
{
    static constexpr quint16 ports[] {80, 443, 445, 22, 3389};
    const int timeoutPerPort = std::max(60, timeoutMs / static_cast<int>(std::size(ports)));
    for (const quint16 port : ports) {
        if (stopRequested.load(std::memory_order_relaxed)) return std::nullopt;
        QTcpSocket socket;
        if (!sourceAddress.isEmpty()) {
            socket.bind(QHostAddress(sourceAddress), 0,
                QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint);
        }
        QElapsedTimer timer;
        timer.start();
        socket.connectToHost(address, port);
        const bool connected = socket.waitForConnected(timeoutPerPort);
        const bool refused = !connected
            && socket.error() == QAbstractSocket::ConnectionRefusedError;
        if (connected || refused) {
            const double latency = static_cast<double>(timer.elapsed());
            socket.abort();
            return TcpReachabilityResult {latency,
                connected ? port : static_cast<quint16>(0),
                connected ? QStringLiteral("TCP %1").arg(port)
                          : QStringLiteral("TCP reset %1").arg(port)};
        }
        socket.abort();
    }
    return std::nullopt;
}

} // namespace

bool NetworkInterfaceInfo::isValid() const
{
    const QHostAddress parsed(address);
    return parsed.protocol() == QAbstractSocket::IPv4Protocol
        && prefixLength > 0 && prefixLength <= 32;
}

QString NetworkInterfaceInfo::label() const
{
    const QString title = displayName.isEmpty() ? name : displayName;
    return QStringLiteral("%1 — %2/%3").arg(title, address).arg(prefixLength);
}

NetworkScannerWorker::NetworkScannerWorker(QObject* parent, ProbeFunction probe)
    : QThread(parent)
    , probe_(std::move(probe))
{
    qRegisterMetaType<NetworkDevice>();
    qRegisterMetaType<NetworkScanResult>();
}

NetworkScannerWorker::~NetworkScannerWorker()
{
    requestStop();
    wait(2000);
}

QVector<NetworkInterfaceInfo> NetworkScannerWorker::listIpv4Interfaces()
{
    QVector<NetworkInterfaceInfo> result;
    for (const QNetworkInterface& networkInterface : QNetworkInterface::allInterfaces()) {
        const auto flags = networkInterface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : networkInterface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol
                || entry.ip().isLoopback() || entry.ip().isLinkLocal()) {
                continue;
            }
            NetworkInterfaceInfo info;
            info.name = networkInterface.name();
            info.displayName = networkInterface.humanReadableName();
            info.address = entry.ip().toString();
            info.netmask = entry.netmask().toString();
            info.mac = normalizedMac(networkInterface.hardwareAddress());
            info.gateway = gatewayForInterface(info.name, info.address);
            info.prefixLength = entry.prefixLength();
            if (info.isValid()) result.push_back(std::move(info));
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.displayName != right.displayName) return left.displayName < right.displayName;
        return ipv4Number(left.address) < ipv4Number(right.address);
    });
    return result;
}

QStringList NetworkScannerWorker::scanTargetsFor(
    const NetworkInterfaceInfo& interfaceInfo,
    bool* truncated,
    QString* effectiveNetwork,
    QString* originalNetwork)
{
    if (truncated != nullptr) *truncated = false;
    if (effectiveNetwork != nullptr) effectiveNetwork->clear();
    if (originalNetwork != nullptr) originalNetwork->clear();
    if (!interfaceInfo.isValid()) return {};

    const quint32 local = ipv4Number(interfaceInfo.address);
    int prefix = interfaceInfo.prefixLength;
    quint32 mask = prefixMask(prefix);
    quint32 network = local & mask;
    if (originalNetwork != nullptr) *originalNetwork = networkText(network, prefix);

    const quint64 addressCount = 1ull << (32 - prefix);
    const quint64 usableCount = prefix <= 30 ? addressCount - 2 : addressCount;
    if (usableCount > maximumScanHosts) {
        prefix = 24;
        mask = prefixMask(prefix);
        network = local & mask;
        if (truncated != nullptr) *truncated = true;
    }
    if (effectiveNetwork != nullptr) *effectiveNetwork = networkText(network, prefix);

    quint64 first = network;
    quint64 last = static_cast<quint64>(network) + (1ull << (32 - prefix)) - 1;
    if (prefix <= 30) {
        ++first;
        --last;
    } else if (prefix == 32) {
        return {};
    }

    QStringList targets;
    targets.reserve(static_cast<qsizetype>(std::min<quint64>(maximumScanHosts, last - first + 1)));
    for (quint64 candidate = first; candidate <= last
        && targets.size() < maximumScanHosts; ++candidate) {
        if (candidate == local) continue;
        targets.push_back(QHostAddress(static_cast<quint32>(candidate)).toString());
    }
    return targets;
}

QString NetworkScannerWorker::vendorForMac(const QString& mac)
{
    static const QHash<QString, QString> vendors {
        {QStringLiteral("00:03:93"), QStringLiteral("Apple")},
        {QStringLiteral("00:0A:95"), QStringLiteral("Apple")},
        {QStringLiteral("00:00:F0"), QStringLiteral("Samsung")},
        {QStringLiteral("8C:71:F8"), QStringLiteral("Samsung")},
        {QStringLiteral("04:CF:8C"), QStringLiteral("Xiaomi")},
        {QStringLiteral("D4:97:0B"), QStringLiteral("Xiaomi")},
        {QStringLiteral("14:CC:20"), QStringLiteral("TP-Link")},
        {QStringLiteral("F0:9F:C2"), QStringLiteral("TP-Link")},
        {QStringLiteral("F4:4C:7F"), QStringLiteral("Huawei")},
        {QStringLiteral("BC:AE:C5"), QStringLiteral("ASUS")},
        {QStringLiteral("F8:1A:67"), QStringLiteral("D-Link")},
        {QStringLiteral("28:28:5D"), QStringLiteral("Keenetic")},
        {QStringLiteral("E4:8D:8C"), QStringLiteral("MikroTik")},
        {QStringLiteral("F4:F5:D8"), QStringLiteral("Google")},
        {QStringLiteral("F0:27:2D"), QStringLiteral("Amazon")},
        {QStringLiteral("28:57:BE"), QStringLiteral("Hikvision")},
        {QStringLiteral("3C:EF:8C"), QStringLiteral("Dahua")},
        {QStringLiteral("00:E0:4C"), QStringLiteral("Realtek")},
        {QStringLiteral("00:02:B3"), QStringLiteral("Intel")},
        {QStringLiteral("AC:FD:CE"), QStringLiteral("Intel")},
        {QStringLiteral("00:0C:29"), QStringLiteral("VMware")},
        {QStringLiteral("00:50:56"), QStringLiteral("VMware")},
    };
    const QString normalized = normalizedMac(mac);
    if (normalized.size() < 8) return QStringLiteral("Неизвестное устройство");
    bool firstByteOk = false;
    const int firstByte = normalized.left(2).toInt(&firstByteOk, 16);
    if (firstByteOk && (firstByte & 0x02) != 0) {
        return QStringLiteral("Приватный/случайный MAC");
    }
    return vendors.value(normalized.left(8), QStringLiteral("Неизвестное устройство"));
}

bool NetworkScannerWorker::setInterface(const NetworkInterfaceInfo& interfaceInfo)
{
    if (isRunning() || !interfaceInfo.isValid()) return false;
    const QMutexLocker lock(&configMutex_);
    interfaceInfo_ = interfaceInfo;
    stopRequested_.store(false, std::memory_order_relaxed);
    return true;
}

void NetworkScannerWorker::requestStop()
{
    stopRequested_.store(true, std::memory_order_relaxed);
}

void NetworkScannerWorker::run()
{
    NetworkInterfaceInfo interfaceInfo;
    {
        const QMutexLocker lock(&configMutex_);
        interfaceInfo = interfaceInfo_;
    }
    if (!interfaceInfo.isValid()) {
        emit scanFailed(QStringLiteral("выбранный IPv4-адаптер недоступен"));
        return;
    }

    QElapsedTimer elapsed;
    elapsed.start();
    NetworkScanResult result;
    QStringList targets = scanTargetsFor(interfaceInfo, &result.truncated,
        &result.network, &result.originalNetwork);

    NetworkDevice local;
    local.ip = interfaceInfo.address;
    local.mac = interfaceInfo.mac;
    local.name = QHostInfo::localHostName();
    const QString localVendor = vendorForMac(local.mac);
    local.typeOrVendor = localVendor == QStringLiteral("Неизвестное устройство")
        ? QStringLiteral("Локальный компьютер")
        : QStringLiteral("Локальный компьютер · %1").arg(localVendor);
    local.confidence = QStringLiteral("высокая");
    local.identificationEvidence = {QStringLiteral("выбранный локальный IPv4-адаптер")};
    local.latencyMs = 0.0;
    local.local = true;
    result.devices.push_back(local);
    emit deviceFound(local);
    emit progressChanged(3, QStringLiteral("Подготовлено %1 адресов в %2")
        .arg(targets.size()).arg(result.network));

    const bool customProbe = static_cast<bool>(probe_);
    ProbeFunction probe = probe_;
    if (!probe) probe = &NetworkScannerWorker::probeAddress;
    const int batchSize = std::min(maximumConcurrentProbes,
        std::max(4, QThread::idealThreadCount() * 2));
    for (int offset = 0; offset < targets.size() && !stopRequested_.load();
         offset += batchSize) {
        const int end = std::min(offset + batchSize, static_cast<int>(targets.size()));
        std::vector<std::future<std::optional<NetworkDevice>>> futures;
        futures.reserve(static_cast<std::size_t>(end - offset));
        for (int index = offset; index < end; ++index) {
            const QString address = targets.at(index);
            futures.push_back(std::async(std::launch::async,
                [&, address] { return probe(address, interfaceInfo,
                    perHostTimeoutMs, stopRequested_); }));
        }
        for (auto& future : futures) {
            if (auto device = future.get(); device.has_value()) {
                if (device->ip.isEmpty()) continue;
                if (device->typeOrVendor.isEmpty()) {
                    device->typeOrVendor = vendorForMac(device->mac);
                }
                result.devices.push_back(*device);
                emit deviceFound(*device);
            }
        }
        const int checked = end;
        const int percent = targets.isEmpty() ? 74
            : 5 + static_cast<int>(69.0 * checked / targets.size());
        emit progressChanged(percent,
            QStringLiteral("Проверено %1 из %2; найдено %3")
                .arg(checked).arg(targets.size()).arg(result.devices.size()));
    }

    if (!stopRequested_.load(std::memory_order_relaxed) && !customProbe) {
        emit progressChanged(75, QStringLiteral("Поиск объявлений SSDP/UPnP…"));
        const auto ssdpDevices = NetworkIdentity::discoverSsdp(
            interfaceInfo, stopRequested_);
        QHash<QString, int> rowByAddress;
        for (int index = 0; index < result.devices.size(); ++index) {
            rowByAddress.insert(result.devices.at(index).ip, index);
        }
        for (auto iterator = ssdpDevices.constBegin(); iterator != ssdpDevices.constEnd();
             ++iterator) {
            int index = rowByAddress.value(iterator.key(), -1);
            if (index < 0) {
                NetworkDevice device;
                device.ip = iterator.key();
                device.typeOrVendor = vendorForMac({});
                device.confidence = QStringLiteral("низкая");
                device.identificationEvidence = {QStringLiteral("ответ SSDP/UPnP")};
                result.devices.push_back(device);
                index = result.devices.size() - 1;
                rowByAddress.insert(iterator.key(), index);
                emit deviceFound(device);
            } else if (!result.devices[index].identificationEvidence.contains(
                           QStringLiteral("ответ SSDP/UPnP"))) {
                result.devices[index].identificationEvidence.push_back(
                    QStringLiteral("ответ SSDP/UPnP"));
            }
        }

        QVector<int> identityTargets;
        identityTargets.reserve(result.devices.size());
        for (int index = 0; index < result.devices.size(); ++index) {
            if (!result.devices.at(index).local) identityTargets.push_back(index);
        }
        std::sort(identityTargets.begin(), identityTargets.end(),
            [&](const int left, const int right) {
                const auto& leftDevice = result.devices.at(left);
                const auto& rightDevice = result.devices.at(right);
                const bool leftSsdp = ssdpDevices.contains(leftDevice.ip);
                const bool rightSsdp = ssdpDevices.contains(rightDevice.ip);
                if (leftSsdp != rightSsdp) return leftSsdp;
                const bool leftGateway = !interfaceInfo.gateway.isEmpty()
                    && leftDevice.ip == interfaceInfo.gateway;
                const bool rightGateway = !interfaceInfo.gateway.isEmpty()
                    && rightDevice.ip == interfaceInfo.gateway;
                if (leftGateway != rightGateway) return leftGateway;
                return ipv4Number(leftDevice.ip) < ipv4Number(rightDevice.ip);
            });
        result.identityLimited = identityTargets.size() > NetworkIdentity::maximumIdentityHosts;
        if (result.identityLimited) {
            identityTargets.resize(NetworkIdentity::maximumIdentityHosts);
        }
        emit progressChanged(80,
            QStringLiteral("Определение имён, типов и служб для %1 устройств…")
                .arg(identityTargets.size()));

        constexpr int maximumConcurrentIdentity = 16;
        int completed = 0;
        for (int offset = 0; offset < identityTargets.size()
            && !stopRequested_.load(std::memory_order_relaxed);
             offset += maximumConcurrentIdentity) {
            const int end = std::min(offset + maximumConcurrentIdentity,
                static_cast<int>(identityTargets.size()));
            std::vector<std::future<QPair<int, NetworkIdentityOutput>>> futures;
            futures.reserve(static_cast<std::size_t>(end - offset));
            for (int position = offset; position < end; ++position) {
                const int deviceIndex = identityTargets.at(position);
                const NetworkDevice device = result.devices.at(deviceIndex);
                NetworkIdentityInput input;
                input.ip = device.ip;
                input.mac = device.mac;
                input.vendor = vendorForMac(device.mac);
                input.gateway = !interfaceInfo.gateway.isEmpty()
                    && device.ip == interfaceInfo.gateway;
                input.knownOpenPorts = device.openPorts;
                input.metadata = ssdpDevices.value(device.ip);
                futures.push_back(std::async(std::launch::async,
                    [&, deviceIndex, input] {
                        return qMakePair(deviceIndex,
                            NetworkIdentity::identify(interfaceInfo, input, stopRequested_));
                    }));
            }
            for (auto& future : futures) {
                const auto enriched = future.get();
                NetworkDevice& device = result.devices[enriched.first];
                const NetworkIdentityOutput& identity = enriched.second;
                device.name = identity.name;
                device.nameSource = identity.nameSource;
                device.manufacturer = identity.vendor;
                device.model = identity.model;
                device.services = identity.services;
                device.openPorts = identity.openPorts;
                device.confidence = identity.confidence;
                for (const QString& evidence : identity.evidence) {
                    if (!device.identificationEvidence.contains(evidence)) {
                        device.identificationEvidence.push_back(evidence);
                    }
                }
                QStringList typeParts {identity.deviceType};
                if (!identity.model.isEmpty()
                    && !identity.name.contains(identity.model, Qt::CaseInsensitive)) {
                    typeParts.push_back(identity.model);
                }
                if (!identity.vendor.isEmpty()
                    && identity.vendor != QStringLiteral("Неизвестное устройство")
                    && identity.vendor != QStringLiteral("Неизвестный производитель")
                    && identity.vendor != QStringLiteral("Приватный/случайный MAC")) {
                    typeParts.push_back(identity.vendor);
                }
                device.typeOrVendor = typeParts.join(QStringLiteral(" · "));
                emit deviceFound(device);
                ++completed;
                const int percent = identityTargets.isEmpty() ? 98
                    : 80 + static_cast<int>(18.0 * completed / identityTargets.size());
                emit progressChanged(percent,
                    QStringLiteral("Опознано %1/%2 устройств")
                        .arg(completed).arg(identityTargets.size()));
            }
        }
    }

    result.cancelled = stopRequested_.load(std::memory_order_relaxed);
    std::sort(result.devices.begin(), result.devices.end(), [](const auto& left, const auto& right) {
        return ipv4Number(left.ip) < ipv4Number(right.ip);
    });
    result.elapsedMs = elapsed.elapsed();
    result.identified = 0;
    for (const NetworkDevice& device : std::as_const(result.devices)) {
        if (device.local || (!device.name.isEmpty()
                && device.name != QStringLiteral("Имя не сообщено")
                && device.name != QStringLiteral("Шлюз / роутер"))) {
            ++result.identified;
        }
    }
    if (!result.cancelled) {
        emit progressChanged(100, QStringLiteral("Сканирование завершено"));
    }
    emit scanCompleted(result);
}

std::optional<NetworkDevice> NetworkScannerWorker::probeAddress(
    const QString& address,
    const NetworkInterfaceInfo& interfaceInfo,
    const int timeoutMs,
    const std::atomic_bool& stopRequested)
{
    if (stopRequested.load(std::memory_order_relaxed)) return std::nullopt;
    std::optional<double> latency;
    quint16 openPort = 0;
    QString mac;
    QString evidence;

#ifdef Q_OS_WIN
    const QByteArray destinationBytes = address.toLatin1();
    const QByteArray sourceBytes = interfaceInfo.address.toLatin1();
    const IPAddr destination = inet_addr(destinationBytes.constData());
    const IPAddr source = inet_addr(sourceBytes.constData());
    if (destination != INADDR_NONE && source != INADDR_NONE) {
        const HANDLE icmp = IcmpCreateFile();
        if (icmp != INVALID_HANDLE_VALUE) {
            const QByteArray payload("ORION_NETSCAN");
            std::vector<unsigned char> reply(
                sizeof(ICMP_ECHO_REPLY) + static_cast<std::size_t>(payload.size()) + 16);
            const DWORD count = IcmpSendEcho2Ex(icmp, nullptr, nullptr, nullptr,
                source, destination, const_cast<char*>(payload.constData()),
                static_cast<WORD>(payload.size()), nullptr, reply.data(),
                static_cast<DWORD>(reply.size()), static_cast<DWORD>(std::min(timeoutMs, 350)));
            if (count > 0) {
                const auto* echo = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply.data());
                if (echo->Status == IP_SUCCESS) {
                    latency = static_cast<double>(echo->RoundTripTime);
                    evidence = QStringLiteral("ответ ICMP");
                }
            }
            IcmpCloseHandle(icmp);
        }
    }
#endif

    if (!latency.has_value() && !stopRequested.load(std::memory_order_relaxed)) {
        const auto tcp = tcpReachability(address, interfaceInfo.address,
            std::max(300, timeoutMs / 2), stopRequested);
        if (tcp.has_value()) {
            latency = tcp->latencyMs;
            openPort = tcp->openPort;
            evidence = tcp->evidence;
        }
    }
    if (!latency.has_value() || stopRequested.load(std::memory_order_relaxed)) {
        return std::nullopt;
    }

#ifdef Q_OS_WIN
    const IPAddr arpDestination = inet_addr(address.toLatin1().constData());
    const IPAddr arpSource = inet_addr(interfaceInfo.address.toLatin1().constData());
    ULONG macBuffer[2] {};
    ULONG macLength = 6;
    if (arpDestination != INADDR_NONE
        && SendARP(arpDestination, arpSource, macBuffer, &macLength) == NO_ERROR
        && macLength >= 6) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(macBuffer);
        mac = QStringLiteral("%1:%2:%3:%4:%5:%6")
            .arg(bytes[0], 2, 16, QLatin1Char('0'))
            .arg(bytes[1], 2, 16, QLatin1Char('0'))
            .arg(bytes[2], 2, 16, QLatin1Char('0'))
            .arg(bytes[3], 2, 16, QLatin1Char('0'))
            .arg(bytes[4], 2, 16, QLatin1Char('0'))
            .arg(bytes[5], 2, 16, QLatin1Char('0')).toUpper();
    }
#else
    mac = macFromLinuxNeighborTable(address);
#endif

    NetworkDevice device;
    device.ip = address;
    device.mac = mac;
    device.typeOrVendor = vendorForMac(mac);
    device.confidence = mac.isEmpty() ? QStringLiteral("низкая") : QStringLiteral("средняя");
    device.identificationEvidence = {evidence};
    if (!mac.isEmpty()) device.identificationEvidence.push_back(QStringLiteral("таблица соседей ОС"));
    device.latencyMs = *latency;
    if (openPort != 0) device.openPorts.push_back(openPort);
    return device;
}

} // namespace orion::app
