#include "network_identity.h"

#include <QCoreApplication>
#include <QtEndian>

#include <cstdlib>
#include <iostream>

namespace {

void append16(QByteArray& bytes, const quint16 value)
{
    const quint16 bigEndian = qToBigEndian(value);
    bytes.append(reinterpret_cast<const char*>(&bigEndian), sizeof(bigEndian));
}

void append32(QByteArray& bytes, const quint32 value)
{
    const quint32 bigEndian = qToBigEndian(value);
    bytes.append(reinterpret_cast<const char*>(&bigEndian), sizeof(bigEndian));
}

[[nodiscard]] QByteArray dnsName(const QString& name)
{
    QByteArray encoded;
    for (const QString& label : name.split(QLatin1Char('.'))) {
        const QByteArray value = label.toUtf8();
        encoded.append(static_cast<char>(value.size()));
        encoded.append(value);
    }
    encoded.append('\0');
    return encoded;
}

[[nodiscard]] QByteArray ptrFixture()
{
    QByteArray packet;
    append16(packet, 0x1234);
    append16(packet, 0x8180);
    append16(packet, 1);
    append16(packet, 1);
    append16(packet, 0);
    append16(packet, 0);
    packet += dnsName(QStringLiteral("9.0.50.168.192.in-addr.arpa"));
    append16(packet, 12);
    append16(packet, 1);
    append16(packet, 0xC00C);
    append16(packet, 12);
    append16(packet, 1);
    append32(packet, 120);
    const QByteArray answer = dnsName(QStringLiteral("fixture-pc.local"));
    append16(packet, static_cast<quint16>(answer.size()));
    packet += answer;
    return packet;
}

void appendNetBiosRow(
    QByteArray& data, const QByteArray& rawName, const quint8 suffix, const quint16 flags)
{
    data += rawName.leftJustified(15, ' ', true);
    data.append(static_cast<char>(suffix));
    append16(data, flags);
}

[[nodiscard]] QByteArray netBiosFixture()
{
    QByteArray packet;
    append16(packet, 0x4F52);
    append16(packet, 0x8500);
    append16(packet, 0);
    append16(packet, 1);
    append16(packet, 0);
    append16(packet, 0);
    packet.append('\0');
    append16(packet, 0x21);
    append16(packet, 1);
    append32(packet, 0);
    QByteArray rows;
    rows.append(static_cast<char>(2));
    appendNetBiosRow(rows, QByteArrayLiteral("WORKGROUP"), 0x00, 0x8000);
    appendNetBiosRow(rows, QByteArrayLiteral("ORION-PC"), 0x20, 0x0000);
    append16(packet, static_cast<quint16>(rows.size()));
    packet += rows;
    return packet;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    using orion::app::NetworkIdentity;
    using orion::app::NetworkIdentityInput;
    using orion::app::NetworkIdentityMetadata;

    const auto ssdp = NetworkIdentity::parseSsdpResponse(QByteArrayLiteral(
        "HTTP/1.1 200 OK\r\nLOCATION: http://192.168.50.1/root.xml\r\n"
        "Server: RouterOS UPnP/1.1\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
        "server: ignored duplicate\r\n\r\n"));
    if (ssdp.value(QStringLiteral("status")) != QStringLiteral("HTTP/1.1 200 OK")
        || ssdp.value(QStringLiteral("location"))
            != QStringLiteral("http://192.168.50.1/root.xml")
        || ssdp.value(QStringLiteral("server")) != QStringLiteral("RouterOS UPnP/1.1")) {
        std::cerr << "SSDP header parsing is not deterministic.\n";
        return EXIT_FAILURE;
    }

    const QStringList ptrNames = NetworkIdentity::parseDnsPtrNames(ptrFixture());
    if (ptrNames != QStringList {QStringLiteral("fixture-pc.local")}
        || !NetworkIdentity::parseDnsPtrNames(QByteArrayLiteral("bad")).isEmpty()) {
        std::cerr << "Bounded DNS PTR parsing rejected the fixture.\n";
        return EXIT_FAILURE;
    }
    if (NetworkIdentity::parseNetBiosNodeStatus(netBiosFixture())
        != QStringLiteral("ORION-PC")) {
        std::cerr << "NetBIOS node-status parsing rejected the fixture.\n";
        return EXIT_FAILURE;
    }

    const auto fingerprint = NetworkIdentity::parseHttpFingerprint(QByteArrayLiteral(
        "HTTP/1.1 200 OK\r\nServer: Synology DSM\r\nContent-Type: text/html\r\n\r\n"
        "<html><head><title>DiskStation &amp; Files</title></head></html>"), 5000);
    const auto genericFingerprint = NetworkIdentity::parseHttpFingerprint(QByteArrayLiteral(
        "HTTP/1.1 200 OK\r\nServer: nginx\r\n\r\n<title>Login</title>"), 80);
    if (fingerprint.httpServer != QStringLiteral("Synology DSM")
        || fingerprint.httpTitle != QStringLiteral("DiskStation & Files")
        || fingerprint.httpPort != 5000 || !genericFingerprint.httpTitle.isEmpty()
        || genericFingerprint.httpServer != QStringLiteral("nginx")) {
        std::cerr << "HTTP fingerprint parsing is incomplete.\n";
        return EXIT_FAILURE;
    }

    const auto upnp = NetworkIdentity::parseUpnpDescription(QByteArrayLiteral(
        "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device>"
        "<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>"
        "<friendlyName>ORION Router</friendlyName><manufacturer>Keenetic</manufacturer>"
        "<modelName>Ultra</modelName></device></root>"));
    if (upnp.friendlyName != QStringLiteral("ORION Router")
        || upnp.manufacturer != QStringLiteral("Keenetic")
        || upnp.modelName != QStringLiteral("Ultra")
        || !upnp.upnpDeviceType.contains(QStringLiteral("InternetGatewayDevice"))) {
        std::cerr << "UPnP XML parsing is incomplete.\n";
        return EXIT_FAILURE;
    }

    NetworkIdentityInput routerInput;
    routerInput.ip = QStringLiteral("192.168.50.1");
    routerInput.gateway = true;
    routerInput.vendor = QStringLiteral("Неизвестное устройство");
    const auto router = NetworkIdentity::classify(routerInput, upnp, {53, 80});
    if (router.name != QStringLiteral("ORION Router")
        || router.deviceType != QStringLiteral("Роутер / шлюз")
        || router.confidence != QStringLiteral("высокая")
        || !router.services.contains(QStringLiteral("DNS"))) {
        std::cerr << "Router classification does not preserve explainable evidence.\n";
        return EXIT_FAILURE;
    }

    NetworkIdentityInput windowsInput;
    windowsInput.ip = QStringLiteral("192.168.50.20");
    windowsInput.vendor = QStringLiteral("Intel");
    const auto windows = NetworkIdentity::classify(windowsInput, {}, {445, 3389},
        QStringLiteral("DESKTOP-ORION"));
    if (windows.name != QStringLiteral("DESKTOP-ORION")
        || windows.nameSource != QStringLiteral("NetBIOS")
        || windows.deviceType != QStringLiteral("Windows-ПК")
        || windows.confidence != QStringLiteral("высокая")
        || !windows.evidence.join(QLatin1Char(' ')).contains(QStringLiteral("RDP"))) {
        std::cerr << "Windows service/name classification is incomplete.\n";
        return EXIT_FAILURE;
    }

    NetworkIdentityInput printerInput;
    printerInput.ip = QStringLiteral("192.168.50.30");
    printerInput.vendor = QStringLiteral("Canon");
    const auto printer = NetworkIdentity::classify(printerInput, {}, {9100});
    if (printer.deviceType != QStringLiteral("Принтер / МФУ")
        || !printer.services.contains(QStringLiteral("JetDirect"))
        || NetworkIdentity::maximumIdentityHosts != 64) {
        std::cerr << "Service classification or the strict identity limit changed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Native SSDP, DNS, NetBIOS, HTTP, UPnP and identity fixtures passed.\n";
    return EXIT_SUCCESS;
}
