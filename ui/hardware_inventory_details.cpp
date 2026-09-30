#include "hardware_inventory_details.h"

#include <QDir>
#include <QFile>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <dxgi1_2.h>
#include <iphlpapi.h>
#include <netioapi.h>
#endif

namespace orion::app {
namespace {

double jsonNumber(const QJsonValue& value)
{
    if (value.isDouble()) return value.toDouble(-1);
    bool ok = false;
    const auto parsed = value.toString().toDouble(&ok);
    return ok ? parsed : -1;
}

int diskNumber(const QJsonValue& value)
{
    const double parsed = jsonNumber(value);
    return std::isfinite(parsed) && parsed >= 0 && parsed <= std::numeric_limits<int>::max()
        && std::floor(parsed) == parsed ? static_cast<int>(parsed) : -1;
}

#ifdef Q_OS_WIN

class DeviceHandle final {
public:
    explicit DeviceHandle(const QString& path)
        : value_(CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), 0,
              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)) {}
    ~DeviceHandle() { if (valid()) CloseHandle(value_); }
    DeviceHandle(const DeviceHandle&) = delete;
    DeviceHandle& operator=(const DeviceHandle&) = delete;
    bool valid() const { return value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return value_; }
private:
    HANDLE value_;
};

QString partitionStyle(PARTITION_STYLE style)
{
    switch (style) {
    case PARTITION_STYLE_GPT: return QStringLiteral("GPT");
    case PARTITION_STYLE_MBR: return QStringLiteral("MBR");
    case PARTITION_STYLE_RAW: return QStringLiteral("RAW");
    default: return {};
    }
}

QString busName(STORAGE_BUS_TYPE type)
{
    switch (type) {
    case BusTypeNvme: return QStringLiteral("NVMe");
    case BusTypeSata: return QStringLiteral("SATA");
    case BusTypeAta: return QStringLiteral("ATA");
    case BusTypeScsi: return QStringLiteral("SCSI");
    case BusTypeUsb: return QStringLiteral("USB");
    case BusTypeSas: return QStringLiteral("SAS");
    case BusTypeRAID: return QStringLiteral("RAID");
    case BusTypeVirtual: return QStringLiteral("Virtual");
    case BusTypeFileBackedVirtual: return QStringLiteral("File-backed virtual");
    case BusTypeSpaces: return QStringLiteral("Storage Spaces");
    default: return {};
    }
}

QString descriptorString(const QByteArray& buffer, DWORD offset)
{
    if (offset == 0 || offset >= static_cast<DWORD>(buffer.size())) return {};
    const auto end = buffer.indexOf('\0', offset);
    if (end < 0) return {};
    return QString::fromLatin1(buffer.constData() + offset, end - offset).trimmed();
}

QJsonObject physicalDetails(QJsonObject disk)
{
    const int index = diskNumber(disk.value(QStringLiteral("Index")));
    if (index < 0) return disk;
    // Derive the read-only device target from a validated integer, not a supplied path.
    const QString path = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(index);
    disk.insert(QStringLiteral("DeviceID"), path);
    DeviceHandle device(path);
    if (!device.valid()) return disk;

    STORAGE_PROPERTY_QUERY query {};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    STORAGE_DESCRIPTOR_HEADER header {};
    DWORD returned = 0;
    if (DeviceIoControl(device.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
            &header, sizeof(header), &returned, nullptr)
        && returned >= sizeof(header) && header.Size >= sizeof(STORAGE_DEVICE_DESCRIPTOR)
        && header.Size <= 1024 * 1024) {
        QByteArray bytes(static_cast<int>(header.Size), '\0');
        if (DeviceIoControl(device.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                bytes.data(), bytes.size(), &returned, nullptr)
            && returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR)
            && returned <= static_cast<DWORD>(bytes.size())) {
            bytes.resize(returned);
            STORAGE_DEVICE_DESCRIPTOR descriptor {};
            std::memcpy(&descriptor, bytes.constData(), sizeof(descriptor));
            const QString bus = busName(descriptor.BusType);
            if (!bus.isEmpty()) disk.insert(QStringLiteral("bus_type"), bus);
            const QString serial = descriptorString(bytes, descriptor.SerialNumberOffset);
            const QString firmware = descriptorString(bytes, descriptor.ProductRevisionOffset);
            if (!serial.isEmpty()) disk.insert(QStringLiteral("SerialNumber"), serial);
            if (!firmware.isEmpty()) disk.insert(QStringLiteral("FirmwareRevision"), firmware);
            if (descriptor.BusType == BusTypeNvme) {
                disk.insert(QStringLiteral("storage_type"), QStringLiteral("SSD"));
            }
        }
    }
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    DEVICE_SEEK_PENALTY_DESCRIPTOR seek {};
    if (!disk.contains(QStringLiteral("storage_type"))
        && DeviceIoControl(device.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
            &seek, sizeof(seek), &returned, nullptr) && returned >= sizeof(seek)) {
        disk.insert(QStringLiteral("storage_type"), seek.IncursSeekPenalty
            ? QStringLiteral("HDD") : QStringLiteral("SSD"));
    }

    // Drive-layout buffers vary by partition count; retry within a fixed memory cap.
    for (int size = 4096; size <= 1024 * 1024; size *= 2) {
        QByteArray bytes(size, '\0');
        if (DeviceIoControl(device.get(), IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0,
                bytes.data(), size, &returned, nullptr)) {
            if (returned >= offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry)) {
                PARTITION_STYLE style;
                std::memcpy(&style, bytes.constData(), sizeof(style));
                disk.insert(QStringLiteral("partition_style"), partitionStyle(style));
            }
            break;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_MORE_DATA && error != ERROR_INSUFFICIENT_BUFFER) break;
    }
    return disk;
}

