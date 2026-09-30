#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>

namespace orion::app {

struct NetworkInterfaceInfo;

struct NetworkIdentityMetadata {
    QString location;
    QString ssdpServer;
    QString ssdpType;
    QString ssdpUsn;
    QString friendlyName;
    QString manufacturer;
    QString modelName;
    QString modelDescription;
    QString upnpDeviceType;
    QString httpTitle;
    QString httpServer;
    quint16 httpPort {0};
};

struct NetworkIdentityInput {
    QString ip;
    QString mac;
    QString vendor;
    bool local {false};
    bool gateway {false};
    QVector<quint16> knownOpenPorts;
    NetworkIdentityMetadata metadata;
};

struct NetworkIdentityOutput {
    QString name;
    QString nameSource;
    QString deviceType;
    QString vendor;
    QString model;
    QString confidence;
    QStringList services;
    QStringList evidence;
    QVector<quint16> openPorts;
};

class NetworkIdentity final {
public:
    static constexpr int maximumIdentityHosts = 64;

    [[nodiscard]] static QHash<QString, QString> parseSsdpResponse(
        const QByteArray& payload);
    [[nodiscard]] static QStringList parseDnsPtrNames(const QByteArray& packet);
    [[nodiscard]] static QString parseNetBiosNodeStatus(const QByteArray& packet);
    [[nodiscard]] static NetworkIdentityMetadata parseHttpFingerprint(
        const QByteArray& response, quint16 port);
    [[nodiscard]] static NetworkIdentityMetadata parseUpnpDescription(
        const QByteArray& xml);
    [[nodiscard]] static NetworkIdentityOutput classify(
        const NetworkIdentityInput& input,
        const NetworkIdentityMetadata& metadata,
        const QVector<quint16>& openPorts,
        const QString& netbiosName = {},
        const QString& mdnsName = {},
        const QString& reverseDnsName = {});
    [[nodiscard]] static QHash<QString, NetworkIdentityMetadata> discoverSsdp(
        const NetworkInterfaceInfo& interfaceInfo,
        const std::atomic_bool& stopRequested,
        int timeoutMs = 550);
    [[nodiscard]] static NetworkIdentityOutput identify(
        const NetworkInterfaceInfo& interfaceInfo,
        const NetworkIdentityInput& input,
        const std::atomic_bool& stopRequested);
};

} // namespace orion::app
