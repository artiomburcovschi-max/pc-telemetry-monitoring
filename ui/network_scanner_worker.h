#pragma once

#include <QMetaType>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <atomic>
#include <functional>
#include <optional>

namespace orion::app {

struct NetworkInterfaceInfo {
    QString name;
    QString displayName;
    QString address;
    QString netmask;
    QString mac;
    QString gateway;
    int prefixLength {0};

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QString label() const;
};

struct NetworkDevice {
    QString ip;
    QString mac;
    QString name;
    QString typeOrVendor;
    QString manufacturer;
    QString model;
    QString nameSource;
    QString confidence;
    QStringList services;
    QStringList identificationEvidence;
    QVector<quint16> openPorts;
    double latencyMs {-1.0};
    bool local {false};
};

struct NetworkScanResult {
    QVector<NetworkDevice> devices;
    QString network;
    QString originalNetwork;
    bool cancelled {false};
    bool truncated {false};
    bool identityLimited {false};
    int identified {0};
    int identityLimit {64};
    qint64 elapsedMs {0};
};

class NetworkScannerWorker final : public QThread {
    Q_OBJECT

public:
    using ProbeFunction = std::function<std::optional<NetworkDevice>(
        const QString& address,
        const NetworkInterfaceInfo& interfaceInfo,
        int timeoutMs,
        const std::atomic_bool& stopRequested)>;

    explicit NetworkScannerWorker(
        QObject* parent = nullptr,
        ProbeFunction probe = {});
    ~NetworkScannerWorker() override;

    [[nodiscard]] static QVector<NetworkInterfaceInfo> listIpv4Interfaces();
    [[nodiscard]] static QStringList scanTargetsFor(
        const NetworkInterfaceInfo& interfaceInfo,
        bool* truncated = nullptr,
        QString* effectiveNetwork = nullptr,
        QString* originalNetwork = nullptr);
    [[nodiscard]] static QString vendorForMac(const QString& mac);

    bool setInterface(const NetworkInterfaceInfo& interfaceInfo);
    void requestStop();

signals:
    void progressChanged(int percent, const QString& status);
    void deviceFound(const orion::app::NetworkDevice& device);
    void scanCompleted(const orion::app::NetworkScanResult& result);
    void scanFailed(const QString& error);

protected:
    void run() override;

private:
    [[nodiscard]] static std::optional<NetworkDevice> probeAddress(
        const QString& address,
        const NetworkInterfaceInfo& interfaceInfo,
        int timeoutMs,
        const std::atomic_bool& stopRequested);

    mutable QMutex configMutex_;
    NetworkInterfaceInfo interfaceInfo_;
    ProbeFunction probe_;
    std::atomic_bool stopRequested_ {false};
};

} // namespace orion::app

Q_DECLARE_METATYPE(orion::app::NetworkDevice)
Q_DECLARE_METATYPE(orion::app::NetworkScanResult)
