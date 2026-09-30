#include "network_identity.h"

#include "network_scanner_worker.h"

#include <QAbstractSocket>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTextStream>
#include <QUdpSocket>
#include <QUrl>
#include <QXmlStreamReader>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <memory>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#endif

namespace orion::app {
namespace {

constexpr std::array<quint16, 19> servicePorts {
    22, 53, 80, 81, 139, 443, 445, 554, 631, 1883, 3389,
    5000, 5001, 8008, 8009, 8080, 8443, 9100, 32400,
};
constexpr std::array<quint16, 9> httpPorts {
    80, 81, 443, 5000, 5001, 8080, 8443, 32400, 631,
};

[[nodiscard]] QString cleanText(QString value, const int maximum = 160)
{
    value.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")), QStringLiteral(" "));
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"), Qt::CaseInsensitive);
    value.replace(QStringLiteral("&quot;"), QStringLiteral("\""), Qt::CaseInsensitive);
    value.replace(QStringLiteral("&#39;"), QStringLiteral("'"), Qt::CaseInsensitive);
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<"), Qt::CaseInsensitive);
    value.replace(QStringLiteral("&gt;"), QStringLiteral(">"), Qt::CaseInsensitive);
    value.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    value = value.simplified();
    if (value.size() > maximum) value.truncate(maximum);
    return value;
}

[[nodiscard]] QString cleanHostname(QString value)
{
    value = cleanText(std::move(value), 120);
    while (value.endsWith(QLatin1Char('.'))) value.chop(1);
    const QString lowered = value.toLower();
    static const QStringList rejected {
        QStringLiteral("unknown"), QStringLiteral("localhost"),
        QStringLiteral("localhost.localdomain"), QStringLiteral("none"),
        QStringLiteral("n/a"), QStringLiteral("not available"),
    };
    if (value.isEmpty() || rejected.contains(lowered)
        || QHostAddress(value).protocol() != QAbstractSocket::UnknownNetworkLayerProtocol) {
        return {};
    }
    static const QRegularExpression valid(
        QStringLiteral("^[\\p{L}\\p{N}_][\\p{L}\\p{N}_.-]{0,119}$"));
    return valid.match(value).hasMatch() ? value : QString();
}

[[nodiscard]] quint16 read16(const QByteArray& bytes, const int offset)
{
    return qFromBigEndian<quint16>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

void append16(QByteArray& bytes, const quint16 value)
{
    const quint16 bigEndian = qToBigEndian(value);
    bytes.append(reinterpret_cast<const char*>(&bigEndian), sizeof(bigEndian));
}

[[nodiscard]] QByteArray encodeDnsName(const QString& name)
{
    QByteArray encoded;
    const QStringList labels = name.chopped(name.endsWith(QLatin1Char('.')) ? 1 : 0)
                                   .split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString& label : labels) {
        const QByteArray raw = label.toUtf8().left(63);
        encoded.append(static_cast<char>(raw.size()));
        encoded.append(raw);
    }
    encoded.append('\0');
    return encoded;
}

[[nodiscard]] bool decodeDnsName(
    const QByteArray& packet, int& offset, QString& decoded)
{
    QStringList labels;
    int cursor = offset;
    int finalOffset = -1;
    QVector<int> visited;
    for (int steps = 0; steps < 128; ++steps) {
        if (cursor < 0 || cursor >= packet.size() || visited.contains(cursor)) return false;
        visited.push_back(cursor);
        const quint8 length = static_cast<quint8>(packet.at(cursor));
        if (length == 0) {
            if (finalOffset < 0) finalOffset = cursor + 1;
            offset = finalOffset;
            decoded = labels.join(QLatin1Char('.'));
            return true;
        }
        if ((length & 0xC0u) == 0xC0u) {
            if (cursor + 1 >= packet.size()) return false;
            const int pointer = ((length & 0x3Fu) << 8)
                | static_cast<quint8>(packet.at(cursor + 1));
            if (finalOffset < 0) finalOffset = cursor + 2;
            cursor = pointer;
            continue;
        }
        if ((length & 0xC0u) != 0 || cursor + 1 + length > packet.size()) return false;
        ++cursor;
        labels.push_back(QString::fromUtf8(packet.constData() + cursor, length));
        cursor += length;
    }
    return false;
}

[[nodiscard]] bool inSelectedNetwork(
    const QString& address, const NetworkInterfaceInfo& interfaceInfo)
{
    bool addressOk = false;
    bool localOk = false;
    const quint32 candidate = QHostAddress(address).toIPv4Address(&addressOk);
    const quint32 local = QHostAddress(interfaceInfo.address).toIPv4Address(&localOk);
    if (!addressOk || !localOk || interfaceInfo.prefixLength <= 0
        || interfaceInfo.prefixLength > 32) {
        return false;
    }
    const quint32 mask = interfaceInfo.prefixLength == 32
        ? 0xFFFFFFFFu : 0xFFFFFFFFu << (32 - interfaceInfo.prefixLength);
    return (candidate & mask) == (local & mask);
}

void mergeMetadata(NetworkIdentityMetadata& target, const NetworkIdentityMetadata& source)
{
    const auto assign = [](QString& destination, const QString& value) {
        if (destination.isEmpty() && !value.isEmpty()) destination = value;
    };
    assign(target.location, source.location);
    assign(target.ssdpServer, source.ssdpServer);
    assign(target.ssdpType, source.ssdpType);
    assign(target.ssdpUsn, source.ssdpUsn);
    assign(target.friendlyName, source.friendlyName);
    assign(target.manufacturer, source.manufacturer);
    assign(target.modelName, source.modelName);
    assign(target.modelDescription, source.modelDescription);
    assign(target.upnpDeviceType, source.upnpDeviceType);
    assign(target.httpTitle, source.httpTitle);
    assign(target.httpServer, source.httpServer);
    if (target.httpPort == 0) target.httpPort = source.httpPort;
}

[[nodiscard]] QByteArray dnsQuery(
    const QString& name, const quint16 type, const quint16 queryClass)
{
    QByteArray packet;
    const quint16 transaction = static_cast<quint16>(
        qHash(name) ^ static_cast<uint>(QElapsedTimer::clockType()));
    append16(packet, transaction);
    append16(packet, 0);
    append16(packet, 1);
    append16(packet, 0);
    append16(packet, 0);
    append16(packet, 0);
    packet += encodeDnsName(name);
    append16(packet, type);
    append16(packet, queryClass);
    return packet;
}

[[nodiscard]] QString sendPtrQuery(
    const QString& target,
    const quint16 port,
    const QString& source,
    const QString& reverseName,
    const quint16 queryClass,
    const int timeoutMs)
{
    QUdpSocket socket;
    if (!source.isEmpty()
        && !socket.bind(QHostAddress(source), 0,
            QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint)) {
        return {};
    }
    if (socket.writeDatagram(dnsQuery(reverseName, 12, queryClass),
            QHostAddress(target), port) < 0) {
        return {};
    }
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        const int wait = static_cast<int>(std::min<qint64>(80, deadline.remainingTime()));
        if (wait <= 0 || !socket.waitForReadyRead(wait)) continue;
        while (socket.hasPendingDatagrams()) {
            QHostAddress sender;
            quint16 senderPort = 0;
            QByteArray response;
            response.resize(static_cast<int>(std::min<qint64>(8192, socket.pendingDatagramSize())));
            const qint64 received = socket.readDatagram(response.data(), response.size(),
                &sender, &senderPort);
            if (received <= 0 || sender != QHostAddress(target)) continue;
            response.resize(static_cast<int>(received));
            const QStringList names = NetworkIdentity::parseDnsPtrNames(response);
            if (!names.isEmpty()) return names.first();
        }
    }
    return {};
}

[[nodiscard]] QString ipv4ReverseName(const QString& address)
{
    bool ok = false;
    const quint32 value = QHostAddress(address).toIPv4Address(&ok);
    if (!ok) return {};
    return QStringLiteral("%1.%2.%3.%4.in-addr.arpa")
        .arg(value & 0xFFu)
        .arg((value >> 8) & 0xFFu)
        .arg((value >> 16) & 0xFFu)
        .arg((value >> 24) & 0xFFu);
}

[[nodiscard]] QString queryMdnsName(
    const QString& address, const QString& source, const int timeoutMs)
{
    const QString reverseName = ipv4ReverseName(address);
    return reverseName.isEmpty() ? QString()
        : sendPtrQuery(address, 5353, source, reverseName, 0x8001u, timeoutMs);
}

[[nodiscard]] QByteArray netbiosQuery()
{
    QByteArray packet;
    append16(packet, 0x4F52u);
    append16(packet, 0);
    append16(packet, 1);
    append16(packet, 0);
    append16(packet, 0);
    append16(packet, 0);
    QByteArray raw(15, ' ');
    raw[0] = '*';
    raw.append('\0');
    packet.append(static_cast<char>(32));
    for (const unsigned char value : raw) {
        packet.append(static_cast<char>('A' + ((value >> 4) & 0x0F)));
        packet.append(static_cast<char>('A' + (value & 0x0F)));
    }
    packet.append('\0');
    append16(packet, 0x21u);
    append16(packet, 1);
    return packet;
}

[[nodiscard]] QString queryNetBiosName(
    const QString& address, const QString& source, const int timeoutMs)
{
    QUdpSocket socket;
    if (!source.isEmpty()
        && !socket.bind(QHostAddress(source), 0,
            QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint)) {
        return {};
    }
    if (socket.writeDatagram(netbiosQuery(), QHostAddress(address), 137) < 0) return {};
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        const int wait = static_cast<int>(std::min<qint64>(80, deadline.remainingTime()));
        if (wait <= 0 || !socket.waitForReadyRead(wait)) continue;
        while (socket.hasPendingDatagrams()) {
            QHostAddress sender;
            QByteArray response;
            response.resize(static_cast<int>(std::min<qint64>(8192, socket.pendingDatagramSize())));
            const qint64 received = socket.readDatagram(response.data(), response.size(), &sender);
            if (received <= 0 || sender != QHostAddress(address)) continue;
            response.resize(static_cast<int>(received));
            const QString name = NetworkIdentity::parseNetBiosNodeStatus(response);
            if (!name.isEmpty()) return name;
        }
    }
    return {};
}