QJsonObject volumeDetails(QJsonObject volume)
{
    volume.insert(QStringLiteral("physical_disk_numbers"), QJsonArray());
    volume.insert(QStringLiteral("mapping_quality"), QStringLiteral("unavailable"));
    QString mount = volume.value(QStringLiteral("mountpoint")).toString();
    mount = QDir::toNativeSeparators(mount);
    if (!mount.endsWith(u'\\')) mount += u'\\';
    std::array<wchar_t, MAX_PATH + 1> name {};
    if (!GetVolumeNameForVolumeMountPointW(reinterpret_cast<LPCWSTR>(mount.utf16()),
            name.data(), static_cast<DWORD>(name.size()))) return volume;
    QString devicePath = QString::fromWCharArray(name.data());
    volume.insert(QStringLiteral("volume_id"), devicePath);
    if (devicePath.endsWith(u'\\')) devicePath.chop(1);
    DeviceHandle device(devicePath);
    if (!device.valid()) return volume;
    DWORD returned = 0;
    for (int size = 1024; size <= 1024 * 1024; size *= 2) {
        QByteArray bytes(size, '\0');
        if (DeviceIoControl(device.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                bytes.data(), size, &returned, nullptr)) {
            DWORD count = 0;
            if (returned < offsetof(VOLUME_DISK_EXTENTS, Extents)
                || returned > static_cast<DWORD>(size)) break;
            std::memcpy(&count, bytes.constData(), sizeof(count));
            const auto maximum = (returned - offsetof(VOLUME_DISK_EXTENTS, Extents)) / sizeof(DISK_EXTENT);
            if (count == 0 || count > maximum) break;
            QSet<int> numbers;
            bool valid = true;
            for (DWORD i = 0; i < count; ++i) {
                DISK_EXTENT extent {};
                std::memcpy(&extent, bytes.constData() + offsetof(VOLUME_DISK_EXTENTS, Extents)
                    + i * sizeof(DISK_EXTENT), sizeof(extent));
                if (extent.DiskNumber > static_cast<DWORD>(std::numeric_limits<int>::max())) {
                    valid = false;
                    break;
                }
                numbers.insert(static_cast<int>(extent.DiskNumber));
            }
            if (!valid) break;
            QList<int> ordered = numbers.values();
            std::ranges::sort(ordered);
            QJsonArray ids;
            for (const int id : ordered) ids.append(id);
            volume.insert(QStringLiteral("physical_disk_numbers"), ids);
            volume.insert(QStringLiteral("mapping_quality"), QStringLiteral("valid"));
            volume.insert(QStringLiteral("mapping_source"), QStringLiteral("Windows volume disk extents"));
            break;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_MORE_DATA && error != ERROR_INSUFFICIENT_BUFFER) break;
    }
    PARTITION_INFORMATION_EX partition {};
    if (DeviceIoControl(device.get(), IOCTL_DISK_GET_PARTITION_INFO_EX, nullptr, 0,
            &partition, sizeof(partition), &returned, nullptr)
        && returned >= sizeof(partition) && partition.PartitionNumber > 0) {
        volume.insert(QStringLiteral("partition_number"), static_cast<double>(partition.PartitionNumber));
        volume.insert(QStringLiteral("partition_style"), partitionStyle(partition.PartitionStyle));
    }
    std::array<wchar_t, MAX_PATH + 1> windowsDirectory {};
    const UINT length = GetWindowsDirectoryW(windowsDirectory.data(), windowsDirectory.size());
    if (length > 0 && length < windowsDirectory.size()) {
        volume.insert(QStringLiteral("is_system"),
            QString::fromWCharArray(windowsDirectory.data()).startsWith(mount, Qt::CaseInsensitive));
    }
    return volume;
}

