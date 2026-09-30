#include "hardware_inventory_worker.h"
#include "hardware_inventory_details.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>

#include <algorithm>
#include <cmath>
#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wbemidl.h>
#endif

namespace orion::app {
namespace {

constexpr int kCollectorCount = 15;

[[nodiscard]] QString cleanText(const QJsonValue& value)
{
    const QString text = value.toString().trimmed();
    if (text.isEmpty()) return QStringLiteral("н/д");
    const QString lowered = text.toLower();
    static const QStringList placeholders {
        QStringLiteral("to be filled by o.e.m."), QStringLiteral("default string"),
        QStringLiteral("system product name"), QStringLiteral("system manufacturer"),
        QStringLiteral("unknown"), QStringLiteral("none"), QStringLiteral("not applicable"),
    };
    return placeholders.contains(lowered) ? QStringLiteral("н/д") : text;
}

[[nodiscard]] double number(const QJsonValue& value, const double fallback = -1.0)
{
    if (value.isDouble()) return value.toDouble(fallback);
    bool ok = false;
    const double parsed = value.toString().trimmed().toDouble(&ok);
    return ok && std::isfinite(parsed) ? parsed : fallback;
}

[[maybe_unused, nodiscard]] QString readTextFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

[[nodiscard]] QJsonObject unavailableBoard()
{
    return {{QStringLiteral("vendor"), QStringLiteral("н/д")},
            {QStringLiteral("model"), QStringLiteral("н/д")}};
}

[[nodiscard]] QJsonObject unavailableBios()
{
    return {{QStringLiteral("vendor"), QStringLiteral("н/д")},
            {QStringLiteral("version"), QStringLiteral("н/д")},
            {QStringLiteral("date"), QStringLiteral("н/д")}};
}

#ifdef Q_OS_WIN

[[nodiscard]] QString textFromVariant(const VARIANT& value)
{
    if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
        return QString::fromWCharArray(value.bstrVal).trimmed();
    }
    if (value.vt == VT_BOOL) return value.boolVal == VARIANT_TRUE
        ? QStringLiteral("true") : QStringLiteral("false");
    switch (value.vt) {
    case VT_I2: return QString::number(value.iVal);
    case VT_UI2: return QString::number(value.uiVal);
    case VT_I4:
    case VT_INT: return QString::number(value.lVal);
    case VT_UI4:
    case VT_UINT: return QString::number(value.ulVal);
    case VT_I8: return QString::number(value.llVal);
    case VT_UI8: return QString::number(value.ullVal);
    case VT_R4: return QString::number(value.fltVal, 'g', 12);
    case VT_R8: return QString::number(value.dblVal, 'g', 16);
    default: return {};
    }
}

class ComSession final {
public:
    ComSession() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComSession() { if (SUCCEEDED(result_)) CoUninitialize(); }
    [[nodiscard]] bool usable() const noexcept
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }
private:
    HRESULT result_;
};