[[nodiscard]] QStringList dnsServers()
{
    QStringList result;
#ifdef Q_OS_WIN
    ULONG size = 0;
    if (GetNetworkParams(nullptr, &size) != ERROR_BUFFER_OVERFLOW || size == 0) return result;
    QByteArray storage(static_cast<int>(size), Qt::Uninitialized);
    auto* info = reinterpret_cast<FIXED_INFO*>(storage.data());
    if (GetNetworkParams(info, &size) != NO_ERROR) return result;
    for (IP_ADDR_STRING* entry = &info->DnsServerList; entry != nullptr; entry = entry->Next) {
        const QString value = QString::fromLatin1(entry->IpAddress.String).trimmed();
        if (QHostAddress(value).protocol() == QAbstractSocket::IPv4Protocol
            && !result.contains(value)) {
            result.push_back(value);
        }
    }
#else
    QFile file(QStringLiteral("/etc/resolv.conf"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (!line.startsWith(QStringLiteral("nameserver "))) continue;
            const QString value = line.simplified().section(QLatin1Char(' '), 1, 1);
            if (QHostAddress(value).protocol() == QAbstractSocket::IPv4Protocol
                && !result.contains(value)) {
                result.push_back(value);
            }
        }
    }
#endif
    return result;
}

[[nodiscard]] QString queryReverseDnsName(
    const QString& address, const QString& source, const int timeoutMs)
{
    const QString reverseName = ipv4ReverseName(address);
    if (reverseName.isEmpty()) return {};
    const QStringList servers = dnsServers();
    if (servers.isEmpty()) return {};
    return sendPtrQuery(servers.first(), 53, source, reverseName, 1, timeoutMs);
}