#endif
} // namespace

QJsonValue hardwareLinkMbps(const quint64 bitsPerSecond)
{
    if (bitsPerSecond == 0 || bitsPerSecond == std::numeric_limits<quint64>::max()) {
        return QJsonValue(QJsonValue::Null);
    }
    return static_cast<double>(bitsPerSecond) / 1000000.0;
}

HardwareInventoryResult collectHardwareGpus(const QJsonArray& fallback)
{
#ifdef Q_OS_WIN
    HardwareInventoryResult result;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void**>(&factory)))) {
        return {fallback, true};
    }
    for (UINT index = 0; index < 256; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT enumerated = factory->EnumAdapters1(index, &adapter);
        if (enumerated == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(enumerated) || adapter == nullptr) { result.partial = true; break; }
        DXGI_ADAPTER_DESC1 description {};
        const HRESULT described = adapter->GetDesc1(&description);
        adapter->Release();
        if (FAILED(described)) { result.partial = true; continue; }
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;
        const QString identity = QStringLiteral("dxgi:%1:%2")
            .arg(static_cast<quint32>(description.AdapterLuid.HighPart), 8, 16, QLatin1Char('0'))
            .arg(description.AdapterLuid.LowPart, 8, 16, QLatin1Char('0'));
        result.rows.append(QJsonObject {
            {QStringLiteral("model"), QString::fromWCharArray(description.Description).trimmed()},
            {QStringLiteral("device_id"), identity},
            {QStringLiteral("vendor_id"), static_cast<double>(description.VendorId)},
            {QStringLiteral("memory_mb"), description.DedicatedVideoMemory > 0
                ? QJsonValue(static_cast<double>(description.DedicatedVideoMemory) / (1024 * 1024))
                : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("shared_memory_mb"), static_cast<double>(description.SharedSystemMemory) / (1024 * 1024)},
            {QStringLiteral("source"), QStringLiteral("DXGI adapter inventory")},
        });
        if (index == 255) result.partial = true;
    }
    factory->Release();
    if (result.rows.isEmpty() && !fallback.isEmpty()) return {fallback, true};
    return result;
#else
    return {fallback, false};
#endif
}

HardwareInventoryResult collectHardwareAdapters(const QJsonArray& seed)
{
    HardwareInventoryResult result;
    for (const auto& value : seed) {
        auto adapter = value.toObject();
        adapter.insert(QStringLiteral("speed_mbps"), QJsonValue(QJsonValue::Null));
        adapter.insert(QStringLiteral("receive_link_mbps"), QJsonValue(QJsonValue::Null));
        adapter.insert(QStringLiteral("transmit_link_mbps"), QJsonValue(QJsonValue::Null));
#ifdef Q_OS_WIN
        const int index = adapter.value(QStringLiteral("interface_index")).toInt(-1);
        MIB_IF_ROW2 row {};
        row.InterfaceIndex = index > 0 ? static_cast<NET_IFINDEX>(index) : 0;
        if (index > 0 && GetIfEntry2(&row) == NO_ERROR) {
            // Match by interface index: MACs and names may be shared or changed.
            adapter.insert(QStringLiteral("is_up"), row.OperStatus == IfOperStatusUp);
            adapter.insert(QStringLiteral("mtu"), static_cast<double>(row.Mtu));
            if (row.OperStatus == IfOperStatusUp) {
                adapter.insert(QStringLiteral("receive_link_mbps"), hardwareLinkMbps(row.ReceiveLinkSpeed));
                adapter.insert(QStringLiteral("transmit_link_mbps"), hardwareLinkMbps(row.TransmitLinkSpeed));
                adapter.insert(QStringLiteral("speed_mbps"), hardwareLinkMbps(row.TransmitLinkSpeed));
            }
            adapter.insert(QStringLiteral("link_source"), QStringLiteral("Windows GetIfEntry2"));
        } else {
            adapter.insert(QStringLiteral("link_source"), QStringLiteral("unavailable"));
            result.partial = true;
        }
#else
        const QString name = adapter.value(QStringLiteral("system_name")).toString();
        if (!name.isEmpty() && !name.contains(u'/') && !name.contains(u'\\')
            && name != QStringLiteral("..")) {
            QFile speed(QStringLiteral("/sys/class/net/%1/speed").arg(name));
            if (adapter.value(QStringLiteral("is_up")).toBool() && speed.open(QIODevice::ReadOnly)) {
                bool valid = false;
                const double mbps = QString::fromLatin1(speed.readAll()).trimmed().toDouble(&valid);
                if (valid && std::isfinite(mbps) && mbps > 0) {
                    adapter.insert(QStringLiteral("speed_mbps"), mbps);
                    adapter.insert(QStringLiteral("receive_link_mbps"), mbps);
                    adapter.insert(QStringLiteral("transmit_link_mbps"), mbps);
                    adapter.insert(QStringLiteral("link_source"), QStringLiteral("Linux sysfs"));
                }
            }
        }
#endif
        result.rows.append(adapter);
    }
    return result;
}