class WmiSession final {
public:
    WmiSession()
    {
        if (!com_.usable()) return;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                IID_IWbemLocator, reinterpret_cast<void**>(&locator_))) || locator_ == nullptr) {
            return;
        }
        BSTR name = SysAllocString(L"ROOT\\CIMV2");
        const HRESULT connected = locator_->ConnectServer(
            name, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services_);
        SysFreeString(name);
        if (FAILED(connected) || services_ == nullptr) return;
        if (FAILED(CoSetProxyBlanket(services_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                nullptr, EOAC_NONE))) {
            services_->Release();
            services_ = nullptr;
        }
    }

    ~WmiSession()
    {
        if (services_ != nullptr) services_->Release();
        if (locator_ != nullptr) locator_->Release();
    }

    [[nodiscard]] bool usable() const noexcept { return services_ != nullptr; }
    [[nodiscard]] int failures() const noexcept { return failures_; }

    [[nodiscard]] QJsonArray query(
        const wchar_t* statement,
        const std::initializer_list<const wchar_t*> properties,
        const std::function<bool()>& cancelled = {}) const
    {
        QJsonArray rows;
        if (services_ == nullptr) { ++failures_; return rows; }
        BSTR language = SysAllocString(L"WQL");
        BSTR queryText = SysAllocString(statement);
        IEnumWbemClassObject* enumerator = nullptr;
        const HRESULT result = services_->ExecQuery(language, queryText,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &enumerator);
        SysFreeString(queryText);
        SysFreeString(language);
        if (FAILED(result) || enumerator == nullptr) { ++failures_; return rows; }

        QElapsedTimer timer;
        timer.start();
        while (!cancelled || !cancelled()) {
            IWbemClassObject* object = nullptr;
            ULONG returned = 0;
            const HRESULT next = enumerator->Next(750, 1, &object, &returned);
            if (returned == 0 || object == nullptr) {
                if (object != nullptr) object->Release();
                if (next == WBEM_S_FALSE) break;
                if (next == WBEM_S_TIMEDOUT && timer.elapsed() < 5000) continue;
                ++failures_;
                break;
            }
            QJsonObject row;
            for (const wchar_t* property : properties) {
                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(object->Get(property, 0, &value, nullptr, nullptr))) {
                    row.insert(QString::fromWCharArray(property), textFromVariant(value));
                }
                VariantClear(&value);
            }
            rows.append(row);
            object->Release();
            if (rows.size() >= 4096 || timer.elapsed() >= 5000) {
                ++failures_;
                break;
            }
        }
        enumerator->Release();
        return rows;
    }

private:
    ComSession com_;
    IWbemLocator* locator_ {nullptr};
    IWbemServices* services_ {nullptr};
    mutable int failures_ {0};
};

[[nodiscard]] QJsonValue batteryInfo()
{
    SYSTEM_POWER_STATUS status {};
    if (!GetSystemPowerStatus(&status) || status.BatteryFlag == 255) {
        return QJsonObject {{QStringLiteral("data_quality"), QStringLiteral("unavailable")}};
    }
    if ((status.BatteryFlag & 128) != 0) {
        return QJsonValue::Null;
    }
    QJsonObject battery {
        {QStringLiteral("percent"), status.BatteryLifePercent == 255
            ? QJsonValue(QJsonValue::Null) : QJsonValue(static_cast<int>(status.BatteryLifePercent))},
        {QStringLiteral("plugged_in"), status.ACLineStatus == 255
            ? QJsonValue(QJsonValue::Null) : QJsonValue(status.ACLineStatus == 1)},
    };
    battery.insert(QStringLiteral("eta_seconds"),
        status.BatteryLifeTime == static_cast<DWORD>(-1)
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(static_cast<double>(status.BatteryLifeTime)));
    return battery;
}

#else