[[nodiscard]] QVector<quint16> probeServices(
    const QString& address,
    const QString& source,
    QVector<quint16> known,
    const std::atomic_bool& stopRequested,
    const int totalTimeoutMs)
{
    std::sort(known.begin(), known.end());
    known.erase(std::unique(known.begin(), known.end()), known.end());
    int remainingCount = 0;
    for (const quint16 port : servicePorts) {
        if (!known.contains(port)) ++remainingCount;
    }
    const int perPort = std::max(25, totalTimeoutMs / std::max(1, remainingCount));
    for (const quint16 port : servicePorts) {
        if (known.contains(port) || stopRequested.load(std::memory_order_relaxed)) continue;
        QTcpSocket socket;
        if (!source.isEmpty()) {
            socket.bind(QHostAddress(source), 0,
                QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint);
        }
        socket.connectToHost(address, port);
        if (socket.waitForConnected(perPort)) known.push_back(port);
        socket.abort();
    }
    std::sort(known.begin(), known.end());
    known.erase(std::unique(known.begin(), known.end()), known.end());
    return known;
}

[[nodiscard]] QByteArray httpGet(
    const QString& address,
    const QString& source,
    const quint16 port,
    const QString& path,
    const QString& hostHeader,
    const bool secure,
    const int timeoutMs,
    const int maximumBytes,
    const std::atomic_bool& stopRequested)
{
    std::unique_ptr<QTcpSocket> socket;
    if (secure) socket = std::make_unique<QSslSocket>();
    else socket = std::make_unique<QTcpSocket>();
    if (!source.isEmpty()) {
        socket->bind(QHostAddress(source), 0,
            QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint);
    }
    if (secure) {
        auto* ssl = static_cast<QSslSocket*>(socket.get());
        ssl->connectToHostEncrypted(address, port);
        ssl->ignoreSslErrors();
        if (!ssl->waitForEncrypted(timeoutMs)) return {};
    } else {
        socket->connectToHost(address, port);
        if (!socket->waitForConnected(timeoutMs)) return {};
    }
    if (stopRequested.load(std::memory_order_relaxed)) return {};
    const QByteArray request = QByteArrayLiteral("GET ") + path.toUtf8()
        + QByteArrayLiteral(" HTTP/1.1\r\nHost: ") + hostHeader.toUtf8()
        + QByteArrayLiteral("\r\nUser-Agent: O.R.I.O.N./3.3\r\nAccept: text/html,application/xhtml+xml,*/*;q=0.2\r\nConnection: close\r\n\r\n");
    if (socket->write(request) != request.size() || !socket->waitForBytesWritten(timeoutMs)) {
        return {};
    }
    QByteArray response;
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired() && response.size() < maximumBytes
        && !stopRequested.load(std::memory_order_relaxed)) {
        if (socket->bytesAvailable() == 0) {
            const int wait = static_cast<int>(std::min<qint64>(80, deadline.remainingTime()));
            if (wait <= 0 || !socket->waitForReadyRead(wait)) {
                if (socket->state() == QAbstractSocket::UnconnectedState) break;
                continue;
            }
        }
        response += socket->read(std::min<qint64>(maximumBytes - response.size(),
            socket->bytesAvailable()));
    }
    socket->abort();
    return response;
}