QJsonObject groupHardwareDisks(const QJsonArray& physical, const QJsonArray& volumes)
{
    QJsonArray groups;
    QJsonArray unmapped;
    QSet<int> knownNumbers;
    for (const auto& value : physical) {
        const auto disk = value.toObject();
        const int id = diskNumber(disk.value(QStringLiteral("Index")));
        if (id >= 0 && knownNumbers.contains(id)) continue;
        if (id >= 0) knownNumbers.insert(id);
        groups.append(QJsonObject {{QStringLiteral("physical"), disk},
            {QStringLiteral("volumes"), QJsonArray()}});
    }
    for (const auto& value : volumes) {
        auto volume = value.toObject();
        QSet<int> ids;
        if (volume.value(QStringLiteral("mapping_quality")) == QStringLiteral("valid")) {
            for (const auto& id : volume.value(QStringLiteral("physical_disk_numbers")).toArray()) {
                const int parsed = diskNumber(id);
                if (parsed >= 0) ids.insert(parsed);
            }
        }
        if (ids.isEmpty()) { unmapped.append(volume); continue; }
        volume.insert(QStringLiteral("spanned"), ids.size() > 1);
        auto sortedIds = ids.values();
        std::ranges::sort(sortedIds);
        for (const int id : sortedIds) {
            if (!knownNumbers.contains(id)) {
                groups.append(QJsonObject {{QStringLiteral("physical"), QJsonObject {
                    {QStringLiteral("Index"), id},
                    {QStringLiteral("Model"), QStringLiteral("н/д")},
                    {QStringLiteral("identity_quality"), QStringLiteral("unavailable")}}},
                    {QStringLiteral("volumes"), QJsonArray()}});
                knownNumbers.insert(id);
            }
            for (qsizetype i = 0; i < groups.size(); ++i) {
                auto group = groups.at(i).toObject();
                if (diskNumber(group.value(QStringLiteral("physical")).toObject()
                        .value(QStringLiteral("Index"))) != id) continue;
                auto members = group.value(QStringLiteral("volumes")).toArray();
                members.append(volume);
                group.insert(QStringLiteral("volumes"), members);
                groups.replace(i, group);
                break;
            }
        }
    }
    return {{QStringLiteral("groups"), groups}, {QStringLiteral("unmapped_volumes"), unmapped},
        {QStringLiteral("physical"), physical}, {QStringLiteral("volumes"), volumes}};
}

QJsonObject collectHardwareDisks(
    QJsonArray physical, QJsonArray volumes, const std::function<bool()>& cancelled)
{
    bool partial = false;
#ifdef Q_OS_WIN
    for (qsizetype i = 0; i < physical.size(); ++i) {
        if (cancelled && cancelled()) return {};
        physical.replace(i, physicalDetails(physical.at(i).toObject()));
    }
    for (qsizetype i = 0; i < volumes.size(); ++i) {
        if (cancelled && cancelled()) return {};
        const auto enriched = volumeDetails(volumes.at(i).toObject());
        partial |= enriched.value(QStringLiteral("mapping_quality")) != QStringLiteral("valid");
        volumes.replace(i, enriched);
    }
#else
    Q_UNUSED(cancelled);
#endif
    auto report = groupHardwareDisks(physical, volumes);
    report.insert(QStringLiteral("collection_partial"), partial);
    return report;
}

} // namespace orion::app
