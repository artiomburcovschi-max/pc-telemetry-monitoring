#include "orion/diagnostics/diagnostic_engine.h"
#include "orion/diagnostics/incident_data.h"

#include "orion/core/thresholds.h"
#include "orion/diagnostics/diagnostic_schema.h"
#include "orion/diagnostics/system_diagnostics_collector.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>
#include <QStringList>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>

namespace orion::diagnostics {
namespace {

[[nodiscard]] std::optional<double> number(
    const QJsonObject& object,
    const QString& key) noexcept
{
    const auto value = object.value(key);
    return value.isDouble() ? std::optional {value.toDouble()} : std::nullopt;
}

[[nodiscard]] std::optional<double> nestedNumber(
    const QJsonObject& summary,
    const QString& window,
    const QString& metric,
    const QString& field) noexcept
{
    return number(
        summary.value(window).toObject().value(metric).toObject(),
        field);
}

[[nodiscard]] bool containsText(
    const QJsonArray& values,
    const QStringList& needles)
{
    for (const auto& value : values) {
        const auto text = value.toString().toLower();
        for (const auto& needle : needles) {
            if (text.contains(needle)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool hasWhea(const QJsonArray& values)
{
    return containsText(values, {
        QStringLiteral("whea"),
        QStringLiteral("machine check"),
        QStringLiteral("machine-check"),
    });
}

[[nodiscard]] bool hasDiskError(const QJsonArray& values)
{
    return containsText(values, {
        QStringLiteral("disk"),
        QStringLiteral("i/o error"),
        QStringLiteral("nvme"),
        QStringLiteral("storahci"),
        QStringLiteral("bad block"),
    });
}

[[nodiscard]] bool hasDisplayError(const QJsonArray& values)
{
    return containsText(values, {
        QStringLiteral("nvlddmkm"), QStringLiteral("amdkmdap"),
        QStringLiteral("amdwddmg"), QStringLiteral("dxgkrnl"),
        QStringLiteral("display driver"), QStringLiteral("tdr"),
        QStringLiteral("видеодрайвер"),
    });
}

[[nodiscard]] bool hasMemoryExhaustion(const QJsonArray& values)
{
    return containsText(values, {
        QStringLiteral("eventid=2004"), QStringLiteral("resource-exhaustion"),
        QStringLiteral("out of memory"), QStringLiteral("not enough memory"),
        QStringLiteral("недостаточно памяти"), QStringLiteral("нехватк памяти"),
    });
}

[[nodiscard]] bool isKnownBackgroundLogNoise(const QString& line)
{
    const auto text = line.toLower();
    return text.contains(QStringLiteral("eventid=10029"))
        && text.contains(QStringLiteral("distributedcom"))
        && text.contains(QStringLiteral("bcastdvruserservice"))
        && (text.contains(QStringLiteral("appcaptureshell"))
            || text.contains(QStringLiteral("appcapturemanager"))
            || text.contains(QStringLiteral("windows.media.capture")));
}

[[nodiscard]] QString logSignature(QString text)
{
    static const QRegularExpression timestamp(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2}[T\\s][^\\s]+\\s+"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hex(QStringLiteral("\\b0x[0-9a-f]+\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression guid(
        QStringLiteral("\\b[0-9a-f]{8}-[0-9a-f-]{27,}\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression largeNumber(QStringLiteral("\\b\\d{5,}\\b"));
    text = text.toLower().simplified();
    text.remove(timestamp);
    text.replace(hex, QStringLiteral("<hex>"));
    text.replace(guid, QStringLiteral("<guid>"));
    text.replace(largeNumber, QStringLiteral("<n>"));
    return text.simplified();
}

struct LogCategoryDefinition {
    QString key;
    QString label;
    QStringList keywords;
    QString explanation;
    QString domain;
    QString confidence;
    int minimumCount {2};
};

struct LogCategorySummary {
    int count {0};
    int signatures {0};
    QStringList examples;
};

[[nodiscard]] const QList<LogCategoryDefinition>& logCategoryDefinitions()
{
    static const QList<LogCategoryDefinition> definitions {
        {QStringLiteral("whea"),
         QStringLiteral("Аппаратные ошибки (WHEA/Machine Check)"),
         {QStringLiteral("whea"), QStringLiteral("machine check"),
          QStringLiteral("machine-check"),
          QStringLiteral("hardware error"), QStringLiteral("mce:"), QStringLiteral("edac")},
         QStringLiteral("ОС получила аппаратную ошибку от CPU, памяти или шины. Исключите разгон, проверьте память и повторите нагрузочный тест."),
         QStringLiteral("stability"), QStringLiteral("high"), 1},
        {QStringLiteral("display"),
         QStringLiteral("Видеодрайвер/дисплей"),
         {QStringLiteral("nvlddmkm"), QStringLiteral("amdkmdap"),
          QStringLiteral("amdwddmg"), QStringLiteral("dxgkrnl"), QStringLiteral("tdr"),
          QStringLiteral("display driver"), QStringLiteral("видеодрайвер")},
         QStringLiteral("Повторяются сбросы или ошибки видеодрайвера. Сверьте их время с GPU-нагрузкой и температурой."),
         QStringLiteral("gpu"), QStringLiteral("medium"), 2},
        {QStringLiteral("disk"),
         QStringLiteral("Диск/контроллер хранения"),
         {QStringLiteral("storahci"), QStringLiteral("stornvme"), QStringLiteral("nvme"),
          QStringLiteral("ntfs"), QStringLiteral("scsi"), QStringLiteral("sata"),
          QStringLiteral("atapi"), QStringLiteral("volsnap"), QStringLiteral("i/o error"),
          QStringLiteral("blk_update_request")},
         QStringLiteral("Повторяются ошибки накопителя, файловой системы или контроллера. Сопоставьте их с конкретным диском и SMART."),
         QStringLiteral("storage"), QStringLiteral("medium"), 2},
        {QStringLiteral("memory_exhaustion"),
         QStringLiteral("Исчерпание памяти/commit"),
         {QStringLiteral("eventid=2004"), QStringLiteral("resource-exhaustion-detector"),
          QStringLiteral("out of memory"), QStringLiteral("not enough memory"),
          QStringLiteral("недостаточно памяти"), QStringLiteral("нехватк памяти")},
         QStringLiteral("ОС зафиксировала исчерпание commit или памяти. Сопоставьте событие с private memory, RAM и pagefile."),
         QStringLiteral("memory"), QStringLiteral("high"), 1},
        {QStringLiteral("app_hang"),
         QStringLiteral("Зависания приложений"),
         {QStringLiteral("apphang"), QStringLiteral("hung"),
          QStringLiteral("not responding"), QStringLiteral("faulting application"),
          QStringLiteral("зависла"), QStringLiteral("зависло")},
         QStringLiteral("ОС фиксирует зависания приложений. Для причины нужна корреляция с RAM, диском, GPU и временем события."),
         QStringLiteral("applications"), QStringLiteral("low"), 2},
    };
    return definitions;
}

// Use the same first-match precedence for the general log and incident rules.
[[nodiscard]] const LogCategoryDefinition* logCategory(const QString& line)
{
    if (isKnownBackgroundLogNoise(line)) return nullptr;
    const auto signature = logSignature(line);
    for (const auto& definition : logCategoryDefinitions())
        for (const auto& keyword : definition.keywords)
            if (signature.contains(keyword)) return &definition;
    return nullptr;
}

void appendFinding(
    QJsonArray& findings,
    const QString& id,
    const QString& severity,
    const QString& title,
    const QString& detail,
    FindingOptions options = {})
{
    options.id = id;
    findings.append(makeFinding(severity, title, detail, options));
}

[[nodiscard]] QString incidentLogQuality(const QJsonObject& report)
{
    const auto quality = report.value("data_quality").toString("unknown");
    return quality == "valid" && report.value("collection_limited").toBool()
        ? QStringLiteral("estimated") : quality;
}

void analyzeIncidentLogs(QJsonArray& findings, const QJsonObject& incident)
{
    const auto correlation = incident.value("recent_system_error_correlation").toObject();
    QString quality = incidentLogQuality(correlation);
    bool comparable = correlation.value("comparison_supported") != false;
    QJsonArray coverage;
    const auto recordQuality = [&coverage, &quality, &comparable](const QString& label, const QString& value) {
        if (value == "valid") return;
        coverage.append(makeEvidence(label, value, "system log collector", value));
        const auto rank = [](const QString& item) {
            return item == "valid" ? 0 : item == "estimated" ? 1 : item == "unknown" ? 2 : 3;
        };
        if (rank(value) > rank(quality)) quality = value;
        if (value != "estimated") comparable = false;
    };
    for (const auto& key : {QStringLiteral("log_before"), QStringLiteral("log_after")})
        if (incident.contains(key)) recordQuality(key, incidentLogQuality(incident.value(key).toObject()));
    if (correlation.contains("data_quality") || correlation.value("collection_limited").toBool())
        recordQuality(QStringLiteral("Временное окно журнала"), incidentLogQuality(correlation));
    if (correlation.contains("comparison_quality"))
        recordQuality(QStringLiteral("Сравнение до/после"), correlation.value("comparison_quality").toString("unknown"));
    if (!comparable || !coverage.isEmpty()) {
        FindingOptions options;
        options.domain = "coverage"; options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = "high"; options.groupKey = "coverage.incident.logs";
        options.impact = "test_validity";
        if (!comparable) coverage.append(makeEvidence(QStringLiteral("Сравнение до/после"), false,
            "before/after system log diff", "unknown"));
        options.evidence = coverage;
        appendFinding(findings, "coverage.incident.system_log", "info",
            QStringLiteral("Системный журнал около отметки проверен не полностью"),
            QStringLiteral("Доступные записи сохраняются, но неполное сравнение не подтверждает новизну событий. Отсутствие совпадений не доказывает норму."), options);
    }
    const auto reportMissingTimestamps = [&findings](int count) {
        FindingOptions options;
        options.domain = "coverage"; options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = "high"; options.groupKey = "coverage.incident.logs";
        options.evidence = {makeEvidence(QStringLiteral("Неразбираемых временных меток"),
            count, "timestamped system-log window", "unknown")};
        appendFinding(findings, "coverage.incident.log_timestamps", "info",
            QStringLiteral("Время части событий журнала не удалось сопоставить"),
            QStringLiteral("Строки без разбираемой временной метки нельзя уверенно связать с отметкой. Близость другого события не переносится на них."), options);
    };

    const auto newErrors = incident.value("new_system_errors").toArray();
    const auto recentValue = incident.value("recent_system_errors");
    const auto recentErrors = recentValue.isObject() ? recentValue.toObject().value("errors").toArray() : recentValue.toArray();
    const auto center = parseSystemErrorTimestamp(incident.value("marker_timestamp").toString());
    const double after = std::clamp(number(incident, "post_seconds").value_or(15.0), 0.0, 300.0);
    const auto offsetFor = [&](const QString& entry) -> std::optional<double> {
        if (center.isValid()) {
            const auto time = parseSystemErrorTimestamp(entry);
            if (time.isValid()) return center.msecsTo(time) / 1000.0;
            return std::nullopt;
        }
        // Older reports may omit the marker, but must identify the exact row.
        // Never borrow the global closest offset from an unrelated category.
        if (!incident.contains("marker_timestamp")) {
            for (const auto& value : correlation.value("matches").toArray()) {
                const auto match = value.toObject();
                const auto offset = number(match, "offset_seconds");
                if (match.value("entry") == entry && offset && std::isfinite(*offset)) return offset;
            }
        }
        return std::nullopt;
    };
    struct Context {
        int newCount {0}; int recentCount {0};
        std::optional<double> closest;
        QStringList examples;
    };
    QHash<QString, Context> contexts;
    QSet<QString> seen;
    int missingOffsets = 0;
    QJsonArray candidates = newErrors;
    for (const auto& value : recentErrors) candidates.append(value);
    for (const auto& value : candidates) {
        const auto entry = value.toString();
        if (entry.isEmpty() || seen.contains(entry)) continue;
        seen.insert(entry);
        const auto* category = logCategory(entry);
        if (!category || (category->key != "whea" && category->key != "display" && category->key != "app_hang")) continue;
        const auto offset = offsetFor(entry);
        if (!offset && recentErrors.contains(value)) ++missingOffsets;
        const bool isNew = comparable && newErrors.contains(value)
            && (!incident.contains("marker_timestamp") || (offset && *offset >= 0 && *offset <= after));
        const bool isRecent = recentErrors.contains(value) && offset && *offset >= -120 && *offset <= after;
        if (!isNew && !isRecent) continue;
        auto& context = contexts[category->key];
        if (isNew) ++context.newCount; else ++context.recentCount;
        if (offset && (!context.closest || std::abs(*offset) < std::abs(*context.closest))) context.closest = offset;
        if (context.examples.size() < 2) context.examples.append(entry);
    }
    const int unparseable = std::max(correlation.value("unparseable_count").toInt(), missingOffsets);
    if (unparseable > 0) reportMissingTimestamps(unparseable);
    for (const auto& definition : logCategoryDefinitions()) {
        const auto context = contexts.value(definition.key);
        if (!context.newCount && !context.recentCount) continue;
        const bool newContext = context.newCount > 0;
        const QString source = newContext ? "before/after system log diff" : "timestamped system-log window";
        const bool close = context.closest && std::abs(*context.closest) <= 15;
        FindingOptions options;
        options.domain = definition.domain;
        options.confidence = quality == "valid" ? (newContext || close ? "high" : "medium")
            : (quality == "estimated" || quality == "unknown" ? "medium" : "low");
        options.impact = "stability";
        options.evidence = {
            makeEvidence(QStringLiteral("Связанных уникальных строк категории"), context.newCount + context.recentCount, source, quality),
            makeEvidence(QStringLiteral("Строк категории в разнице снимков"), context.newCount, "before/after system log diff", quality),
            makeEvidence(QStringLiteral("Остальных строк категории по времени"), context.recentCount, "timestamped system-log window", quality),
            makeEvidence(QStringLiteral("Ближайшая запись этой категории относительно отметки, с"),
                context.closest ? QJsonValue(*context.closest) : QJsonValue(QJsonValue::Null), "event timestamp", quality),
        };
        for (const auto& example : context.examples) options.evidence.append(makeEvidence(QStringLiteral("Пример"), example, source, quality));
        const QString contextText = newContext
            ? (quality == "valid" ? QStringLiteral("Строка отсутствовала в первом снимке и появилась во втором. ")
                                  : QStringLiteral("Строка появилась в доступной части сравнения; из-за неполноты журнала её новизна не подтверждена. "))
            : QStringLiteral("Связь установлена по времени записи, а не по подтверждённому появлению нового события. ");
        QString title, detail;
        if (definition.key == "whea") {
            options.status = QString::fromLatin1(kStatusFail); options.urgency = "immediate";
            options.priority = 80; options.groupKey = "stability.hardware";
            options.rootCause = "possible hardware instability";
            title = QStringLiteral("Около момента проблемы зафиксированы аппаратные ошибки WHEA/Machine Check");
            detail = QStringLiteral("Запись может указывать на аппаратную нестабильность; конкретный компонент и причина сбоя этим не установлены.");
            options.actions = {QStringLiteral("Сопоставить точное время и Event ID с температурами и настройками разгона")};
        } else if (definition.key == "display") {
            options.urgency = "soon"; options.priority = 60; options.groupKey = "gpu.driver";
            title = QStringLiteral("Около момента проблемы ошибся или перезапустился видеодрайвер");
            detail = QStringLiteral("Событие display/TDR может быть связано со сбоем изображения, но одна временная связь не доказывает причину.");
            options.actions = {QStringLiteral("Сопоставить время события с симптомами и версией видеодрайвера")};
        } else {
            options.priority = 45; options.groupKey = "applications.hang";
            title = QStringLiteral("ОС записала сбой или зависание приложения около отметки");
            detail = QStringLiteral("Категория включает зависания и ошибки приложения; конкретный тип уточняется по сообщению. Причина по одной записи не установлена.");
        }
        appendFinding(findings, "incident.logs." + definition.key, definition.key == "whea" ? "critical" : "warning",
            title, contextText + detail, options);
    }
}

[[nodiscard]] QString stressDiskType(
    const QJsonObject& stress,
    const QJsonObject& hardware)
{
    const auto explicitType = stress.value(QStringLiteral("disk_target_type")).toString();
    if (!explicitType.isEmpty()) {
        return explicitType.toUpper();
    }
    const auto targetMount = stress.value(QStringLiteral("disk_target_mountpoint")).toString();
    const auto targetDevice = stress.value(QStringLiteral("disk_target_device")).toString();
    for (const auto& value : hardware.value(QStringLiteral("disks")).toArray()) {
        const auto disk = value.toObject();
        if ((!targetMount.isEmpty()
             && disk.value(QStringLiteral("mountpoint")).toString() == targetMount)
            || (!targetDevice.isEmpty()
                && disk.value(QStringLiteral("device")).toString() == targetDevice)) {
            return disk.value(QStringLiteral("type")).toString().toUpper();
        }
    }
    return QStringLiteral("UNKNOWN");
}

void analyzeCausalChains(
    QJsonArray& findings,
    const QJsonObject& diagnostics,
    const QJsonObject& stress,
    const QJsonObject& hardware)
{
    const auto newErrors = stress.value(QStringLiteral("new_system_errors")).toArray();
    const int workerFailures = stress.value(QStringLiteral("cpu_worker_failures")).toInt();
    if (workerFailures > 0 && hasWhea(newErrors)) {
        FindingOptions options;
        options.domain = QStringLiteral("stability");
        options.status = QString::fromLatin1(kStatusFail);
        options.confidence = QStringLiteral("high");
        options.urgency = QStringLiteral("immediate");
        options.impact = QStringLiteral("stability");
        options.priority = 90;
        options.groupKey = QStringLiteral("stability.hardware");
        options.rootCause = QStringLiteral("hardware instability");
        appendFinding(
            findings,
            QStringLiteral("causal.hardware_instability"),
            QStringLiteral("critical"),
            QStringLiteral("Нагрузка вызвала сбой CPU-воркеров и новые аппаратные ошибки"),
            QStringLiteral("Сбой вычислительных воркеров совпал с новой WHEA/Machine Check ошибкой."),
            options);
    }

    const auto smartDisks = diagnostics.value(QStringLiteral("smart"))
                                .toObject()
                                .value(QStringLiteral("disks"))
                                .toArray();
    const bool validWrite = stress.value(QStringLiteral("run_disk")).toBool()
        && (stress.value(QStringLiteral("disk_fsync_applied")).toBool()
            || stress.value(QStringLiteral("disk_fsync_performed")).toBool())
        && !stress.value(QStringLiteral("disk_target_is_memory_fs")).toBool();
    const auto writeSpeed = number(stress, QStringLiteral("disk_write_mbps"));
    const auto diskType = stressDiskType(stress, hardware);
    const double threshold = diskType == QStringLiteral("SSD")
        ? 100.0
        : diskType == QStringLiteral("HDD") ? 25.0 : 20.0;
    const bool slowWrite = validWrite && writeSpeed.has_value() && *writeSpeed < threshold;
    QJsonArray logErrors = diagnostics.value(QStringLiteral("log_errors"))
                               .toObject()
                               .value(QStringLiteral("errors"))
                               .toArray();
    for (const auto& value : newErrors) {
        logErrors.append(value);
    }
    const auto targetDevice = stress.value(QStringLiteral("disk_target_device")).toString();
    QJsonObject riskyDisk;
    bool exactMatch = false;
    for (const auto& value : smartDisks) {
        const auto disk = value.toObject();
        if (!targetDevice.isEmpty()
            && disk.value(QStringLiteral("device")).toString() == targetDevice
            && !disk.value(QStringLiteral("risk_reasons")).toArray().isEmpty()) {
            riskyDisk = disk;
            exactMatch = true;
            break;
        }
    }
    if (riskyDisk.isEmpty() && smartDisks.size() == 1) {
        const auto candidate = smartDisks.first().toObject();
        if (!candidate.value(QStringLiteral("risk_reasons")).toArray().isEmpty()) {
            riskyDisk = candidate;
        }
    }
    if (!riskyDisk.isEmpty() && slowWrite && hasDiskError(logErrors)) {
        bool critical = false;
        for (const auto& value : riskyDisk.value(QStringLiteral("risk_reasons")).toArray()) {
            critical |= value.toObject().value(QStringLiteral("severity")).toString()
                == QStringLiteral("critical");
        }
        const auto device = riskyDisk.value(QStringLiteral("device"))
                                .toString(targetDevice.isEmpty()
                                              ? QStringLiteral("storage")
                                              : targetDevice);
        // Audited per FINAL-CHECKLIST.md ("storage-causal... правила"): three
        // sources agreeing is only as strong as each source's own reliability.
        // A degraded SMART read or a degraded/failed log collection must not be
        // presented as one of three confirmed, independent signals.
        const auto smartQuality = riskyDisk.value(QStringLiteral("data_quality")).toString();
        const auto logQuality = diagnostics.value(QStringLiteral("log_errors"))
                                     .toObject().value(QStringLiteral("data_quality")).toString();
        const bool smartFullyValid = smartQuality.isEmpty() || smartQuality == QStringLiteral("valid");
        const bool logFullyValid = logQuality.isEmpty() || logQuality == QStringLiteral("valid");
        const bool allSourcesFullyValid = smartFullyValid && logFullyValid;
        FindingOptions options;
        options.domain = QStringLiteral("storage");
        options.status = QString::fromLatin1(
            critical ? kStatusFail : kStatusWarning);
        options.confidence = (exactMatch && allSourcesFullyValid)
            ? QStringLiteral("high")
            : QStringLiteral("medium");
        options.urgency = critical ? QStringLiteral("immediate") : QStringLiteral("soon");
        options.impact = critical ? QStringLiteral("data_loss") : QStringLiteral("data_reliability");
        options.priority = 80;
        options.groupKey = QStringLiteral("storage.%1").arg(device);
        options.rootCause = QStringLiteral("storage degradation");
        options.affectedDevice = device;
        options.evidence = {
            makeEvidence(QStringLiteral("Совпадение устройства теста и SMART"), exactMatch,
                QStringLiteral("stress disk target vs SMART device")),
            makeEvidence(QStringLiteral("Скорость записи, МБ/с"), *writeSpeed, QStringLiteral("stress disk test")),
            makeEvidence(QStringLiteral("Порог для типа накопителя, МБ/с"), threshold, QStringLiteral("disk type profile")),
            makeEvidence(QStringLiteral("Качество SMART-снимка"),
                smartQuality.isEmpty() ? QStringLiteral("valid") : smartQuality, QStringLiteral("smartctl")),
            makeEvidence(QStringLiteral("Качество журнала"),
                logQuality.isEmpty() ? QStringLiteral("valid") : logQuality, QStringLiteral("system log collector")),
        };
        const QString qualifier = allSourcesFullyValid
            ? QString {}
            : QStringLiteral(" SMART-снимок или журнал собраны не полностью, поэтому это не три независимо подтверждённых источника.");
        appendFinding(
            findings,
            QStringLiteral("causal.storage.degradation"),
            critical ? QStringLiteral("critical") : QStringLiteral("warning"),
            QStringLiteral("%1: SMART, медленная запись и ошибки журнала совпали").arg(device),
            QStringLiteral("Три источника указывают на один накопитель.%1").arg(qualifier),
            options);
    }
}

void analyzeIncident(QJsonArray& findings, const QJsonObject& incident)
{
    if (incident.isEmpty()) {
        return;
    }
    const auto summary = incident.value(QStringLiteral("summary")).toObject();
    const bool timestamped = summary.value("measurement_contract") == kIncidentMeasurementContract;
    if (timestamped && summary.value("partial_metric_sample_count").toInt() > 0) {
        FindingOptions options;
        options.domain = "coverage"; options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = "high"; options.groupKey = "coverage.incident.provenance";
        options.evidence = {makeEvidence(QStringLiteral("Отброшено устаревших показателей"), summary.value("stale_metric_count"), "producer timestamps"),
            makeEvidence(QStringLiteral("Повторных сетевых интервалов"), summary.value("repeated_interval_count"), "producer timestamps"),
            makeEvidence(QStringLiteral("Устаревших контекстов приложения"), summary.value("stale_app_sample_count"), "process snapshot timestamps")};
        appendFinding(findings, "coverage.incident.provenance", "info",
            QStringLiteral("Часть показателей инцидента недоступна или исключена"),
            QStringLiteral("Проверены исходное время, качество и паузы. Неизвестные значения не заменяются нулями; совпадения требуют свежих данных одного замера с разбросом времени не более 1 секунды."), options);
    }
    const auto quality = summary.value(QStringLiteral("data_quality"))
                             .toString(QStringLiteral("unknown"));
    const auto runtime = incident.value(QStringLiteral("runtime_memory")).toObject();
    const auto app = summary.value(QStringLiteral("app")).toObject();
    const auto appShare = number(
        app.value(QStringLiteral("ram_share_percent")).toObject(),
        QStringLiteral("max"));
    if (incident.value(QStringLiteral("status")).toString() == QStringLiteral("cancelled")
        || quality != QStringLiteral("valid")) {
        FindingOptions options;
        options.domain = QStringLiteral("coverage");
        options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = QStringLiteral("high");
        options.urgency = QStringLiteral("monitor");
        options.groupKey = QStringLiteral("coverage.incident");
        options.evidence = {
            makeEvidence(QStringLiteral("Качество окна"), quality,
                QStringLiteral("runtime ring buffer"), quality),
            makeEvidence(QStringLiteral("Замеров до отметки"),
                summary.value(QStringLiteral("before_sample_count")), QStringLiteral("runtime ring buffer")),
            makeEvidence(QStringLiteral("Замеров после отметки"),
                summary.value(QStringLiteral("after_sample_count")), QStringLiteral("runtime ring buffer")),
        };
        appendFinding(findings, QStringLiteral("coverage.incident.window"),
            QStringLiteral("info"),
            QStringLiteral("Момент проблемы зафиксирован с неполным покрытием"),
            QStringLiteral("Временное окно заполнено не полностью; отсутствие события в пропущенном интервале не считается доказательством нормы."),
            options);
    }
    const auto missingMetrics = summary.value(QStringLiteral("missing_metrics")).toArray();
    if (!missingMetrics.isEmpty()) {
        QStringList labels;
        for (const auto& value : missingMetrics) labels.append(value.toString());
        FindingOptions options;
        options.domain = QStringLiteral("coverage");
        options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = QStringLiteral("high");
        options.groupKey = QStringLiteral("coverage.incident.metrics");
        appendFinding(findings, QStringLiteral("coverage.incident.metrics"),
            QStringLiteral("info"), QStringLiteral("Не все метрики доступны около момента проблемы"),
            QStringLiteral("Нет валидных показаний: %1.").arg(labels.join(QStringLiteral(", "))),
            options);
    }

    analyzeIncidentLogs(findings, incident);
    if (timestamped ? summary.value("app_contributor_sample_count").toInt() > 0 :
        (runtime.value(QStringLiteral("memory_pressure")).toString()
            == QStringLiteral("confirmed")
        && appShare.value_or(0.0) >= 15.0)) {
        FindingOptions options;
        options.domain = QStringLiteral("memory");
        options.status = QString::fromLatin1(kStatusFail);
        options.confidence = !timestamped && runtime.value(QStringLiteral("data_quality")).toString()
                == QStringLiteral("valid")
            ? QStringLiteral("high")
            : QStringLiteral("medium");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("responsiveness");
        options.priority = 75;
        options.groupKey = QStringLiteral("memory");
        options.rootCause = QStringLiteral("memory pressure");
        if (timestamped) options.evidence = {makeEvidence(QStringLiteral("Совпавших замеров RAM, подкачки и приложения"),
            summary.value("app_contributor_sample_count"), "fresh producer timestamps", "estimated"),
            makeEvidence(QStringLiteral("Совпавшие наблюдения"), summary.value("app_contributor_observations"), "fresh incident samples", "estimated")};
        appendFinding(
            findings,
            QStringLiteral("incident.memory.app_contributor"),
            QStringLiteral("critical"),
            timestamped ? QStringLiteral("Приложение занимало значительную RAM при дефиците памяти")
                        : QStringLiteral("Приложение усилило дефицит RAM и подкачку"),
            timestamped ? QStringLiteral("Доля RAM приложения, дефицит памяти и активные чтения страниц совпали в свежем замере. Это наблюдаемая связь, не доказательство причины зависания или утечки.")
                        : QStringLiteral("Доля памяти приложения совпала с подтверждённым memory pressure."),
            options);
    } else {
        const auto memoryPressure = summary.value(QStringLiteral("focus_memory_pressure")).toString(
            runtime.value(QStringLiteral("memory_pressure")).toString());
        const auto focusRam = nestedNumber(summary, QStringLiteral("focus"),
            QStringLiteral("ram_used_percent"), QStringLiteral("max"));
        if (memoryPressure == QStringLiteral("confirmed")) {
            FindingOptions options;
            options.domain = QStringLiteral("memory");
            options.status = QString::fromLatin1(kStatusFail);
            options.confidence = !timestamped && quality == QStringLiteral("valid")
                ? QStringLiteral("high") : QStringLiteral("medium");
            options.urgency = QStringLiteral("soon");
            options.impact = QStringLiteral("responsiveness");
            options.priority = 65;
            options.groupKey = QStringLiteral("memory");
            options.rootCause = QStringLiteral("memory pressure");
            options.evidence = {makeEvidence(QStringLiteral("Пик RAM в фокусе, %"),
                focusRam.has_value() ? QJsonValue {*focusRam} : QJsonValue {QJsonValue::Null},
                QStringLiteral("runtime ring buffer"))};
            if (timestamped) options.evidence = {makeEvidence(QStringLiteral("Совпавших замеров дефицита RAM и чтения страниц"),
                summary.value("memory_pressure_sample_count"), "fresh producer timestamps", "estimated"),
                makeEvidence(QStringLiteral("Совпавшие наблюдения"), summary.value("memory_pressure_observations"), "fresh incident samples", "estimated")};
            appendFinding(findings, QStringLiteral("incident.memory.pressure"), QStringLiteral("critical"),
                QStringLiteral("В момент проблемы подтверждено давление на оперативную память"),
                timestamped ? QStringLiteral("Низкая доступная RAM и активные чтения страниц совпали в одном свежем замере около отметки. Счётчики косвенные; причина зависания этим не доказана.")
                            : QStringLiteral("Низкая доступная RAM и активная подкачка совпали с пользовательской отметкой."), options);
        } else if (memoryPressure == QStringLiteral("possible") || focusRam.value_or(0.0) >= 95.0) {
            FindingOptions options;
            options.domain = QStringLiteral("memory");
            options.confidence = QStringLiteral("medium");
            options.urgency = QStringLiteral("soon");
            options.impact = QStringLiteral("responsiveness");
            options.priority = 45;
            options.groupKey = QStringLiteral("memory");
            appendFinding(findings, QStringLiteral("incident.memory.possible"), QStringLiteral("warning"),
                QStringLiteral("В момент проблемы RAM была почти исчерпана"),
                QStringLiteral("Высокое потребление памяти совпало с отметкой, но активную подкачку подтвердить полностью не удалось."), options);
        }
    }

    const auto cpuAverage = nestedNumber(
        summary,
        QStringLiteral("focus"),
        QStringLiteral("cpu_usage_percent"),
        QStringLiteral("avg"));
    const auto cpuTemperature = nestedNumber(
        summary,
        QStringLiteral("focus"),
        QStringLiteral("cpu_temp_c"),
        QStringLiteral("max"));
    const auto frequencyDrop = number(summary, QStringLiteral("cpu_frequency_drop_percent"));
    const auto temperatureLevel = orion::core::levelForTemperature(cpuTemperature, "cpu");
    const bool thermalMatch = timestamped ? summary.value("cpu_thermal_sample_count").toInt() > 0 :
        ((temperatureLevel == orion::core::StatusLevel::Warning
         || temperatureLevel == orion::core::StatusLevel::Critical)
        && frequencyDrop.value_or(0.0) >= 10.0
        && cpuAverage.value_or(0.0) >= 60.0);
    if (thermalMatch) {
        const bool critical = timestamped ? summary.value("cpu_thermal_critical_sample_count").toInt() > 0 :
            temperatureLevel == orion::core::StatusLevel::Critical && frequencyDrop.value_or(0.0) >= 20.0;
        FindingOptions options;
        options.domain = QStringLiteral("cooling");
        options.confidence = !timestamped && quality == QStringLiteral("valid")
            ? QStringLiteral("high")
            : QStringLiteral("medium");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("performance_and_stability");
        options.priority = 70;
        options.groupKey = QStringLiteral("cooling.cpu");
        options.rootCause = timestamped ? QStringLiteral("possible thermal throttling") : QStringLiteral("thermal throttling");
        if (timestamped) options.evidence = {makeEvidence(QStringLiteral("Совпавших замеров нагрева, нагрузки и снижения частоты"),
            summary.value("cpu_thermal_sample_count"), "fresh producer timestamps", "estimated"),
            makeEvidence(QStringLiteral("Совпавшие наблюдения"), summary.value("cpu_thermal_observations"), "fresh incident samples", "estimated")};
        appendFinding(
            findings,
            QStringLiteral("incident.cpu.thermal_throttling"),
            critical ? QStringLiteral("critical") : QStringLiteral("warning"),
            QStringLiteral("В момент проблемы CPU нагрелся и снизил частоту"),
            timestamped ? QStringLiteral("Нагрев, нагрузка и падение частоты относительно baseline совпали в свежем замере. Аппаратный флаг троттлинга не проверен; причинная связь остаётся предположением.")
                        : QStringLiteral("Нагрев, нагрузка и падение частоты совпали в одном временном окне."),
            options);
    } else {
        const auto cpuMaximum = nestedNumber(summary, QStringLiteral("focus"),
            QStringLiteral("cpu_usage_percent"), QStringLiteral("max"));
        const auto cpuHighFraction = nestedNumber(summary, "focus", "cpu_usage_percent", "above_90_fraction");
        const auto cpuCount = nestedNumber(summary, "focus", "cpu_usage_percent", "count");
        if (cpuMaximum.value_or(0.0) >= 95.0 && cpuHighFraction.value_or(0.0) >= 0.4
            && cpuHighFraction.value_or(0.0) <= 1.0 && (!cpuCount || *cpuCount > 0)) {
            FindingOptions options;
            options.domain = QStringLiteral("cpu");
            options.confidence = quality == QStringLiteral("valid") && cpuCount.value_or(0) >= 2
                ? QStringLiteral("high") : QStringLiteral("medium");
            options.impact = QStringLiteral("responsiveness");
            options.priority = 40;
            options.groupKey = QStringLiteral("cpu.load");
            const auto cpuStats = summary.value("focus").toObject().value("cpu_usage_percent").toObject();
            options.evidence = {
                makeEvidence(QStringLiteral("Средняя загрузка CPU, %"), cpuStats.value("avg"), "runtime ring buffer", quality),
                makeEvidence(QStringLiteral("Пиковая загрузка CPU, %"), cpuStats.value("max"), "runtime ring buffer", quality),
                makeEvidence(QStringLiteral("Доля известных замеров CPU ≥90%"), *cpuHighFraction, "accepted sample rows", quality),
                makeEvidence(QStringLiteral("Известных замеров CPU"), cpuStats.value("count"), "accepted sample rows", quality),
            };
            appendFinding(findings, QStringLiteral("incident.cpu.saturation"), QStringLiteral("warning"),
                QStringLiteral("В момент проблемы CPU был почти полностью занят"),
                QStringLiteral("Пик CPU достиг 95%, а не менее 40% известных замеров в фокусе имели загрузку ≥90%. Это доля замеров, не доля времени и не доказательство длительной непрерывной нагрузки. Перегрев и причина сбоя этим не подтверждены."), options);
        }
    }

    const auto gpuMaximum = nestedNumber(summary, QStringLiteral("focus"),
        QStringLiteral("gpu_usage_percent"), QStringLiteral("max"));
    if (gpuMaximum.value_or(0.0) >= 95.0) {
        FindingOptions options;
        options.domain = QStringLiteral("gpu");
        options.confidence = QStringLiteral("medium");
        options.impact = QStringLiteral("performance");
        options.priority = 35;
        options.groupKey = QStringLiteral("gpu.load");
        appendFinding(findings, QStringLiteral("incident.gpu.saturation"), QStringLiteral("warning"),
            QStringLiteral("В момент проблемы GPU работал на пределе"),
            QStringLiteral("Загрузка GPU совпала с отметкой; без frame-time это не доказывает троттлинг, но объясняет просадку FPS."), options);
    }
    const auto pingMaximum = nestedNumber(summary, QStringLiteral("focus"),
        QStringLiteral("net_ping_ms"), QStringLiteral("max"));
    const auto networkErrors = number(summary, QStringLiteral("focus_net_error_count")).value_or(0.0);
    const auto networkDrops = number(summary, QStringLiteral("focus_net_drop_count")).value_or(0.0);
    if (pingMaximum.value_or(0.0) >= 200.0 || networkErrors > 0.0 || networkDrops > 0.0) {
        FindingOptions options;
        options.domain = QStringLiteral("network");
        options.confidence = QStringLiteral("medium");
        options.impact = QStringLiteral("network_responsiveness");
        options.priority = 35;
        options.groupKey = QStringLiteral("network.quality");
        appendFinding(findings, QStringLiteral("incident.network.degradation"), QStringLiteral("warning"),
            QStringLiteral("В момент проблемы ухудшилось сетевое соединение"),
            QStringLiteral("Высокий пинг и/или ошибки пакетов совпали с отметкой."), options);
    }
}

void analyzeAppMonitor(QJsonArray& findings, const QJsonObject& report)
{
    if (report.isEmpty()) {
        return;
    }
    const QString exeName = QFileInfo(report.value(QStringLiteral("exe_path")).toString()).fileName().isEmpty()
        ? QStringLiteral("Приложение")
        : QFileInfo(report.value(QStringLiteral("exe_path")).toString()).fileName();
    if (!report.value(QStringLiteral("launch_error")).toString().isEmpty()) {
        FindingOptions options;
        options.domain = QStringLiteral("coverage");
        options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = QStringLiteral("high");
        options.impact = QStringLiteral("test_validity");
        options.groupKey = QStringLiteral("coverage.app_monitor");
        appendFinding(findings, QStringLiteral("coverage.app_monitor.launch"), QStringLiteral("info"),
            QStringLiteral("%1: наблюдение не началось").arg(exeName),
            QStringLiteral("Не удалось запустить приложение: %1")
                .arg(report.value(QStringLiteral("launch_error")).toString()), options);
        return;
    }
    const auto summary = report.value(QStringLiteral("summary")).toObject();
    const int sampleCount = summary.value(QStringLiteral("sample_count")).toInt(
        report.value(QStringLiteral("samples")).toArray().size());
    if (sampleCount == 0) {
        FindingOptions options;
        options.domain = QStringLiteral("coverage");
        options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = QStringLiteral("high");
        options.impact = QStringLiteral("test_validity");
        options.groupKey = QStringLiteral("coverage.app_monitor");
        appendFinding(findings, QStringLiteral("coverage.app_monitor.samples"), QStringLiteral("info"),
            QStringLiteral("%1: недостаточно данных наблюдения").arg(exeName),
            QStringLiteral("Процесс завершился или мониторинг был остановлен до первого валидного замера."), options);
        return;
    }

    if (report.contains(QStringLiteral("system_errors_checked_after"))
        && !report.value(QStringLiteral("system_errors_checked_after")).toBool()) {
        FindingOptions options;
        options.domain = QStringLiteral("coverage");
        options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = QStringLiteral("high");
        options.impact = QStringLiteral("test_validity");
        options.groupKey = QStringLiteral("coverage.app_monitor.logs");
        const QString quality = report.value(QStringLiteral("system_error_comparison_quality"))
                                    .toString(QStringLiteral("collector_error"));
        options.evidence = {makeEvidence(QStringLiteral("Качество сравнения журнала"), quality,
            QStringLiteral("before/after system log diff"), quality)};
        appendFinding(findings, QStringLiteral("coverage.app_monitor.system_log"), QStringLiteral("info"),
            QStringLiteral("%1: журнал ОС до/после нельзя надёжно сравнить").arg(exeName),
            QStringLiteral("Отсутствие новой WHEA, ошибки драйвера или AppHang не считается подтверждённой нормой."), options);
    }

    const bool timestampedIntervals = report.value("measurement_contract") == "identified_adjacent_intervals_v1";
    const auto observations = report.value("samples").toArray();
    const auto coincides = [&observations](const auto& predicate) {
        for (const auto& value : observations) if (predicate(value.toObject())) return true;
        return false;
    };
    if (timestampedIntervals) {
        const auto endpoints = report.value("runtime_memory").toObject();
        if (summary.value("system_unavailable_sample_count").toInt() > 0
            || endpoints.value("data_quality") != "valid") {
            FindingOptions options;
            options.domain = QStringLiteral("coverage");
            options.status = QString::fromLatin1(kStatusUnknown);
            options.confidence = QStringLiteral("high");
            options.groupKey = QStringLiteral("coverage.app_monitor.system_freshness");
            appendFinding(findings, QStringLiteral("coverage.app_monitor.system_freshness"), QStringLiteral("info"),
                QStringLiteral("Системные показатели наблюдения доступны не полностью"),
                QStringLiteral("Устаревшие или несвоевременные снимки исключены; отсутствие показателя не означает норму."), options);
        }
        FindingOptions options;
        options.domain = QStringLiteral("coverage");
        options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = QStringLiteral("high");
        options.groupKey = QStringLiteral("coverage.app_monitor.io");
        appendFinding(findings, QStringLiteral("coverage.app_monitor.io_intervals"), QStringLiteral("info"),
            QStringLiteral("I/O: учтены только измеренные интервалы"),
            QStringLiteral("Начальная активность, паузы, недоступные замеры и хвост завершившихся процессов не включены. Итог — нижняя граница; скорость дерева требует полного соседнего замера."), options);
    }
    const auto newErrors = report.value(QStringLiteral("new_system_errors")).toArray();
    const bool crashed = report.value(QStringLiteral("exited_early")).toBool()
        && !report.value(QStringLiteral("exit_code")).isNull()
        && report.value(QStringLiteral("exit_code")).toInt() != 0;
    if (crashed && hasWhea(newErrors)) {
        FindingOptions options;
        options.domain = QStringLiteral("stability");
        options.status = QString::fromLatin1(kStatusFail);
        options.confidence = QStringLiteral("high");
        options.urgency = QStringLiteral("immediate");
        options.impact = QStringLiteral("stability");
        options.priority = 85;
        options.groupKey = QStringLiteral("stability.hardware");
        options.rootCause = QStringLiteral("hardware instability");
        appendFinding(
            findings,
            QStringLiteral("app.crash.whea"),
            QStringLiteral("critical"),
            QStringLiteral("Приложение аварийно завершилось вместе с новой WHEA-ошибкой"),
            QStringLiteral("Код выхода и аппаратное событие появились в одной сессии."),
            options);
    } else if (crashed && hasDisplayError(newErrors)) {
        FindingOptions options;
        options.domain = QStringLiteral("gpu");
        options.confidence = QStringLiteral("high");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("stability");
        options.priority = 75;
        options.groupKey = QStringLiteral("gpu.driver");
        options.evidence = {makeEvidence(QStringLiteral("Код выхода"),
            report.value(QStringLiteral("exit_code")), QStringLiteral("process exit code"))};
        options.actions = {QStringLiteral("Выполнить чистую переустановку или откат видеодрайвера")};
        appendFinding(findings, QStringLiteral("app.crash.display"), QStringLiteral("critical"),
            QStringLiteral("%1 аварийно завершилось вместе с ошибкой видеодрайвера").arg(exeName),
            QStringLiteral("Код выхода и новое событие display/TDR совпали по одной сессии."), options);
    } else if (crashed) {
        FindingOptions options;
        options.domain = QStringLiteral("applications");
        options.confidence = QStringLiteral("high");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("stability");
        appendFinding(
            findings,
            QStringLiteral("app.crash"),
            QStringLiteral("warning"),
            QStringLiteral("Приложение аварийно завершилось"),
            QStringLiteral("Причина сбоя не установлена."),
            options);
    }

    const auto runtime = report.value(QStringLiteral("runtime_memory")).toObject();
    auto appShare = number(summary, QStringLiteral("ram_share_peak_percent"));
    bool pressureConfirmed = runtime.value(QStringLiteral("memory_pressure")).toString() == QStringLiteral("confirmed");
    if (timestampedIntervals) {
        double concurrentShare = 0;
        bool concurrentPressure = false;
        for (const auto& value : observations) {
            const auto row = value.toObject();
            const auto share = number(row, QStringLiteral("ram_share_percent"));
            if (row.value("memory_pressure") == "confirmed" && share) {
                concurrentShare = std::max(concurrentShare, *share);
                concurrentPressure = true;
            }
        }
        appShare = concurrentShare;
        pressureConfirmed = concurrentPressure;
    }
    if (pressureConfirmed
        && appShare.value_or(0.0) >= 15.0) {
        FindingOptions options;
        options.domain = QStringLiteral("memory");
        options.status = QString::fromLatin1(kStatusFail);
        options.confidence = runtime.value(QStringLiteral("data_quality")).toString() == QStringLiteral("valid")
            ? QStringLiteral("high") : QStringLiteral("medium");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("responsiveness");
        options.priority = 70;
        options.groupKey = QStringLiteral("memory");
        options.rootCause = QStringLiteral("application memory pressure");
        options.affectedDevice = exeName;
        options.evidence = {
            makeEvidence(QStringLiteral("Пик RSS дерева, МБ"), summary.value(QStringLiteral("ram_peak_mb")), QStringLiteral("process tree monitor")),
            makeEvidence(QStringLiteral("Доля физической RAM, %"), *appShare, QStringLiteral("process tree monitor")),
        };
        appendFinding(findings, QStringLiteral("app.memory.pressure_contributor"), QStringLiteral("critical"),
            QStringLiteral("%1 заметно участвовало в подтверждённом дефиците RAM").arg(exeName),
            QStringLiteral("Дерево процессов занимало заметную долю физической памяти одновременно с низкой доступной RAM и paging."), options);
    }

    const double observedSeconds = number(summary, QStringLiteral("observed_seconds")).value_or(0.0);
    const auto privateGrowth = number(summary, QStringLiteral("private_growth_mb"));
    const auto privateSlope = number(summary, QStringLiteral("private_growth_mb_per_min"));
    const auto privateR2 = number(summary, QStringLiteral("private_growth_r2"));
    const auto privateTail = number(summary, QStringLiteral("private_tail_growth_mb"));
    if (observedSeconds >= 20.0 * 60.0 && privateGrowth.value_or(0.0) >= 300.0
        && privateSlope.value_or(0.0) >= 10.0 && privateR2.value_or(0.0) >= 0.65
        && privateTail.value_or(0.0) >= 150.0) {
        FindingOptions options;
        options.domain = QStringLiteral("applications");
        options.confidence = observedSeconds >= 60.0 * 60.0 && *privateR2 >= 0.8
            ? QStringLiteral("high") : QStringLiteral("medium");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("performance_and_stability");
        options.priority = 66;
        options.groupKey = QStringLiteral("applications.%1.memory").arg(exeName);
        options.affectedDevice = exeName;
        options.evidence = {
            makeEvidence(QStringLiteral("Рост private memory, МБ"), *privateGrowth, QStringLiteral("process memory counters")),
            makeEvidence(QStringLiteral("Тренд, МБ/мин"), *privateSlope, QStringLiteral("linear regression")),
            makeEvidence(QStringLiteral("R² тренда"), *privateR2, QStringLiteral("linear regression")),
            makeEvidence(QStringLiteral("Рост последней четверти, МБ"), *privateTail, QStringLiteral("quarter comparison")),
        };
        options.actions = {QStringLiteral("Повторить одинаковый цикл и проверить, выходит ли private memory на плато")};
        appendFinding(findings, QStringLiteral("app.memory.private_growth"), QStringLiteral("warning"),
            QStringLiteral("%1: private memory устойчиво растёт").arg(exeName),
            QStringLiteral("Рост сохраняется в последней четверти длительной сессии и хорошо описывается линейным трендом."), options);
    }

    const auto handleGrowth = number(summary, QStringLiteral("handle_growth"));
    const auto handleSlope = number(summary, QStringLiteral("handle_growth_per_min"));
    const auto handleR2 = number(summary, QStringLiteral("handle_growth_r2"));
    if (observedSeconds >= 20.0 * 60.0 && handleGrowth.value_or(0.0) >= 500.0
        && handleSlope.value_or(0.0) >= 10.0 && (!handleR2.has_value() || *handleR2 >= 0.55)) {
        FindingOptions options;
        options.domain = QStringLiteral("applications");
        options.confidence = QStringLiteral("medium");
        options.impact = QStringLiteral("stability");
        options.priority = 55;
        options.groupKey = QStringLiteral("applications.%1.resources").arg(exeName);
        options.affectedDevice = exeName;
        options.evidence = {
            makeEvidence(QStringLiteral("Рост handles"), *handleGrowth, QStringLiteral("process tree counters")),
            makeEvidence(QStringLiteral("Скорость роста в минуту"), *handleSlope, QStringLiteral("linear regression")),
        };
        appendFinding(findings, QStringLiteral("app.resources.handle_growth"), QStringLiteral("warning"),
            QStringLiteral("%1: возможна утечка handles/дескрипторов").arg(exeName),
            QStringLiteral("Количество handles заметно и устойчиво увеличивалось в длительной сессии."), options);
    }

    const auto availableMinimum = number(summary, QStringLiteral("system_available_min_percent"));
    const auto commitPeak = number(summary, QStringLiteral("system_commit_peak_percent"));
    const auto hungFraction = number(summary, QStringLiteral("ui_hung_fraction"));
    const auto hungStreak = number(summary, QStringLiteral("ui_hung_max_streak_seconds"));
    const bool hungConfirmed = hungStreak.value_or(0.0) >= 8.0 || hungFraction.value_or(0.0) >= 0.2;
    const bool memoryNearHang = timestampedIntervals ? coincides([](const QJsonObject& row) {
        return row.value("ui_hung").toBool() && (row.value("memory_pressure") == "confirmed"
            || number(row, QStringLiteral("system_available_percent")).value_or(100) <= 8
            || number(row, QStringLiteral("system_commit_used_percent")).value_or(0) >= 90);
    }) : runtime.value(QStringLiteral("memory_pressure")).toString() == QStringLiteral("confirmed")
        || availableMinimum.value_or(100.0) <= 8.0 || commitPeak.value_or(0.0) >= 90.0;
    if (hungConfirmed) {
        FindingOptions options;
        options.domain = memoryNearHang ? QStringLiteral("memory") : QStringLiteral("applications");
        options.confidence = QStringLiteral("high");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("responsiveness");
        options.priority = memoryNearHang ? 72 : 62;
        options.groupKey = memoryNearHang ? QStringLiteral("memory")
            : QStringLiteral("applications.%1.hang").arg(exeName);
        options.affectedDevice = exeName;
        options.evidence = {
            makeEvidence(QStringLiteral("Максимальная серия без отклика, с"),
                hungStreak.has_value() ? QJsonValue {*hungStreak} : QJsonValue {QJsonValue::Null}, QStringLiteral("IsHungAppWindow")),
            makeEvidence(QStringLiteral("Доля замеров без отклика"),
                hungFraction.has_value() ? QJsonValue {*hungFraction} : QJsonValue {QJsonValue::Null}, QStringLiteral("IsHungAppWindow")),
        };
        appendFinding(findings,
            memoryNearHang ? QStringLiteral("app.ui.hung_memory_pressure") : QStringLiteral("app.ui.hung"),
            memoryNearHang && hungStreak.value_or(0.0) >= 15.0 ? QStringLiteral("critical") : QStringLiteral("warning"),
            memoryNearHang ? QStringLiteral("%1: окно не отвечало на фоне дефицита памяти").arg(exeName)
                           : QStringLiteral("%1: окно длительно не отвечало").arg(exeName),
            QStringLiteral("Windows IsHungAppWindow подтверждал отсутствие отклика окна в нескольких замерах."), options);
    }

    if (hasMemoryExhaustion(newErrors)) {
        FindingOptions options;
        options.domain = QStringLiteral("memory");
        options.status = QString::fromLatin1(kStatusFail);
        options.confidence = QStringLiteral("high");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("stability");
        options.priority = 82;
        options.groupKey = QStringLiteral("memory");
        options.rootCause = QStringLiteral("commit exhaustion");
        appendFinding(findings, QStringLiteral("app.memory.resource_exhaustion_event"), QStringLiteral("critical"),
            QStringLiteral("%1: Windows зафиксировала исчерпание памяти/commit").arg(exeName),
            QStringLiteral("После запуска появилось событие Resource-Exhaustion/Out of memory."), options);
    }
    const auto faultsPeak = number(summary, QStringLiteral("page_faults_peak_per_sec"));
    const bool faultPressure = timestampedIntervals ? coincides([](const QJsonObject& row) {
        return number(row, QStringLiteral("page_faults_per_sec")).value_or(0) >= 5000
            && (number(row, QStringLiteral("system_available_percent")).value_or(100) <= 8
                || number(row, QStringLiteral("system_commit_used_percent")).value_or(0) >= 90);
    }) : faultsPeak.value_or(0.0) >= 5000.0
        && (availableMinimum.value_or(100.0) <= 8.0 || commitPeak.value_or(0.0) >= 90.0);
    if (faultPressure) {
        FindingOptions options;
        options.domain = QStringLiteral("memory");
        options.status = QString::fromLatin1(kStatusFail);
        options.confidence = QStringLiteral("high");
        options.impact = QStringLiteral("responsiveness");
        options.priority = 72;
        options.groupKey = QStringLiteral("memory");
        appendFinding(findings, QStringLiteral("app.memory.fault_pressure"), QStringLiteral("critical"),
            QStringLiteral("%1: всплеск page faults совпал с дефицитом памяти").arg(exeName),
            QStringLiteral("Высокая частота faults совпала с низкой доступной RAM или почти исчерпанным commit."), options);
    }

    const auto termination = report.value(QStringLiteral("termination")).toObject();
    if (report.value(QStringLiteral("timed_out")).toBool()
        && report.value(QStringLiteral("close_on_timeout")).toBool()
        && termination.value(QStringLiteral("requested")).toBool()
        && !termination.value(QStringLiteral("all_exited")).toBool()) {
        FindingOptions options;
        options.domain = QStringLiteral("applications");
        options.confidence = QStringLiteral("high");
        options.impact = QStringLiteral("stability");
        options.groupKey = QStringLiteral("applications.%1.termination").arg(exeName);
        appendFinding(findings, QStringLiteral("app.termination.incomplete"), QStringLiteral("warning"),
            QStringLiteral("%1: не все процессы закрылись автоматически").arg(exeName),
            QStringLiteral("После WM_CLOSE и принудительного завершения часть дерева процессов осталась активной."), options);
    }

    const auto cpuAverage = number(summary, QStringLiteral("cpu_avg_percent"));
    const auto cpuPeak = number(summary, QStringLiteral("cpu_peak_percent"));
    const auto highFraction = number(summary, QStringLiteral("cpu_high_fraction"));
    const int cpuSampleCount = summary.value(QStringLiteral("sample_count")).toInt();
    // Audited against the incident.cpu.saturation pattern (Stage 49): a handful of
    // samples averaging high is not the same claim as a sustained session, so this
    // requires a minimum known sample count and caps confidence accordingly instead
    // of always reporting "high" from as few as one or two rows.
    if (cpuAverage.value_or(0.0) >= 80.0 && highFraction.value_or(0.0) >= 0.6
        && cpuSampleCount >= 3) {
        FindingOptions options;
        options.domain = QStringLiteral("applications");
        options.confidence = cpuSampleCount >= 10 && observedSeconds >= 30.0
            ? QStringLiteral("high") : QStringLiteral("medium");
        options.impact = QStringLiteral("performance");
        options.priority = 35;
        options.groupKey = QStringLiteral("applications.%1.cpu").arg(exeName);
        options.affectedDevice = exeName;
        options.evidence = {
            makeEvidence(QStringLiteral("Средняя загрузка CPU дерева, %"), *cpuAverage, QStringLiteral("process tree monitor")),
            makeEvidence(QStringLiteral("Пиковая загрузка CPU дерева, %"),
                cpuPeak.has_value() ? QJsonValue {*cpuPeak} : QJsonValue {QJsonValue::Null}, QStringLiteral("process tree monitor")),
            makeEvidence(QStringLiteral("Доля известных замеров ≥80%"), *highFraction, QStringLiteral("process tree monitor")),
            makeEvidence(QStringLiteral("Известных замеров CPU"), cpuSampleCount, QStringLiteral("process tree monitor")),
        };
        appendFinding(findings, QStringLiteral("app.cpu.sustained"), QStringLiteral("warning"),
            QStringLiteral("%1: длительная высокая CPU-нагрузка").arg(exeName),
            QStringLiteral("Загрузка агрегирована по родительскому и дочерним процессам за %1 известных замеров; "
                "это доля замеров, а не подтверждённая непрерывная длительность. Для игры или рендера это может быть ожидаемо.")
                .arg(cpuSampleCount), options);
    }
    const auto diskBusy = number(summary, QStringLiteral("system_disk_busy_peak_percent"));
    const auto readPeak = number(summary, QStringLiteral("read_peak_mbps"));
    const auto writePeak = number(summary, QStringLiteral("write_peak_mbps"));
    const bool ioPressure = timestampedIntervals ? coincides([](const QJsonObject& row) {
        return number(row, QStringLiteral("system_disk_busy_percent")).value_or(0) >= 80
            && std::max(number(row, QStringLiteral("read_mbps")).value_or(0),
                        number(row, QStringLiteral("write_mbps")).value_or(0)) >= 20;
    }) : diskBusy.value_or(0.0) >= 80.0
        && std::max(readPeak.value_or(0.0), writePeak.value_or(0.0)) >= 20.0;
    if (ioPressure) {
        FindingOptions options;
        options.domain = QStringLiteral("applications");
        options.confidence = QStringLiteral("medium");
        options.impact = QStringLiteral("responsiveness");
        options.priority = 40;
        options.groupKey = QStringLiteral("applications.%1.io").arg(exeName);
        options.affectedDevice = exeName;
        appendFinding(findings, QStringLiteral("app.io.pressure"), QStringLiteral("warning"),
            QStringLiteral("%1: высокая I/O-нагрузка совпала с занятостью диска").arg(exeName),
            QStringLiteral("Приложение активно читало или писало, пока системный накопитель был занят."), options);
    }
}

void analyzeDiagnostics(QJsonArray& findings, const QJsonObject& diagnostics)
{
    if (diagnostics.contains(QStringLiteral("smart"))) {
        const auto smart = diagnostics.value(QStringLiteral("smart")).toObject();
        if (!smart.value(QStringLiteral("available")).toBool()) {
            FindingOptions options;
            options.domain = QStringLiteral("coverage");
            options.status = QString::fromLatin1(kStatusUnknown);
            options.confidence = QStringLiteral("high");
            options.urgency = QStringLiteral("monitor");
            options.groupKey = QStringLiteral("coverage.smart");
            options.sourceChecks = {
                QStringLiteral("smartctl --scan -j"),
                QStringLiteral("smartctl -a -j <device>"),
            };
            QString detail = smart.value(QStringLiteral("note")).toString(
                QStringLiteral("SMART недоступен"));
            detail += QStringLiteral(". Здоровье накопителей не проверено.");
            const auto hint = smart.value(QStringLiteral("install_hint")).toString();
            if (!hint.isEmpty()) detail += QStringLiteral(" Установка: %1").arg(hint);
            appendFinding(findings, QStringLiteral("coverage.smart.unavailable"),
                QStringLiteral("info"), QStringLiteral("SMART-диагностика не выполнена"),
                detail, options);
        }
    }
    QString logQuality;
    if (diagnostics.contains(QStringLiteral("log_errors"))) {
        const auto logs = diagnostics.value(QStringLiteral("log_errors")).toObject();
        logQuality = logs.value(QStringLiteral("data_quality")).toString();
        if (!logQuality.isEmpty() && logQuality != QStringLiteral("valid")) {
            FindingOptions options;
            options.domain = QStringLiteral("coverage");
            options.status = QString::fromLatin1(kStatusUnknown);
            options.confidence = QStringLiteral("high");
            options.urgency = QStringLiteral("monitor");
            options.impact = QStringLiteral("test_validity");
            options.groupKey = QStringLiteral("coverage.system_log");
            options.evidence.append(makeEvidence(
                QStringLiteral("Качество источника"), logQuality,
                logs.value(QStringLiteral("source")).toString(QStringLiteral("system log collector")),
                logQuality));
            appendFinding(findings, QStringLiteral("coverage.system_log"),
                QStringLiteral("info"), QStringLiteral("Системный журнал проверен не полностью"),
                QStringLiteral("Отсутствие записей не подтверждает, что аппаратных, драйверных или дисковых событий не было."),
                options);
        }
    }

    const auto logValues = diagnostics.value(QStringLiteral("log_errors"))
                               .toObject().value(QStringLiteral("errors")).toArray();
    QStringList diagnosticErrors;
    int suppressedBackground = 0;
    for (const auto& value : logValues) {
        const auto line = value.toString();
        if (isKnownBackgroundLogNoise(line)) {
            ++suppressedBackground;
        } else if (!line.trimmed().isEmpty()) {
            diagnosticErrors.append(line);
        }
    }
    struct SignatureEntry { int count {0}; QString example; };
    QHash<QString, SignatureEntry> signatures;
    QStringList signatureOrder;
    for (const auto& line : diagnosticErrors) {
        const auto signature = logSignature(line);
        if (signature.isEmpty()) continue;
        if (!signatures.contains(signature)) {
            signatures.insert(signature, SignatureEntry {0, line});
            signatureOrder.append(signature);
        }
        ++signatures[signature].count;
    }
    QHash<QString, LogCategorySummary> categorySummaries;
    int uncategorized = 0;
    for (const auto& signature : signatureOrder) {
        const auto entry = signatures.value(signature);
        const auto* matched = logCategory(entry.example);
        if (matched == nullptr) {
            uncategorized += entry.count;
            continue;
        }
        auto& summary = categorySummaries[matched->key];
        summary.count += entry.count;
        ++summary.signatures;
        if (summary.examples.size() < 2) summary.examples.append(entry.example);
    }
    bool hasActionableCategory = false;
    // Audited per STAGE49.md scope note ("general-log confidence... remain to be
    // audited"): each category previously reported its fixed definition.confidence
    // even when the underlying log collection itself was incomplete/estimated or
    // failed outright. A category is not evidence of anything if the log behind it
    // wasn't reliably read, so quality now caps what confidence a category can claim,
    // mirroring the valid/estimated/collector-error tiers used for incident logs.
    const bool logQualityFullyValid = logQuality.isEmpty() || logQuality == QStringLiteral("valid");
    const bool logQualityFailed = logQuality == QStringLiteral("collector_error")
        || logQuality == QStringLiteral("permission_denied");
    for (const auto& definition : logCategoryDefinitions()) {
        const auto summary = categorySummaries.value(definition.key);
        if (summary.count < definition.minimumCount) continue;
        hasActionableCategory = true;
        FindingOptions options;
        options.domain = definition.domain;
        options.confidence = logQualityFailed
            ? QStringLiteral("low")
            : (logQualityFullyValid
                ? definition.confidence
                : (definition.confidence == QStringLiteral("high") ? QStringLiteral("medium") : definition.confidence));
        options.urgency = definition.key == QStringLiteral("whea")
            ? QStringLiteral("soon") : QStringLiteral("monitor");
        options.impact = QStringLiteral("stability");
        options.groupKey = QStringLiteral("logs.%1").arg(definition.key);
        options.evidence.append(makeEvidence(
            QStringLiteral("Количество событий"), summary.count, QStringLiteral("system log")));
        options.evidence.append(makeEvidence(
            QStringLiteral("Уникальные сигнатуры"), summary.signatures,
            QStringLiteral("normalised messages")));
        options.evidence.append(makeEvidence(
            QStringLiteral("Качество источника журнала"),
            logQuality.isEmpty() ? QStringLiteral("valid") : logQuality,
            QStringLiteral("system log collector")));
        for (const auto& example : summary.examples) {
            options.evidence.append(makeEvidence(
                QStringLiteral("Пример"), example, QStringLiteral("system log")));
        }
        options.actions = {
            QStringLiteral("Сверить время событий с телеметрией и действиями пользователя"),
        };
        const QString qualifier = logQualityFailed
            ? QStringLiteral(" Сбор журнала не удался, поэтому это нижняя граница известных событий, не полная картина.")
            : (!logQualityFullyValid
                ? QStringLiteral(" Журнал собран не полностью, поэтому число событий может быть занижено.")
                : QString {});
        appendFinding(findings,
            QStringLiteral("logs.%1").arg(definition.key),
            definition.key == QStringLiteral("whea")
                ? QStringLiteral("critical") : QStringLiteral("warning"),
            QStringLiteral("%1: %2 событий").arg(definition.label).arg(summary.count),
            QStringLiteral("%1 Уникальных сигнатур: %2.%3")
                .arg(definition.explanation).arg(summary.signatures).arg(qualifier),
            options);
    }
    if (diagnosticErrors.size() >= 10 && !hasActionableCategory) {
        FindingOptions options;
        options.domain = QStringLiteral("stability");
        options.confidence = QStringLiteral("low");
        options.urgency = QStringLiteral("monitor");
        options.groupKey = QStringLiteral("logs.uncategorized");
        options.evidence = {
            makeEvidence(QStringLiteral("Диагностически значимых строк"),
                diagnosticErrors.size(), QStringLiteral("system log")),
            makeEvidence(QStringLiteral("Уникальных сигнатур"),
                signatures.size(), QStringLiteral("normalised messages")),
            makeEvidence(QStringLiteral("Нераспознанных событий"),
                uncategorized, QStringLiteral("system log")),
            makeEvidence(QStringLiteral("Отфильтровано фоновых событий"),
                suppressedBackground, QStringLiteral("known Windows background-event rules")),
        };
        appendFinding(findings,
            QStringLiteral("logs.uncategorized"), QStringLiteral("warning"),
            QStringLiteral("Много нераспознанных ошибок журнала (%1)")
                .arg(diagnosticErrors.size()),
            QStringLiteral("Автоматическая классификация не нашла надёжной общей причины. Нужны источник, Event ID и время каждого события."),
            options);
    }
    const auto temperature = diagnostics.value(QStringLiteral("temperature")).toObject();
    bool unknownTemperature = false;
    for (const auto& key : {QStringLiteral("cpu"), QStringLiteral("gpu")}) {
        unknownTemperature |= temperature.value(key)
                                  .toObject()
                                  .value(QStringLiteral("level"))
                                  .toString(QStringLiteral("unknown"))
            == QStringLiteral("unknown");
    }
    if (unknownTemperature) {
        FindingOptions options;
        options.domain = QStringLiteral("coverage");
        options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = QStringLiteral("high");
        options.groupKey = QStringLiteral("coverage.temperature");
        appendFinding(
            findings,
            QStringLiteral("coverage.temperature"),
            QStringLiteral("info"),
            QStringLiteral("Температура измерена не полностью"),
            QStringLiteral("Отсутствие показаний не считается нормой."),
            options);
    }

    const auto disks = diagnostics.value(QStringLiteral("smart"))
                           .toObject()
                           .value(QStringLiteral("disks"))
                           .toArray();
    for (const auto& value : disks) {
        const auto disk = value.toObject();
        const auto device = disk.value(QStringLiteral("device"))
                                .toString(QStringLiteral("unknown"));
        const auto group = QStringLiteral("storage.%1").arg(device);
        const auto reasons = disk.value(QStringLiteral("risk_reasons")).toArray();
        bool critical = false;
        bool warning = false;
        for (const auto& reasonValue : reasons) {
            const auto severity = reasonValue.toObject().value(QStringLiteral("severity")).toString();
            critical |= severity == QStringLiteral("critical");
            warning |= severity == QStringLiteral("warning");
        }
        if (disk.value(QStringLiteral("data_quality")).toString() == QStringLiteral("collector_error")
            || disk.value(QStringLiteral("level")).toString() == QStringLiteral("unknown")) {
            FindingOptions options;
            options.domain = QStringLiteral("storage");
            options.status = QString::fromLatin1(kStatusUnknown);
            options.confidence = QStringLiteral("high");
            options.groupKey = group;
            options.affectedDevice = device;
            appendFinding(findings,
                QStringLiteral("storage.%1.unknown").arg(device),
                QStringLiteral("info"),
                QStringLiteral("%1: SMART-данные неполные").arg(device),
                disk.value(QStringLiteral("note")).toString(
                    QStringLiteral("Не удалось получить надёжный SMART-снимок этого устройства.")),
                options);
            continue;
        }
        if (critical || warning) {
            FindingOptions options;
            options.domain = QStringLiteral("storage");
            options.status = QString::fromLatin1(critical ? kStatusFail : kStatusWarning);
            options.confidence = critical ? QStringLiteral("high") : QStringLiteral("medium");
            options.urgency = critical ? QStringLiteral("immediate") : QStringLiteral("monitor");
            options.impact = critical ? QStringLiteral("data_loss") : QStringLiteral("data_reliability");
            options.groupKey = group;
            options.affectedDevice = device;
            options.evidence = {
                makeEvidence(QStringLiteral("SMART overall"),
                    disk.value(QStringLiteral("health")), QStringLiteral("smartctl")),
                makeEvidence(QStringLiteral("Reallocated"),
                    disk.value(QStringLiteral("reallocated")), QStringLiteral("SMART")),
                makeEvidence(QStringLiteral("Pending"),
                    disk.value(QStringLiteral("pending")), QStringLiteral("SMART")),
                makeEvidence(QStringLiteral("Uncorrectable"),
                    disk.value(QStringLiteral("uncorrectable")), QStringLiteral("SMART")),
            };
            QStringList reasonTexts;
            for (const auto& reasonValue : reasons) {
                const auto reason = reasonValue.toObject();
                reasonTexts.append(reason.value(QStringLiteral("text")).toString());
                options.evidence.append(makeEvidence(
                    reason.value(QStringLiteral("text")).toString(QStringLiteral("SMART signal")),
                    reason.value(QStringLiteral("value")), QStringLiteral("smartctl")));
            }
            options.actions = critical
                ? QStringList {QStringLiteral("Немедленно создать резервную копию важных данных"),
                    QStringLiteral("Не запускать тяжёлую запись до копирования")}
                : QStringList {QStringLiteral("Сохранить текущие SMART-значения как базовую точку"),
                    QStringLiteral("Проверять изменение атрибутов")};
            appendFinding(findings,
                QStringLiteral("storage.%1.%2").arg(device,
                    critical ? QStringLiteral("critical") : QStringLiteral("warning")),
                critical ? QStringLiteral("critical") : QStringLiteral("warning"),
                critical
                    ? QStringLiteral("%1: критические SMART-признаки").arg(device)
                    : QStringLiteral("%1: SMART требует наблюдения").arg(device),
                QStringLiteral("Обнаружено: %1. %2")
                    .arg(reasonTexts.join(QStringLiteral("; ")),
                         critical
                            ? QStringLiteral("Сначала сохраните важные данные, затем проводите дополнительные тесты.")
                            : QStringLiteral("Решающей будет динамика показателей между проверками.")),
                options);
        }
        const auto temperature = number(disk, QStringLiteral("temperature_c"));
        const auto temperatureLevel = disk.value(QStringLiteral("temperature_level")).toString();
        if (temperature.has_value()
            && (temperatureLevel == QStringLiteral("warn")
                || temperatureLevel == QStringLiteral("critical"))) {
            const bool temperatureCritical = temperatureLevel == QStringLiteral("critical");
            FindingOptions options;
            options.domain = QStringLiteral("cooling");
            options.confidence = QStringLiteral("medium");
            options.urgency = temperatureCritical ? QStringLiteral("soon") : QStringLiteral("monitor");
            options.impact = QStringLiteral("storage_stability");
            options.groupKey = group;
            options.affectedDevice = device;
            options.evidence = {
                makeEvidence(QStringLiteral("Температура"), *temperature, QStringLiteral("SMART")),
                makeEvidence(QStringLiteral("Тип"), disk.value(QStringLiteral("disk_type")),
                    QStringLiteral("smartctl")),
            };
            appendFinding(findings,
                QStringLiteral("storage.%1.temperature").arg(device),
                temperatureCritical ? QStringLiteral("critical") : QStringLiteral("warning"),
                QStringLiteral("%1: высокая температура накопителя (%2°C)")
                    .arg(device).arg(*temperature, 0, 'f', 0),
                QStringLiteral("Температура оценена по профилю типа накопителя. Проверьте обдув и повторите измерение под той же нагрузкой."),
                options);
        }
    }
}

void analyzeStress(
    QJsonArray& findings,
    const QJsonObject& stress,
    const QJsonObject& hardware)
{
    if (stress.value(QStringLiteral("stop_reason")).toString()
            == QStringLiteral("safety")) {
        FindingOptions options;
        options.domain = QStringLiteral("cooling");
        options.status = QString::fromLatin1(kStatusFail);
        options.confidence = QStringLiteral("high");
        options.urgency = QStringLiteral("immediate");
        options.impact = QStringLiteral("stability");
        options.priority = 95;
        options.groupKey = QStringLiteral("stress.safety");
        options.rootCause = QStringLiteral("thermal safety stop");
        options.actions = {
            QStringLiteral("Не повторять длительную нагрузку до проверки охлаждения"),
            QStringLiteral("Проверить вентиляторы, радиаторы и контакт системы охлаждения"),
        };
        appendFinding(
            findings,
            QStringLiteral("stress.safety_stop"),
            QStringLiteral("critical"),
            QStringLiteral("Стресс-тест остановлен температурной защитой"),
            stress.value(QStringLiteral("safety_stop_reason")).toString(
                QStringLiteral("Достигнут защитный температурный порог.")),
            options);
    }

    const auto newErrors = stress.value(QStringLiteral("new_system_errors")).toArray();
    if (!newErrors.isEmpty()) {
        const bool whea = hasWhea(newErrors);
        // Audited per FINAL-CHECKLIST.md ("уверенность/причинные выводы... стресс-сессии"):
        // "new" here only means "not present in the before snapshot". If that
        // comparison itself was degraded (system_errors_comparison_quality != valid)
        // or never marked supported, the appearance is not confirmed to have been
        // caused by the load, so this can no longer always assert high confidence
        // and unqualified causal wording.
        const auto comparisonQuality = stress.value(QStringLiteral("system_errors_comparison_quality")).toString();
        const bool comparisonSupported = !stress.contains(QStringLiteral("system_errors_checked_after"))
            || stress.value(QStringLiteral("system_errors_checked_after")).toBool();
        const bool comparisonFullyValid = comparisonSupported
            && (comparisonQuality.isEmpty() || comparisonQuality == QStringLiteral("valid"));
        FindingOptions options;
        options.domain = QStringLiteral("stability");
        options.confidence = comparisonFullyValid ? QStringLiteral("high") : QStringLiteral("medium");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("stability");
        options.priority = 40;
        options.groupKey = whea
            ? QStringLiteral("logs.whea")
            : QStringLiteral("stress.system_errors");
        options.evidence = {
            makeEvidence(QStringLiteral("Новых строк в сравнении снимков"), newErrors.size(),
                QStringLiteral("before/after system log diff")),
            makeEvidence(QStringLiteral("Сравнение подтверждено"), comparisonSupported,
                QStringLiteral("stress session log diff")),
            makeEvidence(QStringLiteral("Качество сравнения"),
                comparisonQuality.isEmpty() ? QStringLiteral("valid") : comparisonQuality,
                QStringLiteral("system log collector")),
        };
        const QString qualifier = comparisonFullyValid
            ? QString {}
            : QStringLiteral(" Сравнение снимков до/после неполное или не подтверждено, поэтому появление записи не доказывает, что её вызвала именно нагрузка.");
        appendFinding(
            findings,
            QStringLiteral("stress.new_system_errors"),
            whea ? QStringLiteral("critical") : QStringLiteral("warning"),
            QStringLiteral("После нагрузки появились новые системные события"),
            QStringLiteral("События отсутствовали до теста и появились после него.%1").arg(qualifier),
            options);
    }

    if (stress.value(QStringLiteral("run_cpu")).toBool()) {
        if (stress.value(QStringLiteral("cpu_worker_failures")).toInt() > 0) {
            FindingOptions options;
            options.domain = QStringLiteral("cpu");
            options.confidence = QStringLiteral("high");
            options.urgency = QStringLiteral("soon");
            options.impact = QStringLiteral("stability");
            options.priority = 40;
            options.groupKey = QStringLiteral("stress.cpu");
            appendFinding(
                findings,
                QStringLiteral("stress.cpu.worker_failure"),
                QStringLiteral("critical"),
                QStringLiteral("CPU-тест: аварийно завершились воркеры"),
                QStringLiteral("Вычислительный процесс завершился с ошибкой."),
                options);
        }
        if (stress.value(QStringLiteral("cpu_throttling_suspected")).toBool()) {
            FindingOptions options;
            options.domain = QStringLiteral("cpu");
            options.confidence = stress.value(QStringLiteral("cpu_throttling_confidence"))
                                     .toString(QStringLiteral("medium"));
            options.urgency = QStringLiteral("soon");
            options.impact = QStringLiteral("performance");
            options.priority = 30;
            options.groupKey = QStringLiteral("cooling.cpu");
            appendFinding(
                findings,
                QStringLiteral("stress.cpu.throttling"),
                QStringLiteral("warning"),
                QStringLiteral("Вероятен троттлинг CPU под нагрузкой"),
                QStringLiteral("Частота и/или скорость workload снизились во второй части теста."),
                options);
        } else if (stress.value(QStringLiteral("cpu_load_sample_count")).toInt() == 0) {
            FindingOptions options;
            options.domain = QStringLiteral("coverage");
            options.status = QString::fromLatin1(kStatusUnknown);
            options.groupKey = QStringLiteral("stress.cpu");
            appendFinding(
                findings,
                QStringLiteral("stress.cpu.no_telemetry"),
                QStringLiteral("info"),
                QStringLiteral("CPU-тест не измерил фактическую загрузку"),
                QStringLiteral("Результат нельзя трактовать как подтверждённую нагрузку."),
                options);
        }
    }

    if (stress.value(QStringLiteral("run_gpu")).toBool()) {
        if (!stress.value(QStringLiteral("gpu_supported")).toBool()) {
            FindingOptions options;
            options.domain = QStringLiteral("coverage");
            options.status = QString::fromLatin1(kStatusUnknown);
            options.confidence = QStringLiteral("high");
            options.impact = QStringLiteral("test_validity");
            options.groupKey = QStringLiteral("stress.gpu");
            appendFinding(
                findings,
                QStringLiteral("coverage.stress_gpu_unavailable"),
                QStringLiteral("info"),
                QStringLiteral("GPU-тест не выполнен"),
                stress.value(QStringLiteral("gpu_worker_error")).toString(
                    QStringLiteral("Аппаратный backend GPU недоступен.")),
                options);
        } else if (stress.value(QStringLiteral("gpu_output_verified")).isBool()
                   && !stress.value(QStringLiteral("gpu_output_verified")).toBool()) {
            FindingOptions options;
            options.domain = QStringLiteral("gpu");
            options.status = QString::fromLatin1(kStatusFail);
            options.confidence = QStringLiteral("high");
            options.urgency = QStringLiteral("soon");
            options.impact = QStringLiteral("stability");
            options.priority = 60;
            options.groupKey = QStringLiteral("stress.gpu");
            appendFinding(
                findings,
                QStringLiteral("stress.gpu.output_mismatch"),
                QStringLiteral("critical"),
                QStringLiteral("GPU-тест не подтвердил результат вычислений"),
                QStringLiteral("Compute-нагрузка завершилась, но проверяемый результат не изменился корректно."),
                options);
        }
    }

    if (!stress.value(QStringLiteral("run_disk")).toBool()) {
        return;
    }
    if (stress.value(QStringLiteral("disk_data_verified")).isBool()
        && !stress.value(QStringLiteral("disk_data_verified")).toBool()) {
        FindingOptions options;
        options.domain = QStringLiteral("storage");
        options.status = QString::fromLatin1(kStatusFail);
        options.confidence = QStringLiteral("high");
        options.urgency = QStringLiteral("immediate");
        options.impact = QStringLiteral("data_integrity");
        options.priority = 50;
        options.groupKey = QStringLiteral("stress.disk");
        appendFinding(
            findings,
            QStringLiteral("stress.disk.data_mismatch"),
            QStringLiteral("critical"),
            QStringLiteral("Дисковый тест прочитал данные с неверной контрольной суммой"),
            QStringLiteral("Записанный и прочитанный временный файл различаются."),
            options);
    }
    if (stress.value(QStringLiteral("disk_target_is_memory_fs")).toBool()) {
        FindingOptions options;
        options.domain = QStringLiteral("coverage");
        options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = QStringLiteral("high");
        options.urgency = QStringLiteral("none");
        options.impact = QStringLiteral("test_validity");
        options.groupKey = QStringLiteral("stress.disk");
        appendFinding(
            findings,
            QStringLiteral("coverage.stress_disk_memory_fs"),
            QStringLiteral("info"),
            QStringLiteral("Дисковый тест выполнен в памяти"),
            QStringLiteral("Скорость не характеризует физический накопитель."),
            options);
        return;
    }
    const bool flushed = stress.value(QStringLiteral("disk_fsync_applied")).toBool()
        || stress.value(QStringLiteral("disk_fsync_performed")).toBool();
    const auto writeSpeed = number(stress, QStringLiteral("disk_write_mbps"));
    if (!flushed || !writeSpeed.has_value()) {
        return;
    }
    const auto type = stressDiskType(stress, hardware);
    const double threshold = type == QStringLiteral("SSD")
        ? 100.0
        : type == QStringLiteral("HDD") ? 25.0 : 20.0;
    if (*writeSpeed < threshold) {
        FindingOptions options;
        options.domain = QStringLiteral("storage");
        options.confidence = QStringLiteral("medium");
        options.urgency = QStringLiteral("monitor");
        options.impact = QStringLiteral("performance");
        options.groupKey = QStringLiteral("stress.disk");
        appendFinding(
            findings,
            QStringLiteral("stress.disk.slow_write"),
            QStringLiteral("warning"),
            QStringLiteral("Низкая последовательная запись"),
            QStringLiteral("Порог выбран с учётом типа целевого диска."),
            options);
    }
}

void analyzeMemory(
    QJsonArray& findings,
    const QJsonObject& hardware,
    const QJsonObject& runtime)
{
    if (runtime.isEmpty()) {
        return;
    }
    const auto swap = number(runtime, QStringLiteral("swap_used_percent"));
    const auto available = number(runtime, QStringLiteral("memory_available_percent"));
    const bool confirmed = runtime.value(QStringLiteral("memory_pressure")).toString()
            == QStringLiteral("confirmed")
        && runtime.value(QStringLiteral("paging_activity")).toString()
            == QStringLiteral("active")
        && available.value_or(100.0) < 10.0
        && number(hardware.value(QStringLiteral("ram")).toObject(), QStringLiteral("total_gb"))
               .has_value();
    if (confirmed) {
        FindingOptions options;
        options.domain = QStringLiteral("memory");
        options.status = QString::fromLatin1(kStatusFail);
        options.confidence = runtime.value(QStringLiteral("data_quality")).toString()
                == QStringLiteral("valid")
            ? QStringLiteral("high")
            : QStringLiteral("medium");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("responsiveness");
        options.priority = 30;
        options.groupKey = QStringLiteral("memory");
        options.rootCause = QStringLiteral("memory pressure");
        appendFinding(
            findings,
            QStringLiteral("memory.pressure.confirmed"),
            QStringLiteral("critical"),
            QStringLiteral("Подтверждено давление на оперативную память"),
            QStringLiteral("Низкая доступная RAM совпала с измеренным paging."),
            options);
    } else if (swap.value_or(0.0) >= 1.0) {
        FindingOptions options;
        options.domain = QStringLiteral("memory");
        options.status = QString::fromLatin1(kStatusUnknown);
        options.confidence = QStringLiteral("high");
        options.groupKey = QStringLiteral("memory");
        appendFinding(
            findings,
            QStringLiteral("memory.swap.occupied"),
            QStringLiteral("info"),
            QStringLiteral("Pagefile/swap занят"),
            QStringLiteral("Занятое пространство не доказывает активную подкачку."),
            options);
    }
}

void analyzeCurrentSnapshot(QJsonArray& findings, const QJsonObject& snapshot)
{
    const auto diagnostics = snapshot.value(QStringLiteral("diagnostics")).toObject();
    const auto current = diagnostics.value(QStringLiteral("current")).toObject();
    const auto hardware = snapshot.value(QStringLiteral("hardware")).toObject();

    const auto ramPercent = number(current, QStringLiteral("ram_usage_percent"));
    if (ramPercent.value_or(0.0) >= 95.0) {
        FindingOptions options;
        options.domain = QStringLiteral("memory");
        options.confidence = QStringLiteral("medium");
        options.urgency = QStringLiteral("soon");
        options.impact = QStringLiteral("responsiveness");
        options.priority = 35;
        options.groupKey = QStringLiteral("memory.current");
        options.evidence.append(makeEvidence(
            QStringLiteral("Использование RAM, %"), *ramPercent,
            QStringLiteral("native telemetry")));
        options.actions = {
            QStringLiteral("Открыть диспетчер задач O.R.I.O.N. и проверить процессы-лидеры по RAM"),
            QStringLiteral("Повторить замер в момент воспроизводимой проблемы"),
        };
        appendFinding(findings, QStringLiteral("memory.current.high"),
            QStringLiteral("warning"),
            QStringLiteral("На момент проверки почти вся RAM занята"),
            QStringLiteral("Одиночный снимок не доказывает утечку памяти, но при фризах требует проверки процессов и paging."),
            options);
    }

    for (const auto& [key, label] : std::initializer_list<std::pair<QString, QString>> {
             {QStringLiteral("cpu_temperature_c"), QStringLiteral("CPU")},
             {QStringLiteral("gpu_temperature_c"), QStringLiteral("GPU")}}) {
        const auto temperature = number(current, key);
        if (!temperature.has_value()) {
            continue;
        }
        const auto level = orion::core::levelForTemperature(
            temperature, label == QStringLiteral("CPU") ? "cpu" : "gpu");
        if (level != orion::core::StatusLevel::Warning
            && level != orion::core::StatusLevel::Critical) {
            continue;
        }
        const bool critical = level == orion::core::StatusLevel::Critical;
        FindingOptions options;
        options.domain = QStringLiteral("cooling");
        options.confidence = QStringLiteral("medium");
        options.urgency = critical ? QStringLiteral("soon") : QStringLiteral("monitor");
        options.impact = QStringLiteral("performance_and_stability");
        options.priority = critical ? 55 : 25;
        options.groupKey = QStringLiteral("cooling.%1").arg(label.toLower());
        options.evidence.append(makeEvidence(
            QStringLiteral("Температура, °C"), *temperature,
            QStringLiteral("native sensor collector")));
        options.actions = {
            QStringLiteral("Проверить создающий нагрузку процесс и работу вентиляторов"),
            QStringLiteral("Повторить одинаковую нагрузку и сравнить температуру и частоту"),
        };
        appendFinding(findings,
            QStringLiteral("temperature.%1.current").arg(label.toLower()),
            critical ? QStringLiteral("critical") : QStringLiteral("warning"),
            QStringLiteral("%1: повышенная температура в текущем снимке").arg(label),
            QStringLiteral("Это моментальный замер; устойчивость нагрева нужно подтвердить повторным наблюдением."),
            options);
    }

    for (const auto& value : hardware.value(QStringLiteral("disks")).toArray()) {
        const auto disk = value.toObject();
        const auto used = number(disk, QStringLiteral("used_percent"));
        if (!used.has_value() || *used <= 90.0) {
            continue;
        }
        const auto mount = disk.value(QStringLiteral("mountpoint"))
                               .toString(QStringLiteral("накопитель"));
        FindingOptions options;
        options.domain = QStringLiteral("storage");
        options.confidence = QStringLiteral("high");
        options.urgency = *used >= 95.0 ? QStringLiteral("soon") : QStringLiteral("monitor");
        options.impact = QStringLiteral("performance_and_stability");
        options.priority = *used >= 95.0 ? 50 : 20;
        options.groupKey = QStringLiteral("storage.space.%1").arg(mount);
        options.affectedDevice = mount;
        options.evidence.append(makeEvidence(
            QStringLiteral("Занято, %"), *used, QStringLiteral("native volume collector")));
        options.actions = {
            QStringLiteral("Освободить место штатными средствами и проверить временные файлы"),
        };
        appendFinding(findings,
            QStringLiteral("storage.%1.low_space").arg(mount),
            QStringLiteral("warning"),
            QStringLiteral("%1: мало свободного места").arg(mount),
            QStringLiteral("Недостаток места может мешать обновлениям, временным файлам и росту pagefile."),
            options);
    }
}

void analyzeAutostart(QJsonArray& findings, const QJsonArray& entries)
{
    int enabledUserEntries = 0;
    for (const auto& value : entries) {
        const auto entry = value.toObject();
        const auto enabled = entry.value(QStringLiteral("enabled"));
        if (entry.value(QStringLiteral("category")).toString() == QStringLiteral("user")
            && (!enabled.isBool() || enabled.toBool())) {
            ++enabledUserEntries;
        }
    }
    constexpr int manyEntriesThreshold = 15;
    if (enabledUserEntries <= manyEntriesThreshold) {
        return;
    }
    FindingOptions options;
    options.domain = QStringLiteral("startup");
    options.confidence = QStringLiteral("medium");
    options.urgency = QStringLiteral("monitor");
    options.impact = QStringLiteral("startup_performance");
    options.groupKey = QStringLiteral("startup");
    options.evidence.append(makeEvidence(
        QStringLiteral("Включённых пользовательских записей"), enabledUserEntries,
        QStringLiteral("autostart collector")));
    options.actions = {
        QStringLiteral("Сначала измерить влияние программ на запуск, затем отключать ненужное штатными средствами ОС"),
    };
    appendFinding(findings, QStringLiteral("startup.many_entries"),
        QStringLiteral("info"),
        QStringLiteral("Много пользовательских программ в автозагрузке (%1)")
            .arg(enabledUserEntries),
        QStringLiteral("Количество само по себе не доказывает медленный старт системы."),
        options);
}

} // namespace

QJsonArray analyzeSnapshot(const QJsonObject& snapshot)
{
    QJsonArray findings;
    const auto diagnostics = snapshot.value(QStringLiteral("diagnostics")).toObject();
    const auto stress = snapshot.value(QStringLiteral("stress_test")).toObject();
    const auto hardware = snapshot.value(QStringLiteral("hardware")).toObject();
    const auto runtime = snapshot.value(QStringLiteral("runtime")).toObject();

    analyzeCausalChains(findings, diagnostics, stress, hardware);
    analyzeIncident(findings, snapshot.value(QStringLiteral("incident")).toObject());
    analyzeAppMonitor(findings, snapshot.value(QStringLiteral("app_monitor")).toObject());
    analyzeDiagnostics(findings, diagnostics);
    analyzeStress(findings, stress, hardware);
    analyzeMemory(findings, hardware, runtime);
    analyzeCurrentSnapshot(findings, snapshot);
    analyzeAutostart(findings, snapshot.value(QStringLiteral("autostart_entries")).toArray());
    return mergeSameGroup(findings);
}

} // namespace orion::diagnostics