[[nodiscard]] QByteArray responseBody(const QByteArray& response)
{
    int separator = response.indexOf("\r\n\r\n");
    int width = 4;
    if (separator < 0) {
        separator = response.indexOf("\n\n");
        width = 2;
    }
    return separator < 0 ? QByteArray() : response.mid(separator + width);
}

[[nodiscard]] NetworkIdentityMetadata fetchUpnpDescription(
    const QString& address,
    const QString& source,
    const QString& location,
    const std::atomic_bool& stopRequested)
{
    if (location.isEmpty()) return {};
    const QUrl url(location);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QStringLiteral("http")
            && scheme != QStringLiteral("https"))
        || QHostAddress(url.host()) != QHostAddress(address)) {
        return {};
    }
    const bool secure = scheme == QStringLiteral("https");
    const quint16 port = static_cast<quint16>(url.port(secure ? 443 : 80));
    QString path = url.path(QUrl::FullyEncoded);
    if (path.isEmpty()) path = QStringLiteral("/");
    if (url.hasQuery()) path += QLatin1Char('?') + url.query(QUrl::FullyEncoded);
    QString host = address;
    if (port != (secure ? 443 : 80)) host += QStringLiteral(":%1").arg(port);
    return NetworkIdentity::parseUpnpDescription(responseBody(httpGet(
        address, source, port, path, host, secure, 600, 65536, stopRequested)));
}

[[nodiscard]] NetworkIdentityMetadata fingerprintHttp(
    const QString& address,
    const QString& source,
    const QVector<quint16>& openPorts,
    const std::atomic_bool& stopRequested)
{
    for (const quint16 port : httpPorts) {
        if (!openPorts.contains(port) || stopRequested.load(std::memory_order_relaxed)) continue;
        const bool secure = port == 443 || port == 5001 || port == 8443;
        const QByteArray response = httpGet(address, source, port, QStringLiteral("/"),
            address, secure, 450, 32768, stopRequested);
        const NetworkIdentityMetadata parsed = NetworkIdentity::parseHttpFingerprint(response, port);
        if (!parsed.httpTitle.isEmpty() || !parsed.httpServer.isEmpty()) return parsed;
    }
    return {};
}

[[nodiscard]] QString serviceLabel(const quint16 port)
{
    static const QHash<quint16, QString> labels {
        {22, QStringLiteral("SSH")}, {53, QStringLiteral("DNS")},
        {80, QStringLiteral("HTTP")}, {81, QStringLiteral("HTTP")},
        {139, QStringLiteral("NetBIOS")}, {443, QStringLiteral("HTTPS")},
        {445, QStringLiteral("SMB")}, {554, QStringLiteral("RTSP")},
        {631, QStringLiteral("IPP")}, {1883, QStringLiteral("MQTT")},
        {3389, QStringLiteral("RDP")}, {5000, QStringLiteral("Web/NAS")},
        {5001, QStringLiteral("Web/NAS TLS")}, {8008, QStringLiteral("Google Cast")},
        {8009, QStringLiteral("Google Cast")}, {8080, QStringLiteral("HTTP-alt")},
        {8443, QStringLiteral("HTTPS-alt")}, {9100, QStringLiteral("JetDirect")},
        {32400, QStringLiteral("Plex")},
    };
    return labels.value(port, QStringLiteral("TCP %1").arg(port));
}

void appendUnique(QStringList& values, const QString& value)
{
    if (!value.isEmpty() && !values.contains(value)) values.push_back(value);
}

} // namespace

QHash<QString, QString> NetworkIdentity::parseSsdpResponse(const QByteArray& payload)
{
    QHash<QString, QString> headers;
    const QString normalized = QString::fromUtf8(payload).replace(QStringLiteral("\r\n"),
        QStringLiteral("\n"));
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    if (!lines.isEmpty()) headers.insert(QStringLiteral("status"), lines.first().trimmed());
    for (qsizetype index = 1; index < lines.size(); ++index) {
        const QString& line = lines.at(index);
        const qsizetype separator = line.indexOf(QLatin1Char(':'));
        if (separator <= 0) continue;
        const QString key = line.left(separator).trimmed().toLower();
        const QString value = cleanText(line.mid(separator + 1), 500);
        if (!key.isEmpty() && !value.isEmpty() && !headers.contains(key)) {
            headers.insert(key, value);
        }
    }
    return headers;
}