[[nodiscard]] QJsonValue batteryInfo()
{
    const QDir power(QStringLiteral("/sys/class/power_supply"));
    const auto batteries = power.entryList({QStringLiteral("BAT*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    if (batteries.isEmpty()) return QJsonValue::Null;
    const QString root = power.filePath(batteries.constFirst());
    bool percentOk = false;
    const double percent = readTextFile(root + QStringLiteral("/capacity")).toDouble(&percentOk);
    const QString state = readTextFile(root + QStringLiteral("/status")).toLower();
    return QJsonObject {
        {QStringLiteral("percent"), percentOk ? QJsonValue(percent) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("plugged_in"), state == QStringLiteral("charging")
            || state == QStringLiteral("full")},
        {QStringLiteral("eta_seconds"), QJsonValue(QJsonValue::Null)},
    };
}

#endif

[[nodiscard]] bool gamingGpuName(const QString& value)
{
    const QString text = value.toLower();
    static const QStringList markers {
        QStringLiteral("rtx"), QStringLiteral("gtx"), QStringLiteral("radeon rx"),
        QStringLiteral("arc a7"), QStringLiteral("arc a5"), QStringLiteral("quadro"),
        QStringLiteral("titan"),
    };
    return std::ranges::any_of(markers, [&text](const QString& token) {
        return text.contains(token);
    });
}

[[nodiscard]] bool integratedGpuName(const QString& value)
{
    const QString text = value.toLower();
    static const QStringList markers {
        QStringLiteral("uhd graphics"), QStringLiteral("iris"),
        QStringLiteral("hd graphics"), QStringLiteral("graphics family"),
        QStringLiteral("vega graphics"), QStringLiteral("radeon(tm) graphics"),
    };
    return std::ranges::any_of(markers, [&text](const QString& token) {
        return text.contains(token);
    });
}

} // namespace

HardwareInventoryWorker::HardwareInventoryWorker(QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<QJsonObject>();
}

void HardwareInventoryWorker::scan(const QJsonObject& seed)
{
    if (isRunning()) return;
    {
        const QMutexLocker locker(&mutex_);
        seed_ = seed;
    }
    start();
}

void HardwareInventoryWorker::stop()
{
    requestInterruption();
}

QString HardwareInventoryWorker::memoryTypeName(const int smbiosType)
{
    switch (smbiosType) {
    case 20: return QStringLiteral("DDR");
    case 21: return QStringLiteral("DDR2");
    case 24: return QStringLiteral("DDR3");
    case 26: return QStringLiteral("DDR4");
    case 34: return QStringLiteral("DDR5");
    default: return QStringLiteral("н/д");
    }
}

QString HardwareInventoryWorker::normaliseFirmwareDate(const QString& value)
{
    const QString text = value.trimmed();
    static const QRegularExpression wmiDate(QStringLiteral("^(\\d{4})(\\d{2})(\\d{2})"));
    const auto match = wmiDate.match(text);
    if (match.hasMatch()) {
        return QStringLiteral("%1-%2-%3")
            .arg(match.captured(1), match.captured(2), match.captured(3));
    }
    const int t = text.indexOf(u'T');
    return text.isEmpty() ? QStringLiteral("н/д") : (t > 0 ? text.left(t) : text);
}

bool HardwareInventoryWorker::isNpuDeviceName(const QString& name)
{
    const QString text = name.toLower();
    static const QStringList positive {
        QStringLiteral("neural processing unit"), QStringLiteral("intel(r) ai boost"),
        QStringLiteral("intel ai boost"), QStringLiteral("intel vpu"),
        QStringLiteral("movidius"), QStringLiteral("amd xdna"),
        QStringLiteral("qualcomm hexagon"),
    };
    if (std::ranges::any_of(positive, [&text](const QString& token) {
            return text.contains(token);
        })) return true;
    static const QRegularExpression npuWord(QStringLiteral("(^|[^a-z0-9])npu([^a-z0-9]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    return npuWord.match(text).hasMatch();
}

QJsonObject HardwareInventoryWorker::ratePc(const QJsonObject& report)
{
    const auto cpu = report.value(QStringLiteral("cpu")).toObject();
    const auto ram = report.value(QStringLiteral("ram")).toObject();
    const double cores = number(cpu.value(QStringLiteral("physical_cores")));
    const double ramGiB = number(ram.value(QStringLiteral("total_gb")));
    if (cores < 0.0 || ramGiB < 0.0) {
        return {{QStringLiteral("key"), QStringLiteral("insufficient")},
                {QStringLiteral("label"), QStringLiteral("недостаточно данных")},
                {QStringLiteral("gpu_confidence"), QStringLiteral("none")},
                {QStringLiteral("reasons"), QJsonArray {
                    QStringLiteral("Не удалось подтвердить число физических ядер или объём RAM.")}}};
    }

    QString bestGpu;
    double bestMemory = -1.0;
    int bestGamingBonus = -1;
    for (const auto& value : report.value(QStringLiteral("gpu")).toArray()) {
        const auto gpu = value.toObject();
        const QString model = gpu.value(QStringLiteral("model")).toString().trimmed();
        if (model.isEmpty() || integratedGpuName(model)) continue;
        const double memory = number(gpu.value(QStringLiteral("memory_mb")));
        const int gamingBonus = gamingGpuName(model) ? 1 : 0;
        if (bestGpu.isEmpty() || gamingBonus > bestGamingBonus
            || (gamingBonus == bestGamingBonus && memory > bestMemory)) {
            bestGpu = model;
            bestMemory = memory;
            bestGamingBonus = gamingBonus;
        }
    }
    const bool vramKnown = bestMemory > 0.0;
    const QString confidence = bestGpu.isEmpty() ? QStringLiteral("none")
        : vramKnown ? QStringLiteral("known") : QStringLiteral("name_only");
    const bool gamingClass = !bestGpu.isEmpty()
        && ((vramKnown && bestMemory >= 4000.0)
            || (!vramKnown && gamingGpuName(bestGpu)));

    QJsonArray reasons;
    if (gamingClass && ramGiB >= 16.0 && cores >= 6.0) {
        reasons.append(QStringLiteral("Дискретная видеокарта игрового уровня: %1").arg(bestGpu));
        reasons.append(QStringLiteral("%1 ГБ RAM (порог: от 16 ГБ)").arg(ramGiB, 0, 'f', 1));
        reasons.append(QStringLiteral("%1 физ. ядер CPU (порог: от 6)").arg(cores, 0, 'f', 0));
    } else if (ramGiB < 8.0 || cores <= 2.0) {
        if (ramGiB < 8.0) reasons.append(QStringLiteral("Менее 8 ГБ оперативной памяти."));
        if (cores <= 2.0) reasons.append(QStringLiteral("Не более 2 физических ядер CPU."));
    } else {
        reasons.append(QStringLiteral("%1 ГБ RAM, %2 физ. ядер CPU")
            .arg(ramGiB, 0, 'f', 1).arg(cores, 0, 'f', 0));
        reasons.append(bestGpu.isEmpty()
            ? QStringLiteral("Дискретная видеокарта не обнаружена")
            : QStringLiteral("Дискретная видеокарта: %1%2").arg(bestGpu,
                vramKnown ? QStringLiteral(" (%1 МБ)").arg(bestMemory, 0, 'f', 0)
                          : QStringLiteral(" (объём памяти не определён)")));
    }
    if (confidence == QStringLiteral("name_only")) {
        reasons.append(QStringLiteral(
            "⚠ точный объём видеопамяти не определён; карта распознана только по имени, "
            "и часть оценки построена по названию."));
    }
    const QString key = gamingClass && ramGiB >= 16.0 && cores >= 6.0
        ? QStringLiteral("gaming")
        : (ramGiB < 8.0 || cores <= 2.0) ? QStringLiteral("weak")
                                         : QStringLiteral("ordinary");
    const QString label = key == QStringLiteral("gaming") ? QStringLiteral("игровой")
        : key == QStringLiteral("weak") ? QStringLiteral("слабый")
                                         : QStringLiteral("обычный");
    return {{QStringLiteral("key"), key},
            {QStringLiteral("label"), label},
            {QStringLiteral("gpu_confidence"), confidence},
            {QStringLiteral("reasons"), reasons}};
}

void HardwareInventoryWorker::run()
{
    emit scanStarted();
    QJsonObject seed;
    {
        const QMutexLocker locker(&mutex_);
        seed = seed_;
    }
    const QJsonObject report = collectReport(seed);
    if (!isInterruptionRequested()) emit reportReady(report);
}

QJsonObject HardwareInventoryWorker::collectReport(const QJsonObject& seed,
    const std::function<bool()>& externalCancellation)
{
    const auto cancelled = [this, &externalCancellation] {
        return isInterruptionRequested() || (externalCancellation && externalCancellation());
    };
    QJsonObject report;
    int completed = 0;
    const auto add = [this, &report, &completed, &cancelled](
                         const QString& key, const QString& label, const QJsonValue& value) {
        if (cancelled()) return false;
        report.insert(key, value);
        emit progressChanged(++completed, kCollectorCount, label);
        return !cancelled();
    };

#ifdef Q_OS_WIN
    WmiSession wmi;
#endif

    const QJsonObject sensorSnapshot = seed.value(QStringLiteral("sensor_snapshot")).toObject();
    if (!add(QStringLiteral("sensor_snapshot"), QStringLiteral("Источники датчиков"),
            sensorSnapshot)) return {};
    if (!add(QStringLiteral("os"), QStringLiteral("Операционная система"),
            seed.value(QStringLiteral("os")))) return {};

    QJsonObject board = unavailableBoard();
#ifdef Q_OS_WIN
    const auto boardRows = wmi.query(L"SELECT Manufacturer, Product FROM Win32_BaseBoard",
        {L"Manufacturer", L"Product"}, cancelled);
    if (!boardRows.isEmpty()) {
        const auto row = boardRows.at(0).toObject();
        board.insert(QStringLiteral("vendor"), cleanText(row.value(QStringLiteral("Manufacturer"))));
        board.insert(QStringLiteral("model"), cleanText(row.value(QStringLiteral("Product"))));
    }
#else
    board.insert(QStringLiteral("vendor"), cleanText(readTextFile(
        QStringLiteral("/sys/class/dmi/id/board_vendor"))));
    board.insert(QStringLiteral("model"), cleanText(readTextFile(
        QStringLiteral("/sys/class/dmi/id/board_name"))));
#endif
    if (!add(QStringLiteral("motherboard"), QStringLiteral("Материнская плата"), board)) return {};

    QJsonObject bios = unavailableBios();
#ifdef Q_OS_WIN
    const auto biosRows = wmi.query(
        L"SELECT Manufacturer, SMBIOSBIOSVersion, ReleaseDate FROM Win32_BIOS",
        {L"Manufacturer", L"SMBIOSBIOSVersion", L"ReleaseDate"}, cancelled);
    if (!biosRows.isEmpty()) {
        const auto row = biosRows.at(0).toObject();
        bios.insert(QStringLiteral("vendor"), cleanText(row.value(QStringLiteral("Manufacturer"))));
        bios.insert(QStringLiteral("version"), cleanText(row.value(QStringLiteral("SMBIOSBIOSVersion"))));
        bios.insert(QStringLiteral("date"), normaliseFirmwareDate(
            row.value(QStringLiteral("ReleaseDate")).toString()));
    }
#else
    bios.insert(QStringLiteral("vendor"), cleanText(readTextFile(
        QStringLiteral("/sys/class/dmi/id/bios_vendor"))));
    bios.insert(QStringLiteral("version"), cleanText(readTextFile(
        QStringLiteral("/sys/class/dmi/id/bios_version"))));
    bios.insert(QStringLiteral("date"), cleanText(readTextFile(
        QStringLiteral("/sys/class/dmi/id/bios_date"))));
#endif
    if (!add(QStringLiteral("bios"), QStringLiteral("BIOS / UEFI"), bios)) return {};

    QJsonObject cpu = seed.value(QStringLiteral("cpu")).toObject();
    cpu.insert(QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture());
#ifdef Q_OS_WIN
    const auto cpuRows = wmi.query(
        L"SELECT Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed, SocketDesignation FROM Win32_Processor",
        {L"Name", L"NumberOfCores", L"NumberOfLogicalProcessors", L"MaxClockSpeed", L"SocketDesignation"},
        cancelled);
    double physical = 0.0;
    double logical = 0.0;
    double maxClock = -1.0;
    QStringList sockets;
    for (const auto& value : cpuRows) {
        const auto row = value.toObject();
        physical += std::max(0.0, number(row.value(QStringLiteral("NumberOfCores")), 0.0));
        logical += std::max(0.0, number(row.value(QStringLiteral("NumberOfLogicalProcessors")), 0.0));
        maxClock = std::max(maxClock, number(row.value(QStringLiteral("MaxClockSpeed"))));
        const QString socket = cleanText(row.value(QStringLiteral("SocketDesignation")));
        if (socket != QStringLiteral("н/д") && !sockets.contains(socket)) sockets.append(socket);
        if (cleanText(cpu.value(QStringLiteral("model"))) == QStringLiteral("н/д")) {
            cpu.insert(QStringLiteral("model"), cleanText(row.value(QStringLiteral("Name"))));
        }
    }
    if (physical > 0.0) cpu.insert(QStringLiteral("physical_cores"), physical);
    if (logical > 0.0) cpu.insert(QStringLiteral("logical_threads"), logical);
    if (maxClock > 0.0) cpu.insert(QStringLiteral("max_frequency_mhz"), maxClock);
    cpu.insert(QStringLiteral("socket"), sockets.isEmpty() ? QStringLiteral("н/д") : sockets.join(QStringLiteral(", ")));
    cpu.insert(QStringLiteral("socket_count"), cpuRows.isEmpty()
        ? QJsonValue(QJsonValue::Null) : QJsonValue(cpuRows.size()));
#else
    QSet<QString> physicalCores;
    QSet<QString> packages;
    const QDir cpuDir(QStringLiteral("/sys/devices/system/cpu"));
    const auto entries = cpuDir.entryList({QStringLiteral("cpu[0-9]*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        const QString root = cpuDir.filePath(entry + QStringLiteral("/topology/"));
        const QString package = readTextFile(root + QStringLiteral("physical_package_id"));
        const QString core = readTextFile(root + QStringLiteral("core_id"));
        if (!package.isEmpty()) packages.insert(package);
        if (!core.isEmpty()) physicalCores.insert(package + u':' + core);
    }
    if (!physicalCores.isEmpty()) cpu.insert(QStringLiteral("physical_cores"), physicalCores.size());
    if (!entries.isEmpty()) cpu.insert(QStringLiteral("logical_threads"), entries.size());
    cpu.insert(QStringLiteral("socket"), QStringLiteral("н/д"));
    cpu.insert(QStringLiteral("socket_count"), packages.isEmpty()
        ? QJsonValue(QJsonValue::Null) : QJsonValue(packages.size()));
#endif
    if (!cpu.contains(QStringLiteral("physical_cores"))) cpu.insert(
        QStringLiteral("physical_cores"), QJsonValue(QJsonValue::Null));
    if (!cpu.contains(QStringLiteral("logical_threads"))) cpu.insert(
        QStringLiteral("logical_threads"), QJsonValue(QJsonValue::Null));
    if (!add(QStringLiteral("cpu"), QStringLiteral("Процессор"), cpu)) return {};

    QJsonObject ram = seed.value(QStringLiteral("ram")).toObject();
    QJsonArray modules;
#ifdef Q_OS_WIN
    const auto ramRows = wmi.query(
        L"SELECT Manufacturer, PartNumber, SerialNumber, Capacity, Speed, ConfiguredClockSpeed, SMBIOSMemoryType, DeviceLocator, BankLabel FROM Win32_PhysicalMemory",
        {L"Manufacturer", L"PartNumber", L"SerialNumber", L"Capacity", L"Speed",
         L"ConfiguredClockSpeed", L"SMBIOSMemoryType", L"DeviceLocator", L"BankLabel"}, cancelled);
    double installedBytes = 0.0;
    QSet<QString> types;
    QSet<int> speeds;
    for (const auto& value : ramRows) {
        auto module = value.toObject();
        const double capacity = number(module.value(QStringLiteral("Capacity")));
        if (capacity > 0.0) installedBytes += capacity;
        const int configured = static_cast<int>(number(
            module.value(QStringLiteral("ConfiguredClockSpeed")), 0.0));
        const int advertised = static_cast<int>(number(module.value(QStringLiteral("Speed")), 0.0));
        const int speed = configured > 0 ? configured : advertised;
        if (speed > 0) speeds.insert(speed);
        const QString type = memoryTypeName(static_cast<int>(number(
            module.value(QStringLiteral("SMBIOSMemoryType")), 0.0)));
        if (type != QStringLiteral("н/д")) types.insert(type);
        module.insert(QStringLiteral("capacity_gb"), capacity > 0.0
            ? QJsonValue(capacity / (1024.0 * 1024.0 * 1024.0)) : QJsonValue(QJsonValue::Null));
        module.insert(QStringLiteral("speed_mhz"), speed > 0
            ? QJsonValue(speed) : QJsonValue(QJsonValue::Null));
        module.insert(QStringLiteral("type"), type);
        modules.append(module);
    }
    if (installedBytes > 0.0) ram.insert(QStringLiteral("total_gb"),
        installedBytes / (1024.0 * 1024.0 * 1024.0));
    QStringList typeSpeed;
    if (!types.isEmpty()) typeSpeed.append(QStringList(types.cbegin(), types.cend()).join(QStringLiteral(" / ")));
    if (!speeds.isEmpty()) {
        QStringList values;
        for (const int speed : speeds) values.append(QStringLiteral("%1 МГц").arg(speed));
        typeSpeed.append(values.join(QStringLiteral(" / ")));
    }
    ram.insert(QStringLiteral("type_and_speed"), typeSpeed.isEmpty()
        ? QStringLiteral("н/д") : typeSpeed.join(QStringLiteral(" · ")));
#else
    ram.insert(QStringLiteral("type_and_speed"), QStringLiteral("н/д — SPD/SMBIOS не доступен без привилегий"));
#endif
    ram.insert(QStringLiteral("modules"), modules);
    if (!add(QStringLiteral("ram"), QStringLiteral("Оперативная память"), ram)) return {};
    if (!add(QStringLiteral("runtime_memory"), QStringLiteral("Состояние памяти"),
            seed.value(QStringLiteral("runtime_memory")))) return {};
    const auto gpus = collectHardwareGpus(seed.value(QStringLiteral("gpu")).toArray());
    if (!add(QStringLiteral("gpu"), QStringLiteral("Видеокарты"), gpus.rows)) return {};

    QJsonArray npuDevices;
    bool npuCollectionFailed = false;
#ifdef Q_OS_WIN
    const int failuresBeforeNpu = wmi.failures();
    const auto pnpRows = wmi.query(
        L"SELECT Name, Manufacturer, Status, PNPDeviceID FROM Win32_PnPEntity",
        {L"Name", L"Manufacturer", L"Status", L"PNPDeviceID"}, cancelled);
    npuCollectionFailed = wmi.failures() != failuresBeforeNpu;
    for (const auto& value : pnpRows) {
        const auto row = value.toObject();
        if (!isNpuDeviceName(row.value(QStringLiteral("Name")).toString())) continue;
        npuDevices.append(QJsonObject {
            {QStringLiteral("name"), cleanText(row.value(QStringLiteral("Name")))},
            {QStringLiteral("vendor"), cleanText(row.value(QStringLiteral("Manufacturer")))},
            {QStringLiteral("status"), cleanText(row.value(QStringLiteral("Status")))},
            {QStringLiteral("device_id"), cleanText(row.value(QStringLiteral("PNPDeviceID")))},
            {QStringLiteral("source"), QStringLiteral("Windows PnP / WMI")},
        });
    }
#else
    const QDir accel(QStringLiteral("/sys/class/accel"));
    for (const QString& entry : accel.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString name = readTextFile(accel.filePath(entry + QStringLiteral("/device/name")));
        if (isNpuDeviceName(name + u' ' + entry)) {
            npuDevices.append(QJsonObject {{QStringLiteral("name"), name.isEmpty() ? entry : name},
                {QStringLiteral("vendor"), QStringLiteral("н/д")},
                {QStringLiteral("status"), QStringLiteral("обнаружен")},
                {QStringLiteral("source"), QStringLiteral("Linux sysfs accel")}});
        }
    }
#endif
    const QJsonObject npu {
        {QStringLiteral("detected"), !npuDevices.isEmpty()},
        {QStringLiteral("devices"), npuDevices},
        {QStringLiteral("data_quality"), npuCollectionFailed ? QStringLiteral("collector_error") : npuDevices.isEmpty()
            ? QStringLiteral("not_detected") : QStringLiteral("device_inventory")},
        {QStringLiteral("stress_test_supported"), false},
        {QStringLiteral("inference_validation_supported"), false},
        {QStringLiteral("note"), QStringLiteral(
            "Нагрузочный тест NPU не запускается без подтверждённого execution provider.")},
    };
    if (!add(QStringLiteral("npu"), QStringLiteral("Нейропроцессор"), npu)) return {};

    QJsonObject disks {{QStringLiteral("volumes"), seed.value(QStringLiteral("disks"))}};
#ifdef Q_OS_WIN
    disks.insert(QStringLiteral("physical"), wmi.query(
        L"SELECT Index, Model, SerialNumber, FirmwareRevision, InterfaceType, MediaType, Size, Status, DeviceID FROM Win32_DiskDrive",
        {L"Index", L"Model", L"SerialNumber", L"FirmwareRevision", L"InterfaceType",
         L"MediaType", L"Size", L"Status", L"DeviceID"}, cancelled));
#else
    QJsonArray physical;
    const QDir block(QStringLiteral("/sys/class/block"));
    for (const QString& entry : block.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.startsWith(QStringLiteral("loop")) || entry.startsWith(QStringLiteral("ram"))) continue;
        const QString model = readTextFile(block.filePath(entry + QStringLiteral("/device/model")));
        if (model.isEmpty()) continue;
        physical.append(QJsonObject {{QStringLiteral("Model"), model},
            {QStringLiteral("SerialNumber"), readTextFile(block.filePath(entry + QStringLiteral("/device/serial")))},
            {QStringLiteral("FirmwareRevision"), readTextFile(block.filePath(entry + QStringLiteral("/device/rev")))},
            {QStringLiteral("DeviceID"), QStringLiteral("/dev/%1").arg(entry)}});
    }
    disks.insert(QStringLiteral("physical"), physical);
#endif
    disks = collectHardwareDisks(disks.value(QStringLiteral("physical")).toArray(),
        disks.value(QStringLiteral("volumes")).toArray(),
        cancelled);
    if (!add(QStringLiteral("disks"), QStringLiteral("Накопители"), disks)) return {};
    if (!add(QStringLiteral("displays"), QStringLiteral("Мониторы"),
            seed.value(QStringLiteral("displays")))) return {};
    if (!add(QStringLiteral("battery"), QStringLiteral("Батарея"), batteryInfo())) return {};
    const auto adapters = collectHardwareAdapters(seed.value(QStringLiteral("network_adapters")).toArray());
    if (!add(QStringLiteral("network_adapters"), QStringLiteral("Сетевые адаптеры"), adapters.rows)) return {};
    if (!add(QStringLiteral("fans"), QStringLiteral("Вентиляторы"),
            seed.value(QStringLiteral("fans")))) return {};
    if (!add(QStringLiteral("sensors"), QStringLiteral("Температуры"),
            seed.value(QStringLiteral("sensors")))) return {};
    report.insert(QStringLiteral("rating"), ratePc(report));
    bool partial = gpus.partial || adapters.partial || disks.value(QStringLiteral("collection_partial")).toBool();
#ifdef Q_OS_WIN
    partial |= wmi.failures() != 0;
#endif
    report.insert(QStringLiteral("collection_partial"), partial);
    return report;
}

} // namespace orion::app
