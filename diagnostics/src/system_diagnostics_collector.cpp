#include "orion/diagnostics/system_diagnostics_collector.h"
#include "orion/diagnostics/system_log_limits.h"

#include "orion/core/thresholds.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonValue>
#include <QList>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winevt.h>
#endif

namespace orion::diagnostics {
namespace {

struct ProcessResult {
    bool started {false};
    bool timedOut {false};
    int exitCode {-1};
    QByteArray output;
    QString error;
};

[[nodiscard]] ProcessResult runProcess(
    const QString& program,
    const QStringList& arguments,
    const int timeoutMs)
{
    QProcess process;
#ifdef _WIN32
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(std::min(timeoutMs, 2000))) {
        return {false, false, -1, {}, process.errorString()};
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {true, true, -1, process.readAllStandardOutput(),
            QStringLiteral("процесс превысил тайм-аут")};
    }
    return {true, false, process.exitCode(), process.readAllStandardOutput(),
        QString::fromLocal8Bit(process.readAllStandardError()).trimmed()};
}

[[nodiscard]] QString smartctlPath()
{
    const auto executable = QStandardPaths::findExecutable(QStringLiteral("smartctl"));
    if (!executable.isEmpty()) {
        return executable;
    }
#ifdef _WIN32
    const QStringList candidates {
        QDir::fromNativeSeparators(
            qEnvironmentVariable("ProgramFiles") + QStringLiteral("/smartmontools/bin/smartctl.exe")),
        QDir::fromNativeSeparators(
            qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/smartmontools/bin/smartctl.exe")),
    };
    for (const auto& candidate : candidates) {
        if (!candidate.startsWith(u'/') && QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
#endif
    return {};
}

[[nodiscard]] QJsonValue optionalInteger(const std::optional<qint64>& value)
{
    return value.has_value() ? QJsonValue {static_cast<double>(*value)}
                             : QJsonValue {QJsonValue::Null};
}

[[nodiscard]] QJsonValue optionalNumber(const std::optional<double>& value)
{
    return value.has_value() ? QJsonValue {*value} : QJsonValue {QJsonValue::Null};
}

[[nodiscard]] std::optional<qint64> integer(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    return static_cast<qint64>(value.toDouble());
}

[[nodiscard]] QJsonObject emptySmartDevice(
    const QString& name,
    const QString& note)
{
    return {
        {QStringLiteral("device"), name},
        {QStringLiteral("model"), QStringLiteral("н/д")},
        {QStringLiteral("health"), QStringLiteral("н/д")},
        {QStringLiteral("disk_type"), QStringLiteral("н/д")},
        {QStringLiteral("is_nvme"), false},
        {QStringLiteral("reallocated"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("pending"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("uncorrectable"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("temperature_c"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("temperature_level"), QStringLiteral("unknown")},
        {QStringLiteral("nvme_critical_warning"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("nvme_available_spare"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("nvme_available_spare_threshold"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("nvme_percentage_used"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("nvme_media_errors"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("nvme_error_log_entries"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("nvme_unsafe_shutdowns"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("level"), QStringLiteral("unknown")},
        {QStringLiteral("risk_reasons"), QJsonArray {}},
        {QStringLiteral("data_quality"), QStringLiteral("collector_error")},
        {QStringLiteral("note"), note},
    };
}

void appendRisk(
    QJsonArray& risks,
    const QString& code,
    const QString& severity,
    const QString& text,
    const std::optional<qint64>& value = std::nullopt)
{
    QJsonObject risk {
        {QStringLiteral("code"), code},
        {QStringLiteral("severity"), severity},
        {QStringLiteral("text"), text},
    };
    if (value.has_value()) {
        risk.insert(QStringLiteral("value"), static_cast<double>(*value));
    }
    risks.append(risk);
}

[[nodiscard]] QString normalizedErrorSignature(QString text)
{
    static const QRegularExpression timestamp(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2}[T\\s][^\\s]+\\s+"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bcast(
        QStringLiteral("BcastDVRUserService_[0-9A-Fa-f]+"));
    static const QRegularExpression hex(QStringLiteral("\\b0x[0-9A-Fa-f]+\\b"));
    static const QRegularExpression guid(
        QStringLiteral("\\b[0-9A-Fa-f]{8}-[0-9A-Fa-f-]{27,}\\b"));
    text = text.simplified();
    text.remove(timestamp);
    text.replace(bcast, QStringLiteral("BcastDVRUserService_<instance>"));
    text.replace(hex, QStringLiteral("<hex>"));
    text.replace(guid, QStringLiteral("<guid>"));
    return text.toLower().simplified();
}

[[nodiscard]] QString reportQuality(const QJsonObject& report)
{
    if (report.isEmpty()) {
        return QStringLiteral("collector_error");
    }
    const auto quality = report.value(QStringLiteral("data_quality")).toString(QStringLiteral("valid"));
    return quality == "valid" && report.value("collection_limited").toBool() ? QStringLiteral("estimated") : quality;
}

[[nodiscard]] QString worseQuality(QString left, QString right)
{
    const QHash<QString, int> rank {
        {QStringLiteral("valid"), 0},
        {QStringLiteral("estimated"), 1},
        {QStringLiteral("unsupported"), 2},
        {QStringLiteral("permission_denied"), 3},
        {QStringLiteral("collector_error"), 4},
    };
    if (!rank.contains(left)) left = QStringLiteral("collector_error");
    if (!rank.contains(right)) right = QStringLiteral("collector_error");
    return rank.value(left) >= rank.value(right) ? left : right;
}

#ifdef _WIN32
class EventHandle {
public:
    EventHandle() = default;
    explicit EventHandle(EVT_HANDLE value) : value_(value) {}
    ~EventHandle() { if (value_ != nullptr) EvtClose(value_); }
    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;
    EventHandle(EventHandle&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    [[nodiscard]] EVT_HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
private:
    EVT_HANDLE value_ {nullptr};
};

[[nodiscard]] QString windowsErrorText(const DWORD error)
{
    wchar_t buffer[2048] {};
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, buffer, 2048, nullptr);
    return length > 0
        ? QString::fromWCharArray(buffer, static_cast<qsizetype>(length)).trimmed()
        : QStringLiteral("Windows error %1").arg(error);
}

bool admitEventBuffer(quint64 bytes, quint64 maximum, LogReadStats& stats)
{
    const auto admission = reserveLogBuffer(bytes, maximum, stats.remainingBytes);
    if (admission == LogBufferAdmission::Oversized) ++stats.oversized;
    if (admission == LogBufferAdmission::Exhausted) stats.byteLimitReached = true;
    return admission == LogBufferAdmission::Accepted;
}

[[nodiscard]] QString renderEventXml(const EVT_HANDLE event, LogReadStats& stats)
{
    DWORD bytes = 0;
    DWORD properties = 0;
    EvtRender(nullptr, event, EvtRenderEventXml, 0, nullptr, &bytes, &properties);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
        return {};
    }
    if (bytes % sizeof(wchar_t) != 0 || !admitEventBuffer(bytes, kEventXmlBytes, stats)) return {};
    std::vector<wchar_t> buffer(static_cast<std::size_t>(bytes / sizeof(wchar_t)), L'\0');
    if (!EvtRender(nullptr, event, EvtRenderEventXml, bytes, buffer.data(),
                   &bytes, &properties)) {
        return {};
    }
    const auto end = std::find(buffer.cbegin(), buffer.cend(), L'\0');
    return QString::fromWCharArray(buffer.data(), end - buffer.cbegin());
}

[[nodiscard]] QString formatEventMessage(
    const EVT_HANDLE event,
    const QString& provider, LogReadStats& stats)
{
    EventHandle metadata(EvtOpenPublisherMetadata(
        nullptr,
        reinterpret_cast<LPCWSTR>(provider.utf16()),
        nullptr, 0, 0));
    if (!metadata) {
        return {};
    }
    DWORD characters = 0;
    EvtFormatMessage(metadata.get(), event, 0, 0, nullptr,
        EvtFormatMessageEvent, 0, nullptr, &characters);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || characters == 0) {
        return {};
    }
    // EvtFormatMessage uses WCHAR counts; widen before multiplication and check before allocation.
    if (!admitEventBuffer(quint64(characters) * sizeof(wchar_t), kEventMessageBytes, stats)) return {};
    std::vector<wchar_t> buffer(static_cast<std::size_t>(characters));
    if (!EvtFormatMessage(metadata.get(), event, 0, 0, nullptr,
                          EvtFormatMessageEvent, characters, buffer.data(), &characters)) {
        return {};
    }
    const auto end = std::find(buffer.cbegin(), buffer.cend(), L'\0');
    return QString::fromWCharArray(buffer.data(), end - buffer.cbegin());
}

struct EventQueryResult {
    QList<QJsonObject> events;
    QString error;
    bool permissionDenied {false};
    LogReadStats stats;
};

[[nodiscard]] EventQueryResult queryWindowsChannel(
    const QString& channel,
    const int limit)
{
    const auto uptimeMs = static_cast<qulonglong>(GetTickCount64());
    const QString query = QStringLiteral(
        "*[System[(Level=1 or Level=2) and TimeCreated[timediff(@SystemTime) <= %1]]]")
                              .arg(uptimeMs);
    EventHandle results(EvtQuery(
        nullptr,
        reinterpret_cast<LPCWSTR>(channel.utf16()),
        reinterpret_cast<LPCWSTR>(query.utf16()),
        EvtQueryChannelPath | EvtQueryReverseDirection));
    if (!results) {
        const DWORD error = GetLastError();
        return {{}, windowsErrorText(error), error == ERROR_ACCESS_DENIED, {}};
    }

    EventQueryResult result;
    constexpr DWORD batchSize = 16;
    EVT_HANDLE batch[batchSize] {};
    while (result.stats.examined < limit && !result.stats.byteLimitReached) {
        DWORD returned = 0;
        const auto requested = std::min<DWORD>(batchSize, limit - result.stats.examined);
        if (!EvtNext(results.get(), requested, batch, 0, 0, &returned)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                result.error = windowsErrorText(error);
                result.permissionDenied = error == ERROR_ACCESS_DENIED;
            }
            break;
        }
        for (DWORD index = 0; index < returned; ++index) {
            EventHandle event(batch[index]);
            ++result.stats.examined;
            const QString xml = renderEventXml(event.get(), result.stats);
            auto parsed = parseWindowsEventXml(xml);
            if (!parsed.isEmpty()) {
                const QString message = formatEventMessage(
                    event.get(), parsed.value(QStringLiteral("provider")).toString(), result.stats);
                if (!message.isEmpty()) {
                    parsed = parseWindowsEventXml(xml, message);
                } else ++result.stats.unformatted;
                result.stats.shortened += parsed.value("shortened").toBool() ? 1 : 0;
                result.events.append(parsed);
            } else ++result.stats.skipped;
            if (result.stats.examined >= limit || result.stats.byteLimitReached) {
                for (DWORD rest = index + 1; rest < returned; ++rest) {
                    EvtClose(batch[rest]);
                }
                break;
            }
        }
    }
    result.stats.recordLimitReached = result.stats.examined >= limit;
    return result;
}
#endif

#if defined(__linux__)
[[nodiscard]] QJsonObject collectLinuxSystemErrors(const int limit)
{
    const QString program = QStandardPaths::findExecutable(QStringLiteral("journalctl"));
    if (program.isEmpty()) {
        return {
            {QStringLiteral("errors"), QJsonArray {}},
            {QStringLiteral("note"), QStringLiteral("journalctl не найден")},
            {QStringLiteral("data_quality"), QStringLiteral("unsupported")},
            {QStringLiteral("source"), QStringLiteral("journalctl")},
        };
    }
    const auto result = runBoundedLogProcess(program, {
        QStringLiteral("-p"), QStringLiteral("err"), QStringLiteral("-b"),
        QStringLiteral("--no-pager"), QStringLiteral("-n"), QString::number(limit),
        QStringLiteral("-r"), QStringLiteral("-o"), QStringLiteral("json")}, 6000);
    return journalctlProcessReport(result, limit);
}
#endif

} // namespace

QJsonObject parseSmartctlDeviceJson(
    const QByteArray& json,
    const QString& requestedDevice)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (!document.isObject()) {
        return emptySmartDevice(requestedDevice,
            QStringLiteral("не удалось разобрать ответ smartctl: %1").arg(error.errorString()));
    }
    const auto data = document.object();
    const auto device = data.value(QStringLiteral("device")).toObject();
    const QString name = requestedDevice.isEmpty()
        ? device.value(QStringLiteral("name")).toString(QStringLiteral("неизвестный диск"))
        : requestedDevice;
    const QString model = data.value(QStringLiteral("model_name"))
        .toString(device.value(QStringLiteral("name")).toString(name));
    const auto smartStatus = data.value(QStringLiteral("smart_status")).toObject();
    const auto passedValue = smartStatus.value(QStringLiteral("passed"));
    const std::optional<bool> passed = passedValue.isBool()
        ? std::optional<bool> {passedValue.toBool()} : std::nullopt;
    const QString health = !passed.has_value() ? QStringLiteral("н/д")
        : *passed ? QStringLiteral("OK") : QStringLiteral("ОТКАЗ");

    const QString protocol = device.value(QStringLiteral("protocol")).toString();
    const QString deviceType = device.value(QStringLiteral("type")).toString();
    const bool isNvme = QStringLiteral("%1 %2 %3").arg(protocol, deviceType, name)
                            .contains(QStringLiteral("nvme"), Qt::CaseInsensitive);

    QHash<QString, std::optional<qint64>> attributes;
    for (const auto& value : data.value(QStringLiteral("ata_smart_attributes"))
                                 .toObject().value(QStringLiteral("table")).toArray()) {
        const auto attribute = value.toObject();
        const auto attributeName = attribute.value(QStringLiteral("name")).toString().toLower();
        if (!attributeName.isEmpty()) {
            attributes.insert(attributeName,
                integer(attribute.value(QStringLiteral("raw")).toObject()
                            .value(QStringLiteral("value"))));
        }
    }
    const auto firstAttribute = [&attributes](const QStringList& needles) -> std::optional<qint64> {
        for (auto it = attributes.cbegin(); it != attributes.cend(); ++it) {
            for (const auto& needle : needles) {
                if (it.key().contains(needle)) return it.value();
            }
        }
        return std::nullopt;
    };
    const auto reallocated = firstAttribute({QStringLiteral("reallocated_sector"),
        QStringLiteral("reallocated_event")});
    const auto pending = firstAttribute({QStringLiteral("current_pending"),
        QStringLiteral("pending_sector")});
    const auto uncorrectable = firstAttribute({QStringLiteral("offline_uncorrectable"),
        QStringLiteral("reported_uncorrect")});

    const auto temperatureValue = data.value(QStringLiteral("temperature"))
                                      .toObject().value(QStringLiteral("current"));
    const std::optional<double> temperature = temperatureValue.isDouble()
        ? std::optional<double> {temperatureValue.toDouble()} : std::nullopt;
    const auto rotationValue = data.value(QStringLiteral("rotation_rate"));
    QString diskType = QStringLiteral("н/д");
    std::string_view temperatureKind = "generic";
    if (isNvme) {
        diskType = QStringLiteral("NVMe SSD");
        temperatureKind = "nvme";
    } else if (rotationValue.isDouble() && rotationValue.toInt() == 0) {
        diskType = QStringLiteral("SSD");
        temperatureKind = "ssd";
    } else if (rotationValue.isDouble() && rotationValue.toInt() > 0) {
        diskType = QStringLiteral("HDD, %1 RPM").arg(rotationValue.toInt());
        temperatureKind = "hdd";
    }

    const auto nvme = data.value(QStringLiteral("nvme_smart_health_information_log")).toObject();
    const auto criticalWarning = integer(nvme.value(QStringLiteral("critical_warning")));
    const auto availableSpare = integer(nvme.value(QStringLiteral("available_spare")));
    const auto spareThreshold = integer(nvme.value(QStringLiteral("available_spare_threshold")));
    const auto percentageUsed = integer(nvme.value(QStringLiteral("percentage_used")));
    const auto mediaErrors = integer(nvme.value(QStringLiteral("media_errors")));
    const auto errorLogEntries = integer(nvme.value(QStringLiteral("num_err_log_entries")));
    const auto unsafeShutdowns = integer(nvme.value(QStringLiteral("unsafe_shutdowns")));

    QJsonArray risks;
    if (passed.has_value() && !*passed) {
        appendRisk(risks, QStringLiteral("smart_failed"), QStringLiteral("critical"),
            QStringLiteral("SMART overall-health сообщает отказ"));
    }
    if (pending.value_or(0) > 0) {
        appendRisk(risks, QStringLiteral("pending_sectors"), QStringLiteral("critical"),
            QStringLiteral("есть нестабильные pending-сектора"), pending);
    }
    if (uncorrectable.value_or(0) > 0) {
        appendRisk(risks, QStringLiteral("uncorrectable_sectors"), QStringLiteral("critical"),
            QStringLiteral("есть неисправимые ошибки чтения"), uncorrectable);
    }
    if (criticalWarning.value_or(0) > 0) {
        appendRisk(risks, QStringLiteral("nvme_critical_warning"), QStringLiteral("critical"),
            QStringLiteral("NVMe выставил Critical Warning"), criticalWarning);
    }
    if (mediaErrors.value_or(0) > 0) {
        appendRisk(risks, QStringLiteral("nvme_media_errors"), QStringLiteral("critical"),
            QStringLiteral("NVMe зарегистрировал ошибки целостности носителя"), mediaErrors);
    }
    if (reallocated.value_or(0) > 0) {
        appendRisk(risks, QStringLiteral("reallocated_sectors"), QStringLiteral("warning"),
            QStringLiteral("есть переназначенные сектора"), reallocated);
    }
    if (percentageUsed.has_value() && *percentageUsed >= 100) {
        appendRisk(risks, QStringLiteral("nvme_wear_exhausted"), QStringLiteral("warning"),
            QStringLiteral("расчётный ресурс NVMe достиг или превысил 100%"), percentageUsed);
    } else if (percentageUsed.has_value() && *percentageUsed >= 90) {
        appendRisk(risks, QStringLiteral("nvme_wear_high"), QStringLiteral("warning"),
            QStringLiteral("NVMe близок к расчётному ресурсу"), percentageUsed);
    }
    if (availableSpare.has_value() && spareThreshold.has_value()
        && *availableSpare <= *spareThreshold) {
        appendRisk(risks, QStringLiteral("nvme_spare_low"), QStringLiteral("warning"),
            QStringLiteral("запас резервных блоков NVMe достиг порога"), availableSpare);
    }

    auto smartLevel = orion::core::StatusLevel::Ok;
    bool criticalRisk = false;
    bool warningRisk = false;
    for (const auto& value : risks) {
        const auto severity = value.toObject().value(QStringLiteral("severity")).toString();
        criticalRisk |= severity == QStringLiteral("critical");
        warningRisk |= severity == QStringLiteral("warning");
    }
    if (criticalRisk) smartLevel = orion::core::StatusLevel::Critical;
    else if (warningRisk) smartLevel = orion::core::StatusLevel::Warning;
    else if (!passed.has_value() && attributes.isEmpty() && nvme.isEmpty())
        smartLevel = orion::core::StatusLevel::Unknown;
    const auto temperatureLevel = orion::core::levelForTemperature(temperature, temperatureKind);
    const auto level = orion::core::worse(smartLevel, temperatureLevel);

    return {
        {QStringLiteral("device"), name},
        {QStringLiteral("model"), model},
        {QStringLiteral("health"), health},
        {QStringLiteral("disk_type"), diskType},
        {QStringLiteral("is_nvme"), isNvme},
        {QStringLiteral("reallocated"), optionalInteger(reallocated)},
        {QStringLiteral("pending"), optionalInteger(pending)},
        {QStringLiteral("uncorrectable"), optionalInteger(uncorrectable)},
        {QStringLiteral("temperature_c"), optionalNumber(temperature)},
        {QStringLiteral("temperature_level"), QString::fromLatin1(orion::core::toString(temperatureLevel))},
        {QStringLiteral("nvme_critical_warning"), optionalInteger(criticalWarning)},
        {QStringLiteral("nvme_available_spare"), optionalInteger(availableSpare)},
        {QStringLiteral("nvme_available_spare_threshold"), optionalInteger(spareThreshold)},
        {QStringLiteral("nvme_percentage_used"), optionalInteger(percentageUsed)},
        {QStringLiteral("nvme_media_errors"), optionalInteger(mediaErrors)},
        {QStringLiteral("nvme_error_log_entries"), optionalInteger(errorLogEntries)},
        {QStringLiteral("nvme_unsafe_shutdowns"), optionalInteger(unsafeShutdowns)},
        {QStringLiteral("level"), QString::fromLatin1(orion::core::toString(level))},
        {QStringLiteral("risk_reasons"), risks},
        {QStringLiteral("data_quality"), passed.has_value() || !attributes.isEmpty() || !nvme.isEmpty()
             ? QStringLiteral("valid") : QStringLiteral("partial")},
        {QStringLiteral("note"), QJsonValue {QJsonValue::Null}},
    };
}

QJsonObject parseWindowsEventXml(
    const QString& xml,
    const QString& formattedMessage)
{
    if (xml.size() > kEventXmlBytes / 2 || xml.trimmed().isEmpty()) {
        return {};
    }
    QString provider;
    QString timestamp;
    QString channel;
    QString eventId;
    QStringList dataValues;
    bool shortened = false;
    QXmlStreamReader reader(xml);
    reader.setEntityExpansionLimit(0);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.tokenType() == QXmlStreamReader::DTD) return {};
        if (!reader.isStartElement()) continue;
        const auto name = reader.name();
        if (name == QStringLiteral("Provider")) {
            provider = boundedLogText(reader.attributes().value(QStringLiteral("Name")), 256, shortened);
        } else if (name == QStringLiteral("TimeCreated")) {
            timestamp = boundedLogText(reader.attributes().value(QStringLiteral("SystemTime")), 64, shortened);
        } else if (name == QStringLiteral("EventID")) {
            eventId = boundedLogText(reader.readElementText(), 32, shortened);
        } else if (name == QStringLiteral("Channel")) {
            channel = boundedLogText(reader.readElementText(), 256, shortened);
        } else if (name == QStringLiteral("Data")) {
            if (dataValues.size() >= 3) { reader.skipCurrentElement(); continue; }
            const auto value = boundedLogText(reader.readElementText(), kLogLineUnits, shortened);
            if (!value.isEmpty()) dataValues.append(value);
        }
    }
    if (reader.hasError()) {
        return {};
    }
    QString message = boundedLogText(formattedMessage, kLogLineUnits, shortened, true);
    if (message.isEmpty()) {
        message = dataValues.sliced(0, std::min<qsizetype>(3, dataValues.size()))
                      .join(QStringLiteral("; "));
    }
    if (message.isEmpty()) message = QStringLiteral("сообщение события недоступно");
    message = boundedLogText(message, kLogLineUnits, shortened);
    bool idOk = false;
    const int numericId = eventId.toInt(&idOk);
    const QString line = boundedLogText(QStringLiteral("%1 [%2] EventID=%3 Provider=%4 %5")
        .arg(timestamp.isEmpty() ? QStringLiteral("н/д") : timestamp,
             channel.isEmpty() ? QStringLiteral("н/д") : channel,
             eventId.isEmpty() ? QStringLiteral("н/д") : eventId,
             provider.isEmpty() ? QStringLiteral("н/д") : provider,
             message), kLogLineUnits, shortened);
    return {
        {QStringLiteral("timestamp"), timestamp},
        {QStringLiteral("channel"), channel},
        {QStringLiteral("event_id"), idOk ? QJsonValue {numericId} : QJsonValue {eventId}},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("message"), message},
        {QStringLiteral("line"), line},
        {QStringLiteral("shortened"), shortened},
    };
}

QJsonArray summarizeSystemErrorEntries(
    const QJsonArray& entries,
    const int maximumGroups)
{
    struct Group { int count {0}; QString example; };
    QHash<QString, Group> groups;
    QStringList order;
    for (const auto& value : entries) {
        const auto text = value.toString().simplified();
        if (text.isEmpty()) continue;
        const auto signature = normalizedErrorSignature(text);
        if (!groups.contains(signature)) {
            groups.insert(signature, Group {0, text});
            order.append(signature);
        }
        ++groups[signature].count;
    }
    QJsonArray result;
    const int limit = std::min(std::max(1, maximumGroups), static_cast<int>(order.size()));
    for (int index = 0; index < limit; ++index) {
        const auto group = groups.value(order[index]);
        result.append(QJsonObject {
            {QStringLiteral("count"), group.count},
            {QStringLiteral("example"), group.example},
        });
    }
    if (order.size() > limit) {
        result.append(QJsonObject {
            {QStringLiteral("count"), 0},
            {QStringLiteral("example"), QStringLiteral("… ещё групп сообщений: %1").arg(order.size() - limit)},
            {QStringLiteral("omitted"), true},
        });
    }
    return result;
}

QJsonObject diffSystemErrorReports(
    const QJsonObject& before,
    const QJsonObject& after)
{
    const auto beforeQuality = reportQuality(before);
    const auto afterQuality = reportQuality(after);
    const bool limited = before.value("collection_limited").toBool() || after.value("collection_limited").toBool();
    const bool comparable = !before.isEmpty() && !after.isEmpty()
        && (beforeQuality == QStringLiteral("valid") || beforeQuality == QStringLiteral("estimated"))
        && (afterQuality == QStringLiteral("valid") || afterQuality == QStringLiteral("estimated"));
    if (!comparable) {
        return {
            {QStringLiteral("errors"), QJsonArray {}},
            {QStringLiteral("note"), QStringLiteral("снимки системного журнала нельзя надёжно сравнить")},
            {QStringLiteral("comparison_supported"), false},
            {QStringLiteral("data_quality"), worseQuality(beforeQuality, afterQuality)},
            {QStringLiteral("before_quality"), beforeQuality},
            {QStringLiteral("after_quality"), afterQuality},
            {QStringLiteral("collection_limited"), limited},
        };
    }
    QHash<QString, int> beforeCounts;
    for (const auto& value : before.value(QStringLiteral("errors")).toArray()) {
        ++beforeCounts[value.toString()];
    }
    QJsonArray newErrors;
    for (const auto& value : after.value(QStringLiteral("errors")).toArray()) {
        const auto entry = value.toString();
        if (beforeCounts.value(entry) > 0) {
            --beforeCounts[entry];
        } else {
            newErrors.append(entry);
        }
    }
    return {
        {QStringLiteral("errors"), newErrors},
        {QStringLiteral("note"), limited ? QJsonValue(QStringLiteral(
             "Журнал прочитан не полностью: сравнивается только доступная часть снимков; отсутствие новых записей не подтверждает отсутствие событий."))
             : newErrors.isEmpty()
             ? QJsonValue {QStringLiteral("новых ошибок после нагрузки не обнаружено")}
             : QJsonValue {QJsonValue::Null}},
        {QStringLiteral("comparison_supported"), true},
        {QStringLiteral("data_quality"), beforeQuality == QStringLiteral("valid")
                 && afterQuality == QStringLiteral("valid")
             ? QStringLiteral("valid") : QStringLiteral("estimated")},
        {QStringLiteral("before_quality"), beforeQuality},
        {QStringLiteral("after_quality"), afterQuality},
        {QStringLiteral("collection_limited"), limited},
    };
}

QDateTime parseSystemErrorTimestamp(const QString& entry)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^(\\d{4}-\\d{2}-\\d{2}[T ]\\d{2}:\\d{2}:\\d{2})"
        "([\\.,]\\d+)?(Z|[+-]\\d{2}:?\\d{2})?"));
    const auto match = pattern.match(entry.trimmed());
    if (!match.hasMatch()) return {};
    QString fraction = match.captured(2);
    if (!fraction.isEmpty()) {
        fraction.remove(0, 1);
        fraction = fraction.left(3).leftJustified(3, QLatin1Char('0'));
        fraction.prepend(QLatin1Char('.'));
    }
    QString zone = match.captured(3);
    if (zone.size() == 5 && zone.at(3) != QLatin1Char(':')) zone.insert(3, QLatin1Char(':'));
    QString stamp = match.captured(1).replace(QLatin1Char(' '), QLatin1Char('T'))
        + fraction + zone;
    auto parsed = QDateTime::fromString(
        stamp, fraction.isEmpty() ? Qt::ISODate : Qt::ISODateWithMs);
    if (!parsed.isValid()) return {};
    if (parsed.timeSpec() == Qt::LocalTime) parsed.setTimeZone(QTimeZone::systemTimeZone());
    return parsed.toUTC();
}

QJsonObject mergeSystemErrorReports(
    const QJsonObject& first,
    const QJsonObject& second)
{
    QJsonArray errors;
    QSet<QString> seen;
    QStringList notes;
    bool limited = false;
    int snapshotIndex = 0;
    for (const auto& report : {first, second}) {
        ++snapshotIndex;
        limited |= report.value("collection_limited").toBool();
        const auto note = report.value("note").toString();
        if (!note.isEmpty()) notes.append(QStringLiteral("Снимок %1: %2").arg(snapshotIndex).arg(note));
        for (const auto& value : report.value(QStringLiteral("errors")).toArray()) {
            const QString entry = value.toString();
            if (!entry.isEmpty() && !seen.contains(entry)) {
                seen.insert(entry);
                errors.append(entry);
            }
        }
    }
    return {
        {QStringLiteral("errors"), errors},
        {QStringLiteral("data_quality"), worseQuality(reportQuality(first), reportQuality(second))},
        {QStringLiteral("source"), QStringLiteral("merged timestamped system-log snapshots")},
        {QStringLiteral("collection_limited"), limited},
        {QStringLiteral("note"), notes.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(notes.join("; "))},
    };
}

QJsonObject filterSystemErrorReportWindow(
    const QJsonObject& report,
    const QString& centerAt,
    const double beforeSeconds,
    const double afterSeconds)
{
    const auto center = parseSystemErrorTimestamp(centerAt);
    const auto entries = report.value(QStringLiteral("errors")).toArray();
    const double before = std::max(0.0, beforeSeconds);
    const double after = std::max(0.0, afterSeconds);
    QJsonArray errors;
    QJsonArray matches;
    int timestamped = 0;
    int unparseable = 0;
    std::optional<double> closest;
    if (center.isValid()) {
        for (const auto& value : entries) {
            const QString entry = value.toString();
            const auto timestamp = parseSystemErrorTimestamp(entry);
            if (!timestamp.isValid()) {
                ++unparseable;
                continue;
            }
            ++timestamped;
            const double offset = static_cast<double>(center.msecsTo(timestamp)) / 1000.0;
            if (offset < -before || offset > after) continue;
            errors.append(entry);
            matches.append(QJsonObject {
                {QStringLiteral("entry"), entry},
                {QStringLiteral("event_at"), timestamp.toString(Qt::ISODateWithMs)},
                {QStringLiteral("offset_seconds"), offset},
            });
            if (!closest.has_value() || std::abs(offset) < std::abs(*closest)) closest = offset;
        }
    } else {
        unparseable = entries.size();
    }
    QStringList notes;
    if (!report.value("note").toString().isEmpty()) notes.append(report.value("note").toString());
    if (!center.isValid()) notes.append(QStringLiteral("время пользовательской отметки не удалось разобрать"));
    else if (timestamped == 0) notes.append(QStringLiteral("источник не предоставил разбираемые временные метки"));
    return {
        {QStringLiteral("errors"), errors},
        {QStringLiteral("matches"), matches},
        {QStringLiteral("timestamped_count"), timestamped},
        {QStringLiteral("unparseable_count"), unparseable},
        {QStringLiteral("closest_offset_seconds"), closest.has_value()
             ? QJsonValue {*closest} : QJsonValue {QJsonValue::Null}},
        {QStringLiteral("window_before_seconds"), before},
        {QStringLiteral("window_after_seconds"), after},
        {QStringLiteral("note"), notes.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(notes.join("; "))},
        {QStringLiteral("collection_limited"), report.value("collection_limited").toBool()},
        {QStringLiteral("data_quality"), reportQuality(report)},
    };
}

QJsonObject collectSmartReport()
{
    const auto executable = smartctlPath();
    if (executable.isEmpty()) {
        return {
            {QStringLiteral("available"), false},
            {QStringLiteral("disks"), QJsonArray {}},
            {QStringLiteral("note"), QStringLiteral(
                 "smartctl (smartmontools) не найден в системе — SMART-диагностика диска недоступна")},
#ifdef _WIN32
            {QStringLiteral("install_hint"), QStringLiteral(
                 "winget install --id smartmontools.smartmontools (или установите smartmontools.org)")},
#else
            {QStringLiteral("install_hint"), QStringLiteral("установите пакет smartmontools")},
#endif
            {QStringLiteral("source"), QStringLiteral("smartctl JSON")},
        };
    }
    const auto version = runProcess(executable, {QStringLiteral("--version")}, 4000);
    if (!version.started || version.timedOut || (version.exitCode != 0 && version.exitCode != 1)) {
        return {
            {QStringLiteral("available"), false},
            {QStringLiteral("disks"), QJsonArray {}},
            {QStringLiteral("note"), version.error.isEmpty()
                 ? QStringLiteral("smartctl недоступен") : version.error},
            {QStringLiteral("install_hint"), QJsonValue {QJsonValue::Null}},
            {QStringLiteral("source"), QStringLiteral("smartctl JSON")},
        };
    }
    const auto scan = runProcess(executable,
        {QStringLiteral("--scan"), QStringLiteral("-j")}, 6000);
    const auto scanDocument = QJsonDocument::fromJson(scan.output);
    if (!scan.started || scan.timedOut || !scanDocument.isObject()) {
        return {
            {QStringLiteral("available"), true},
            {QStringLiteral("disks"), QJsonArray {}},
            {QStringLiteral("note"), scan.error.isEmpty()
                 ? QStringLiteral("smartctl не обнаружил ни одного опрашиваемого устройства")
                 : scan.error},
            {QStringLiteral("install_hint"), QJsonValue {QJsonValue::Null}},
            {QStringLiteral("source"), QStringLiteral("smartctl JSON")},
        };
    }
    QJsonArray disks;
    const auto devices = scanDocument.object().value(QStringLiteral("devices")).toArray();
    for (const auto& value : devices) {
        const auto device = value.toObject();
        const auto name = device.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;
        QStringList arguments {QStringLiteral("-a"), QStringLiteral("-j")};
        const auto type = device.value(QStringLiteral("type")).toString();
        if (!type.isEmpty()) arguments.append({QStringLiteral("-d"), type});
        arguments.append(name);
        const auto query = runProcess(executable, arguments, 8000);
        if (!query.started || query.output.isEmpty()) {
            disks.append(emptySmartDevice(name,
                query.error.isEmpty() ? QStringLiteral("smartctl не вернул данных") : query.error));
        } else {
            disks.append(parseSmartctlDeviceJson(query.output, name));
        }
    }
    return {
        {QStringLiteral("available"), true},
        {QStringLiteral("disks"), disks},
        {QStringLiteral("note"), disks.isEmpty()
             ? QJsonValue {QStringLiteral("smartctl не обнаружил ни одного опрашиваемого устройства")}
             : QJsonValue {QJsonValue::Null}},
        {QStringLiteral("install_hint"), QJsonValue {QJsonValue::Null}},
        {QStringLiteral("source"), QStringLiteral("smartctl JSON")},
    };
}

QJsonObject collectSystemErrorReport(const int requestedLimit)
{
    const int limit = std::clamp(requestedLimit, 1, 500);
#ifdef _WIN32
    const auto system = queryWindowsChannel(QStringLiteral("System"), limit);
    const auto application = queryWindowsChannel(QStringLiteral("Application"), limit);
    QList<QJsonObject> events = system.events;
    events.append(application.events);
    std::stable_sort(events.begin(), events.end(), [](const auto& left, const auto& right) {
        return left.value(QStringLiteral("timestamp")).toString()
            > right.value(QStringLiteral("timestamp")).toString();
    });
    QJsonArray errors;
    const int count = std::min(limit, static_cast<int>(events.size()));
    for (int index = 0; index < count; ++index) {
        errors.append(events[index].value(QStringLiteral("line")).toString());
    }
    const bool systemOk = system.error.isEmpty();
    const bool applicationOk = application.error.isEmpty();
    QStringList notes;
    if (!systemOk) notes.append(QStringLiteral("System: %1").arg(system.error));
    if (!applicationOk) notes.append(QStringLiteral("Application: %1").arg(application.error));
    if (system.stats.limited()) notes.append(QStringLiteral("System: %1").arg(system.stats.note()));
    if (application.stats.limited()) notes.append(QStringLiteral("Application: %1").arg(application.stats.note()));
    if (events.size() > limit) notes.append(QStringLiteral("Показаны только последние %1 доступных записей из двух каналов.").arg(limit));
    const bool limited = !systemOk || !applicationOk || system.stats.limited()
        || application.stats.limited() || events.size() > limit;
    QString quality = QStringLiteral("valid");
    if (!systemOk || !applicationOk) {
        quality = system.permissionDenied || application.permissionDenied
            ? QStringLiteral("permission_denied")
            : (systemOk || applicationOk) ? QStringLiteral("estimated")
                                          : QStringLiteral("collector_error");
    }
    if (limited && quality == "valid") quality = "estimated";
    QString note;
    if (!notes.isEmpty()) note = notes.join(QStringLiteral("; "));
    else if (errors.isEmpty()) note = QStringLiteral(
        "критических событий и ошибок с момента загрузки не найдено");
    return {
        {QStringLiteral("errors"), errors},
        {QStringLiteral("note"), note.isEmpty()
             ? QJsonValue {QJsonValue::Null} : QJsonValue {note}},
        {QStringLiteral("data_quality"), quality},
        {QStringLiteral("source"), QStringLiteral("Windows Event Log API (wevtapi)")},
        {QStringLiteral("since_boot"), true},
        {QStringLiteral("limit"), limit},
        {QStringLiteral("groups"), summarizeSystemErrorEntries(errors)},
        {QStringLiteral("collection_limited"), limited},
        {QStringLiteral("collection_limits"), systemLogLimits()},
        {QStringLiteral("read_stats"), QJsonObject{{"System", system.stats.toJson()}, {"Application", application.stats.toJson()}}},
    };
#elif defined(__linux__)
    return collectLinuxSystemErrors(limit);
#else
    return {
        {QStringLiteral("errors"), QJsonArray {}},
        {QStringLiteral("note"), QStringLiteral("Сбор системных ошибок не поддерживается на этой ОС")},
        {QStringLiteral("data_quality"), QStringLiteral("unsupported")},
        {QStringLiteral("source"), QStringLiteral("unsupported")},
    };
#endif
}

} // namespace orion::diagnostics