QStringList NetworkIdentity::parseDnsPtrNames(const QByteArray& packet)
{
    QStringList names;
    if (packet.size() < 12) return names;
    const int questionCount = read16(packet, 4);
    const int recordCount = read16(packet, 6) + read16(packet, 8) + read16(packet, 10);
    int offset = 12;
    for (int question = 0; question < questionCount; ++question) {
        QString ignored;
        if (!decodeDnsName(packet, offset, ignored) || offset + 4 > packet.size()) return {};
        offset += 4;
    }
    for (int record = 0; record < recordCount; ++record) {
        QString owner;
        if (!decodeDnsName(packet, offset, owner) || offset + 10 > packet.size()) break;
        const quint16 type = read16(packet, offset);
        const quint16 length = read16(packet, offset + 8);
        offset += 10;
        const int dataOffset = offset;
        offset += length;
        if (offset > packet.size()) break;
        if (type != 12) continue;
        int nameOffset = dataOffset;
        QString name;
        if (decodeDnsName(packet, nameOffset, name)) {
            name = cleanHostname(name);
            appendUnique(names, name);
        }
    }
    return names;
}

QString NetworkIdentity::parseNetBiosNodeStatus(const QByteArray& packet)
{
    if (packet.size() < 12) return {};
    const int questionCount = read16(packet, 4);
    const int answerCount = read16(packet, 6);
    int offset = 12;
    for (int question = 0; question < questionCount; ++question) {
        QString ignored;
        if (!decodeDnsName(packet, offset, ignored) || offset + 4 > packet.size()) return {};
        offset += 4;
    }
    QVector<QPair<int, QString>> candidates;
    for (int answer = 0; answer < answerCount; ++answer) {
        QString ignored;
        if (!decodeDnsName(packet, offset, ignored) || offset + 10 > packet.size()) break;
        const quint16 type = read16(packet, offset);
        const quint16 length = read16(packet, offset + 8);
        offset += 10;
        const int end = offset + length;
        if (end > packet.size()) break;
        if (type == 0x21 && length >= 1) {
            const int count = static_cast<quint8>(packet.at(offset));
            int cursor = offset + 1;
            for (int row = 0; row < count && cursor + 18 <= end; ++row, cursor += 18) {
                const QString name = QString::fromLatin1(packet.constData() + cursor, 15).trimmed();
                const quint8 suffix = static_cast<quint8>(packet.at(cursor + 15));
                const quint16 flags = read16(packet, cursor + 16);
                if (name.isEmpty() || name == QStringLiteral("__MSBROWSE__")
                    || (flags & 0x8000u) != 0) {
                    continue;
                }
                if (suffix == 0x00) candidates.push_back({0, name});
                else if (suffix == 0x20) candidates.push_back({1, name});
            }
        }
        offset = end;
    }
    if (candidates.isEmpty()) return {};
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    return cleanHostname(candidates.first().second);
}

NetworkIdentityMetadata NetworkIdentity::parseHttpFingerprint(
    const QByteArray& response, const quint16 port)
{
    NetworkIdentityMetadata result;
    if (response.isEmpty()) return result;
    int separator = response.indexOf("\r\n\r\n");
    int width = 4;
    if (separator < 0) {
        separator = response.indexOf("\n\n");
        width = 2;
    }
    if (separator < 0) return result;
    const QStringList headers = QString::fromUtf8(response.left(separator))
                                    .replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
                                    .split(QLatin1Char('\n'));
    for (const QString& line : headers) {
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0 && line.left(colon).trimmed().compare(
                QStringLiteral("server"), Qt::CaseInsensitive) == 0) {
            result.httpServer = cleanText(line.mid(colon + 1), 120);
            break;
        }
    }
    const QString body = QString::fromUtf8(response.mid(separator + width));
    static const QRegularExpression titlePattern(
        QStringLiteral("<title[^>]*>(.*?)</title>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const auto match = titlePattern.match(body);
    if (match.hasMatch()) {
        const QString title = cleanText(match.captured(1), 120);
        static const QStringList generic {
            QStringLiteral("index of /"), QStringLiteral("welcome"),
            QStringLiteral("home"), QStringLiteral("login"), QStringLiteral("log in"),
            QStringLiteral("sign in"), QStringLiteral("admin"),
            QStringLiteral("administrator"), QStringLiteral("authentication"),
            QStringLiteral("web interface"),
        };
        if (!generic.contains(title.toLower())) result.httpTitle = title;
    }
    if (!result.httpTitle.isEmpty() || !result.httpServer.isEmpty()) result.httpPort = port;
    return result;
}

NetworkIdentityMetadata NetworkIdentity::parseUpnpDescription(const QByteArray& xml)
{
    NetworkIdentityMetadata result;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) continue;
        const QString name = reader.name().toString();
        if (name != QStringLiteral("friendlyName") && name != QStringLiteral("manufacturer")
            && name != QStringLiteral("modelName") && name != QStringLiteral("modelDescription")
            && name != QStringLiteral("deviceType")) {
            continue;
        }
        const QString value = cleanText(reader.readElementText(QXmlStreamReader::SkipChildElements));
        if (name == QStringLiteral("friendlyName")) result.friendlyName = value;
        else if (name == QStringLiteral("manufacturer")) result.manufacturer = value;
        else if (name == QStringLiteral("modelName")) result.modelName = value;
        else if (name == QStringLiteral("modelDescription")) result.modelDescription = value;
        else if (name == QStringLiteral("deviceType")) result.upnpDeviceType = value;
    }
    return reader.hasError() ? NetworkIdentityMetadata() : result;
}

NetworkIdentityOutput NetworkIdentity::classify(
    const NetworkIdentityInput& input,
    const NetworkIdentityMetadata& metadata,
    const QVector<quint16>& openPorts,
    const QString& netbiosName,
    const QString& mdnsName,
    const QString& reverseDnsName)
{
    NetworkIdentityOutput result;
    result.openPorts = openPorts;
    std::sort(result.openPorts.begin(), result.openPorts.end());
    result.openPorts.erase(std::unique(result.openPorts.begin(), result.openPorts.end()),
        result.openPorts.end());
    for (const quint16 port : result.openPorts) result.services.push_back(serviceLabel(port));

    const QString ouiVendor = input.vendor.isEmpty()
        ? QStringLiteral("Неизвестное устройство") : input.vendor;
    result.vendor = metadata.manufacturer.isEmpty() ? ouiVendor : metadata.manufacturer;
    result.model = cleanText(metadata.modelName.isEmpty()
        ? metadata.modelDescription : metadata.modelName);

    const QString friendlyName = cleanText(metadata.friendlyName);
    const QString httpTitle = cleanText(metadata.httpTitle);
    if (input.local) {
        result.name = QStringLiteral("Этот компьютер");
        result.nameSource = QStringLiteral("имя локального компьютера");
    } else if (!friendlyName.isEmpty()) {
        result.name = friendlyName;
        result.nameSource = QStringLiteral("UPnP/SSDP friendlyName");
    } else if (!cleanHostname(netbiosName).isEmpty()) {
        result.name = cleanHostname(netbiosName);
        result.nameSource = QStringLiteral("NetBIOS");
    } else if (!cleanHostname(mdnsName).isEmpty()) {
        result.name = cleanHostname(mdnsName);
        result.nameSource = QStringLiteral("mDNS");
    } else if (!cleanHostname(reverseDnsName).isEmpty()) {
        result.name = cleanHostname(reverseDnsName);
        result.nameSource = QStringLiteral("обратный DNS");
    } else if (!httpTitle.isEmpty()) {
        result.name = httpTitle;
        result.nameSource = QStringLiteral("заголовок веб-интерфейса");
    } else if (!result.model.isEmpty()) {
        result.name = result.model;
        result.nameSource = QStringLiteral("модель из UPnP");
    } else if (input.gateway) {
        result.name = QStringLiteral("Шлюз / роутер");
        result.nameSource = QStringLiteral("маршрут выбранного адаптера");
    } else {
        result.name = QStringLiteral("Имя не сообщено");
        result.nameSource = QStringLiteral("имя не удалось получить");
    }

    const QString combined = QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8 %9")
        .arg(metadata.friendlyName, metadata.manufacturer, metadata.modelName,
            metadata.modelDescription, metadata.upnpDeviceType, metadata.httpTitle,
            metadata.httpServer, metadata.ssdpType, metadata.ssdpServer).toLower();
    const auto hasPort = [&result](const quint16 port) { return result.openPorts.contains(port); };
    const auto containsAny = [&combined](const QStringList& needles) {
        for (const QString& needle : needles) if (combined.contains(needle)) return true;
        return false;
    };
    QString typeEvidence;
    if (input.local) {
        result.deviceType = QStringLiteral("Этот компьютер");
        result.confidence = QStringLiteral("высокая");
        typeEvidence = QStringLiteral("адрес выбранного локального адаптера");
    } else if (input.gateway || containsAny({QStringLiteral("internetgatewaydevice"),
                   QStringLiteral("wanconnectiondevice"), QStringLiteral("router")})) {
        result.deviceType = QStringLiteral("Роутер / шлюз");
        result.confidence = QStringLiteral("высокая");
        typeEvidence = QStringLiteral("адрес шлюза или профиль UPnP роутера");
    } else if (hasPort(9100) || containsAny({QStringLiteral("printer"),
                   QStringLiteral("laserjet"), QStringLiteral("officejet"),
                   QStringLiteral("epson"), QStringLiteral("brother"),
                   QStringLiteral("xerox"), QStringLiteral("canon")})) {
        result.deviceType = QStringLiteral("Принтер / МФУ");
        result.confidence = QStringLiteral("высокая");
        typeEvidence = QStringLiteral("JetDirect или явный профиль принтера");
    } else if (hasPort(631)) {
        result.deviceType = QStringLiteral("Принтер / сервер печати");
        result.confidence = QStringLiteral("средняя");
        typeEvidence = QStringLiteral("открыта служба IPP; это может быть принтер или сервер печати");
    } else if (containsAny({QStringLiteral("camera"), QStringLiteral("webcam"),
                   QStringLiteral("nvr"), QStringLiteral("dvr"),
                   QStringLiteral("hikvision"), QStringLiteral("dahua"),
                   QStringLiteral("reolink"), QStringLiteral("axis")})
        || (hasPort(554) && QStringList {QStringLiteral("Hikvision"),
                QStringLiteral("Dahua"), QStringLiteral("Axis"), QStringLiteral("Reolink")}
                .contains(result.vendor))) {
        result.deviceType = QStringLiteral("Камера / видеорегистратор");
        result.confidence = QStringLiteral("высокая");
        typeEvidence = QStringLiteral("RTSP/описание или производитель видеонаблюдения");
    } else if (hasPort(554)) {
        result.deviceType = QStringLiteral("Вероятно камера / медиоустройство");
        result.confidence = QStringLiteral("средняя");
        typeEvidence = QStringLiteral("открыт RTSP, но модель не сообщена");
    } else if (hasPort(8008) || hasPort(8009)
        || containsAny({QStringLiteral("mediarenderer"), QStringLiteral("mediaserver"),
            QStringLiteral("chromecast"), QStringLiteral("google cast"),
            QStringLiteral("roku"), QStringLiteral("smart tv"), QStringLiteral("plex")})) {
        result.deviceType = QStringLiteral("Медиоустройство / медиасервер");
        result.confidence = QStringLiteral("высокая");
        typeEvidence = QStringLiteral("Google Cast/Plex или UPnP MediaRenderer/MediaServer");
    } else if (hasPort(32400)) {
        result.deviceType = QStringLiteral("Медиасервер / Plex");
        result.confidence = QStringLiteral("высокая");
        typeEvidence = QStringLiteral("открыта стандартная служба Plex");
    } else if (containsAny({QStringLiteral("synology"), QStringLiteral("diskstation"),
                   QStringLiteral("qnap"), QStringLiteral("nas "),
                   QStringLiteral("network attached storage")})
        || ((hasPort(5000) || hasPort(5001)) && hasPort(445))) {
        result.deviceType = QStringLiteral("NAS / файловое хранилище");
        result.confidence = QStringLiteral("высокая");
        typeEvidence = QStringLiteral("NAS-панель/описание и файловые службы");
    } else if (result.vendor.contains(QStringLiteral("VMware"), Qt::CaseInsensitive)
        || result.vendor.contains(QStringLiteral("VirtualBox"), Qt::CaseInsensitive)
        || result.vendor.contains(QStringLiteral("Parallels"), Qt::CaseInsensitive)) {
        result.deviceType = QStringLiteral("Виртуальная машина");
        result.confidence = QStringLiteral("высокая");
        typeEvidence = QStringLiteral("OUI виртуальной сетевой карты");
    } else if (hasPort(3389) && (hasPort(139) || hasPort(445) || !netbiosName.isEmpty())) {
        result.deviceType = QStringLiteral("Windows-ПК");
        result.confidence = QStringLiteral("высокая");
        typeEvidence = QStringLiteral("RDP вместе с NetBIOS/SMB");
    } else if (hasPort(3389)) {
        result.deviceType = QStringLiteral("ПК / RDP-хост");
        result.confidence = QStringLiteral("средняя");
        typeEvidence = QStringLiteral("открыт RDP; это может быть Windows или xrdp на Linux");
    } else if (hasPort(139) || hasPort(445) || !netbiosName.isEmpty()) {
        result.deviceType = QStringLiteral("ПК / файловое устройство");
        result.confidence = QStringLiteral("средняя");
        typeEvidence = QStringLiteral("NetBIOS/SMB или имя NetBIOS; возможны Windows, Samba либо NAS");
    } else if (hasPort(1883) || containsAny({QStringLiteral("home assistant"),
                   QStringLiteral("espressif"), QStringLiteral("tuya"),
                   QStringLiteral("tasmota"), QStringLiteral("shelly"),
                   QStringLiteral("mqtt")})) {
        result.deviceType = QStringLiteral("IoT / умный дом");
        result.confidence = QStringLiteral("средняя");
        typeEvidence = QStringLiteral("MQTT или маркеры умного дома");
    } else if (hasPort(53) && (hasPort(80) || hasPort(443)
                   || hasPort(8080) || hasPort(8443))) {
        result.deviceType = QStringLiteral("Сетевое устройство");
        result.confidence = QStringLiteral("средняя");
        typeEvidence = QStringLiteral("DNS и веб-интерфейс");
    } else if (hasPort(22)) {
        result.deviceType = QStringLiteral("Linux/Unix или сетевое устройство");
        result.confidence = QStringLiteral("низкая");
        typeEvidence = QStringLiteral("открыт SSH");
    } else if (result.vendor != QStringLiteral("Неизвестное устройство")
        && result.vendor != QStringLiteral("Неизвестный производитель")
        && result.vendor != QStringLiteral("Приватный/случайный MAC")) {
        result.deviceType = QStringLiteral("Тип не определён");
        result.confidence = QStringLiteral("низкая");
        typeEvidence = QStringLiteral("известен только производитель сетевого интерфейса");
    } else {
        result.deviceType = QStringLiteral("Тип не определён");
        result.confidence = QStringLiteral("низкая");
        typeEvidence = QStringLiteral("устройство не сообщило достаточно признаков");
    }

    appendUnique(result.evidence, typeEvidence);
    if (!result.services.isEmpty()) {
        appendUnique(result.evidence, QStringLiteral("службы: %1").arg(
            result.services.join(QStringLiteral(", "))));
    }
    if (result.name != QStringLiteral("Имя не сообщено")) {
        appendUnique(result.evidence, QStringLiteral("имя получено через %1").arg(result.nameSource));
    }
    if (!metadata.ssdpServer.isEmpty()) {
        appendUnique(result.evidence, QStringLiteral("SSDP: %1").arg(metadata.ssdpServer));
    }
    return result;
}

QHash<QString, NetworkIdentityMetadata> NetworkIdentity::discoverSsdp(
    const NetworkInterfaceInfo& interfaceInfo,
    const std::atomic_bool& stopRequested,
    const int timeoutMs)
{
    QHash<QString, NetworkIdentityMetadata> devices;
    QUdpSocket socket;
    if (!socket.bind(QHostAddress(interfaceInfo.address), 0,
            QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint)) {
        return devices;
    }
    const QNetworkInterface selected = QNetworkInterface::interfaceFromName(interfaceInfo.name);
    if (selected.isValid()) socket.setMulticastInterface(selected);
    socket.setSocketOption(QAbstractSocket::MulticastTtlOption, 2);
    const QByteArray request = QByteArrayLiteral(
        "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ssdp:all\r\n"
        "USER-AGENT: O.R.I.O.N./3.3 UPnP/1.1\r\n\r\n");
    for (int attempt = 0; attempt < 2 && !stopRequested.load(std::memory_order_relaxed);
         ++attempt) {
        if (socket.writeDatagram(request, QHostAddress(QStringLiteral("239.255.255.250")),
                1900) < 0) {
            return devices;
        }
    }
    QDeadlineTimer deadline(std::max(50, timeoutMs));
    while (!deadline.hasExpired() && !stopRequested.load(std::memory_order_relaxed)) {
        const int wait = static_cast<int>(std::min<qint64>(80, deadline.remainingTime()));
        if (wait <= 0 || !socket.waitForReadyRead(wait)) continue;
        while (socket.hasPendingDatagrams()) {
            QHostAddress sender;
            QByteArray payload;
            payload.resize(static_cast<int>(std::min<qint64>(65535,
                socket.pendingDatagramSize())));
            const qint64 received = socket.readDatagram(payload.data(), payload.size(), &sender);
            if (received <= 0) continue;
            const QString address = sender.toString();
            if (address == interfaceInfo.address || !inSelectedNetwork(address, interfaceInfo)) continue;
            payload.resize(static_cast<int>(received));
            const auto headers = parseSsdpResponse(payload);
            NetworkIdentityMetadata incoming;
            incoming.location = cleanText(headers.value(QStringLiteral("location")), 500);
            incoming.ssdpServer = cleanText(headers.value(QStringLiteral("server")), 500);
            incoming.ssdpType = cleanText(headers.value(QStringLiteral("st")), 500);
            if (incoming.ssdpType.isEmpty()) {
                incoming.ssdpType = cleanText(headers.value(QStringLiteral("nt")), 500);
            }
            incoming.ssdpUsn = cleanText(headers.value(QStringLiteral("usn")), 500);
            mergeMetadata(devices[address], incoming);
        }
    }
    return devices;
}

NetworkIdentityOutput NetworkIdentity::identify(
    const NetworkInterfaceInfo& interfaceInfo,
    const NetworkIdentityInput& input,
    const std::atomic_bool& stopRequested)
{
    NetworkIdentityMetadata metadata = input.metadata;
    if (!metadata.location.isEmpty() && !stopRequested.load(std::memory_order_relaxed)) {
        mergeMetadata(metadata, fetchUpnpDescription(input.ip, interfaceInfo.address,
            metadata.location, stopRequested));
    }
    const QVector<quint16> ports = probeServices(input.ip, interfaceInfo.address,
        input.knownOpenPorts, stopRequested, 650);
    if (!stopRequested.load(std::memory_order_relaxed)) {
        mergeMetadata(metadata, fingerprintHttp(input.ip, interfaceInfo.address, ports,
            stopRequested));
    }

    QString netbiosName;
    QString mdnsName;
    QString reverseName;
    if (!input.local && metadata.friendlyName.isEmpty()
        && !stopRequested.load(std::memory_order_relaxed)) {
        netbiosName = queryNetBiosName(input.ip, interfaceInfo.address, 240);
    }
    if (!input.local && metadata.friendlyName.isEmpty() && netbiosName.isEmpty()
        && !stopRequested.load(std::memory_order_relaxed)) {
        mdnsName = queryMdnsName(input.ip, interfaceInfo.address, 240);
    }
    if (!input.local && metadata.friendlyName.isEmpty() && netbiosName.isEmpty()
        && mdnsName.isEmpty() && !stopRequested.load(std::memory_order_relaxed)) {
        reverseName = queryReverseDnsName(input.ip, interfaceInfo.address, 240);
    }
    return classify(input, metadata, ports, netbiosName, mdnsName, reverseName);
}

} // namespace orion::app
