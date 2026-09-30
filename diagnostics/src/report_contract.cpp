#include "orion/diagnostics/report_contract.h"

#include "orion/diagnostics/diagnostic_engine.h"
#include "orion/diagnostics/diagnostic_schema.h"
#include "orion/diagnostics/system_diagnostics_collector.h"

#include <QDateTime>
#include <QJsonArray>
#include <QStringList>

#include <algorithm>

namespace orion::diagnostics {
namespace {

[[nodiscard]] QJsonValue valueOrNull(
    const QJsonObject& object,
    const QString& key)
{
    const auto value = object.value(key);
    return value.isUndefined() ? QJsonValue {QJsonValue::Null} : value;
}

[[nodiscard]] QString formatted(
    const QJsonValue& value,
    const QString& unit = {},
    const int precision = 1)
{
    if (value.isUndefined() || value.isNull()) {
        return QStringLiteral("н/д");
    }
    QString text;
    if (value.isDouble()) {
        text = QString::number(value.toDouble(), 'f', std::max(0, precision));
    } else if (value.isBool()) {
        text = value.toBool() ? QStringLiteral("да") : QStringLiteral("нет");
    } else {
        text = value.toString();
    }
    return unit.isEmpty() ? text : QStringLiteral("%1 %2").arg(text, unit);
}

[[nodiscard]] QString yesNoUnknown(const QJsonValue& value)
{
    return value.isBool()
        ? (value.toBool() ? QStringLiteral("успешно") : QStringLiteral("ОШИБКА"))
        : QStringLiteral("не выполнена");
}

[[nodiscard]] QString confidenceText(const QString& confidence)
{
    if (confidence == QStringLiteral("high")) return QStringLiteral("высокая");
    if (confidence == QStringLiteral("medium")) return QStringLiteral("средняя");
    if (confidence == QStringLiteral("low")) return QStringLiteral("низкая");
    return QStringLiteral("не определена");
}

void appendStringValues(
    QStringList& lines,
    const QJsonArray& values,
    const QString& prefix,
    const int maximum = 8)
{
    const auto count = std::min<qsizetype>(maximum, values.size());
    for (qsizetype index = 0; index < count; ++index) {
        const auto text = values.at(index).toString().trimmed();
        if (!text.isEmpty()) {
            lines.append(prefix + text);
        }
    }
}

void appendSystemErrors(QStringList& lines, const QJsonArray& errors, const QString& prefix)
{
    for (const auto& value : summarizeSystemErrorEntries(errors, 8)) {
        const auto group = value.toObject();
        if (group.value(QStringLiteral("omitted")).toBool()) {
            lines.append(prefix + group.value(QStringLiteral("example")).toString());
        } else {
            const auto count = group.value(QStringLiteral("count")).toInt();
            const auto marker = count > 1 ? QStringLiteral("[%1×] ").arg(count) : QString {};
            lines.append(prefix + marker + group.value(QStringLiteral("example")).toString());
        }
    }
}

void appendFinding(QStringList& lines, const QJsonObject& finding, const int ordinal = 0)
{
    const auto severity = finding.value(QStringLiteral("severity")).toString();
    const auto marker = severity == QStringLiteral("critical")
        ? QStringLiteral("КРИТИЧНО")
        : severity == QStringLiteral("warning") ? QStringLiteral("ВНИМАНИЕ") : QStringLiteral("ИНФО");
    const auto title = finding.value(QStringLiteral("title")).toString(QStringLiteral("Без названия"));
    lines.append(ordinal > 0
        ? QStringLiteral("%1. %2 (уверенность: %3)")
              .arg(ordinal).arg(title, confidenceText(finding.value(QStringLiteral("confidence")).toString()))
        : QStringLiteral("[%1] %2").arg(marker, title));
    if (ordinal > 0) {
        return;
    }
    const auto detail = finding.value(QStringLiteral("detail")).toString();
    if (!detail.isEmpty()) {
        lines.append(QStringLiteral("   %1").arg(detail));
    }
    lines.append(QStringLiteral("   Уверенность: %1; срочность: %2; статус: %3.")
        .arg(confidenceText(finding.value(QStringLiteral("confidence")).toString()),
             finding.value(QStringLiteral("urgency")).toString(QStringLiteral("н/д")),
             finding.value(QStringLiteral("status")).toString(QStringLiteral("unknown"))));

    QStringList evidenceParts;
    const auto evidence = finding.value(QStringLiteral("evidence")).toArray();
    for (qsizetype index = 0; index < std::min<qsizetype>(6, evidence.size()); ++index) {
        const auto item = evidence.at(index).toObject();
        if (!item.contains(QStringLiteral("value")) || item.value(QStringLiteral("value")).isNull()) {
            continue;
        }
        QStringList annotations;
        if (!item.value(QStringLiteral("source")).toString().isEmpty()) {
            annotations.append(item.value(QStringLiteral("source")).toString());
        }
        const auto quality = item.value(QStringLiteral("quality")).toString();
        if (!quality.isEmpty() && quality != QStringLiteral("valid")) {
            annotations.append(QStringLiteral("quality=%1").arg(quality));
        }
        evidenceParts.append(QStringLiteral("%1: %2%3")
            .arg(item.value(QStringLiteral("label")).toString(QStringLiteral("Факт")),
                 formatted(item.value(QStringLiteral("value")), {}, 1),
                 annotations.isEmpty()
                    ? QString {} : QStringLiteral(" [%1]").arg(annotations.join(QStringLiteral("; ")))));
    }
    if (!evidenceParts.isEmpty()) {
        lines.append(QStringLiteral("   Доказательства: %1").arg(evidenceParts.join(QStringLiteral("; "))));
    }
    const auto counterEvidence = finding.value(QStringLiteral("counter_evidence")).toArray();
    if (!counterEvidence.isEmpty()) {
        QStringList values;
        for (qsizetype index = 0; index < std::min<qsizetype>(3, counterEvidence.size()); ++index) {
            const auto item = counterEvidence.at(index).toObject();
            values.append(QStringLiteral("%1: %2")
                .arg(item.value(QStringLiteral("label")).toString(QStringLiteral("Факт")),
                     formatted(item.value(QStringLiteral("value")), {}, 1)));
        }
        lines.append(QStringLiteral("   Контрдоказательства: %1").arg(values.join(QStringLiteral("; "))));
    }
    const auto actions = finding.value(QStringLiteral("actions")).toArray();
    if (!actions.isEmpty()) {
        QStringList values;
        for (qsizetype index = 0; index < std::min<qsizetype>(3, actions.size()); ++index) {
            values.append(actions.at(index).toString());
        }
        lines.append(QStringLiteral("   Что сделать: %1").arg(values.join(QStringLiteral("; "))));
    }
    const auto verification = finding.value(QStringLiteral("verification_steps")).toArray();
    if (!verification.isEmpty()) {
        QStringList values;
        for (qsizetype index = 0; index < std::min<qsizetype>(2, verification.size()); ++index) {
            values.append(verification.at(index).toString());
        }
        lines.append(QStringLiteral("   Как проверить: %1").arg(values.join(QStringLiteral("; "))));
    }
    const auto sourceChecks = finding.value(QStringLiteral("source_checks")).toArray();
    if (!sourceChecks.isEmpty()) {
        QStringList values;
        for (qsizetype index = 0; index < std::min<qsizetype>(2, sourceChecks.size()); ++index) {
            values.append(sourceChecks.at(index).toString());
        }
        lines.append(QStringLiteral("   Проверить источник: %1").arg(values.join(QStringLiteral("; "))));
    }
    lines.append(QString {});
}

} // namespace

QJsonObject buildReport(const QJsonObject& snapshot)
{
    const auto findings = analyzeSnapshot(snapshot);
    const auto coverage = buildCoverageSummary(findings);
    return {
        {QStringLiteral("schema_version"), kReportSchemaVersion},
        {QStringLiteral("generated_at"), QDateTime::currentDateTime().toString(Qt::ISODate)},
        {QStringLiteral("diagnostics"), valueOrNull(snapshot, QStringLiteral("diagnostics"))},
        {QStringLiteral("stress_test"), valueOrNull(snapshot, QStringLiteral("stress_test"))},
        {QStringLiteral("speed_test"), valueOrNull(snapshot, QStringLiteral("speed_test"))},
        {QStringLiteral("runtime"), valueOrNull(snapshot, QStringLiteral("runtime"))},
        {QStringLiteral("incident"), valueOrNull(snapshot, QStringLiteral("incident"))},
        {QStringLiteral("app_monitor"), valueOrNull(snapshot, QStringLiteral("app_monitor"))},
        {QStringLiteral("findings"), findings},
        {QStringLiteral("verdict_findings"), selectVerdictFindings(findings)},
        {QStringLiteral("coverage"), coverage},
        {QStringLiteral("risk_assessment"), buildRiskAssessment(
             findings,
             coverage,
             snapshot.value(QStringLiteral("stress_test")).toObject(),
             snapshot.value(QStringLiteral("runtime")).toObject())},
        {QStringLiteral("action_plan"), buildActionPlan(findings)},
    };
}

bool isReportV3(const QJsonObject& report) noexcept
{
    return report.value(QStringLiteral("schema_version")).toInt() == kReportSchemaVersion
        && report.value(QStringLiteral("generated_at")).isString()
        && report.value(QStringLiteral("findings")).isArray()
        && report.value(QStringLiteral("verdict_findings")).isArray()
        && report.value(QStringLiteral("coverage")).isObject()
        && report.value(QStringLiteral("risk_assessment")).isObject()
        && report.value(QStringLiteral("action_plan")).isArray();
}

QString reportToText(const QJsonObject& report)
{
    const auto risk = report.value(QStringLiteral("risk_assessment")).toObject();
    const auto coverage = report.value(QStringLiteral("coverage")).toObject();
    QStringList lines {
        QStringLiteral("O.R.I.O.N. — отчёт диагностики и стресс-теста"),
        QStringLiteral("Сформирован: %1").arg(
            report.value(QStringLiteral("generated_at")).toString(QStringLiteral("н/д"))),
        QString {},
        QStringLiteral("=== Диагностика ==="),
        QString {},
        QStringLiteral("-- SMART диска --"),
    };

    const auto diagnostics = report.value(QStringLiteral("diagnostics")).toObject();
    const auto smart = diagnostics.value(QStringLiteral("smart")).toObject();
    const auto logs = diagnostics.value(QStringLiteral("log_errors")).toObject();
    const auto current = diagnostics.value(QStringLiteral("current")).toObject();
    const auto smartDisks = smart.value(QStringLiteral("disks")).toArray();
    if (!smart.value(QStringLiteral("available")).toBool()) {
        lines.append(QStringLiteral("  НЕ ПРОВЕРЕНО: %1")
            .arg(smart.value(QStringLiteral("note")).toString(QStringLiteral("SMART недоступен"))));
    } else if (smartDisks.isEmpty()) {
        lines.append(QStringLiteral("  НЕИЗВЕСТНО: %1")
            .arg(smart.value(QStringLiteral("note")).toString(QStringLiteral("Диски не обнаружены."))));
    } else {
        for (const auto& value : smartDisks) {
            const auto disk = value.toObject();
            lines.append(QStringLiteral(
                "  %1 (%2): level=%3, SMART=%4, type=%5, reallocated=%6, pending=%7, "
                "uncorrectable=%8, temp=%9")
                .arg(disk.value(QStringLiteral("device")).toString(QStringLiteral("накопитель")),
                     disk.value(QStringLiteral("model")).toString(QStringLiteral("н/д")),
                     disk.value(QStringLiteral("level")).toString(QStringLiteral("unknown")),
                     disk.value(QStringLiteral("health")).toString(QStringLiteral("н/д")),
                     disk.value(QStringLiteral("disk_type")).toString(QStringLiteral("н/д")),
                     formatted(disk.value(QStringLiteral("reallocated")), {}, 0),
                     formatted(disk.value(QStringLiteral("pending")), {}, 0),
                     formatted(disk.value(QStringLiteral("uncorrectable")), {}, 0),
                     formatted(disk.value(QStringLiteral("temperature_c")), QStringLiteral("°C"))));
            const auto reasons = disk.value(QStringLiteral("risk_reasons")).toArray();
            if (!reasons.isEmpty()) {
                QStringList reasonTexts;
                for (const auto& reasonValue : reasons) {
                    const auto reason = reasonValue.toObject();
                    reasonTexts.append(reason.value(QStringLiteral("text"))
                        .toString(QStringLiteral("неизвестный SMART-сигнал")));
                }
                lines.append(QStringLiteral("    сигналы: %1").arg(reasonTexts.join(QStringLiteral("; "))));
            }
            if (!disk.value(QStringLiteral("note")).toString().isEmpty()) {
                lines.append(QStringLiteral("    примечание: %1")
                    .arg(disk.value(QStringLiteral("note")).toString()));
            }
        }
    }

    lines.append(QString {});
    lines.append(QStringLiteral("-- Ошибки в логах --"));
    const auto logErrors = logs.value(QStringLiteral("errors")).toArray();
    lines.append(QStringLiteral("  Источник: %1; качество=%2")
        .arg(logs.value(QStringLiteral("source")).toString(QStringLiteral("unknown")),
             logs.value(QStringLiteral("data_quality")).toString(QStringLiteral("unknown"))));
    if (logErrors.isEmpty()) {
        lines.append(QStringLiteral("  %1")
            .arg(logs.value(QStringLiteral("note")).toString(QStringLiteral("Ошибок не найдено."))));
    } else {
        if (!logs.value(QStringLiteral("note")).toString().isEmpty())
            lines.append(QStringLiteral("  %1").arg(logs.value(QStringLiteral("note")).toString()));
        lines.append(QStringLiteral("  Найдено записей с текущей загрузки: %1").arg(logErrors.size()));
        appendSystemErrors(lines, logErrors, QStringLiteral("  * "));
    }
    const auto stress = report.value(QStringLiteral("stress_test")).toObject();
    if (stress.contains(QStringLiteral("system_errors_checked_after"))) {
        if (stress.value(QStringLiteral("system_errors_checked_after")).toBool()) {
            const auto after = stress.value(QStringLiteral("new_system_errors")).toArray();
            lines.append(QStringLiteral("  Новых записей после нагрузки: %1").arg(after.size()));
            appendSystemErrors(lines, after, QStringLiteral("    + "));
        } else {
            lines.append(QStringLiteral("  Новые записи после нагрузки: НЕ ПРОВЕРЕНО"));
        }
    } else {
        lines.append(QStringLiteral("  Новые записи после нагрузки: НЕ ПРОВЕРЕНО"));
    }

    lines.append(QString {});
    lines.append(QStringLiteral("-- Температурные аномалии --"));
    const auto temperatures = diagnostics.value(QStringLiteral("temperature")).toObject();
    for (const auto& pair : std::initializer_list<std::pair<QString, QString>> {
             {QStringLiteral("cpu"), QStringLiteral("CPU")},
             {QStringLiteral("gpu"), QStringLiteral("GPU")}}) {
        const auto info = temperatures.value(pair.first).toObject();
        const auto level = info.value(QStringLiteral("level")).toString(QStringLiteral("unknown"));
        if (level == QStringLiteral("unknown")) {
            lines.append(QStringLiteral("  %1: НЕ ПРОВЕРЕНО / датчик недоступен").arg(pair.second));
        } else if (level == QStringLiteral("ok")) {
            lines.append(QStringLiteral("  %1: в норме (%2)")
                .arg(pair.second, formatted(info.value(QStringLiteral("current_value_c")), QStringLiteral("°C"))));
        } else {
            lines.append(QStringLiteral("  %1: %2 — текущий интервал %3 сек; сейчас %4")
                .arg(pair.second, level)
                .arg(info.value(QStringLiteral("duration_seconds")).toInt())
                .arg(formatted(info.value(QStringLiteral("current_value_c")), QStringLiteral("°C"))));
        }
    }

    const auto sensors = diagnostics.value(QStringLiteral("sensors")).toObject();
    if (!sensors.isEmpty()) {
        lines.append(QString {});
        lines.append(QStringLiteral("-- Датчики и вентиляторы --"));
        const auto sensorTemperatures = sensors.value(QStringLiteral("temperatures")).toArray();
        const auto fans = sensors.value(QStringLiteral("fans")).toArray();
        lines.append(QStringLiteral("  Качество: %1; температурных каналов=%2; вентиляторов=%3")
            .arg(sensors.value(QStringLiteral("data_quality")).toString(QStringLiteral("unknown")))
            .arg(sensorTemperatures.size()).arg(fans.size()));
        for (qsizetype index = 0; index < std::min<qsizetype>(24, sensorTemperatures.size()); ++index) {
            const auto sensor = sensorTemperatures.at(index).toObject();
            lines.append(QStringLiteral("    %1: %2 = %3 (%4)")
                .arg(sensor.value(QStringLiteral("component")).toString(QStringLiteral("прочее")),
                     sensor.value(QStringLiteral("label")).toString(QStringLiteral("Sensor")),
                     formatted(sensor.value(QStringLiteral("value_c")), QStringLiteral("°C")),
                     sensor.value(QStringLiteral("source")).toString(QStringLiteral("источник не указан"))));
            if (sensor.contains(QStringLiteral("quality")))
                lines.append(QStringLiteral("      Качество: %1; время: %2")
                    .arg(sensor.value(QStringLiteral("quality")).toString(),
                         sensor.value(QStringLiteral("observed_at")).toString(QStringLiteral("н/д"))));
        }
        for (qsizetype index = 0; index < std::min<qsizetype>(24, fans.size()); ++index) {
            const auto fan = fans.at(index).toObject();
            const auto value = fan.value(QStringLiteral("rpm")).isDouble()
                ? formatted(fan.value(QStringLiteral("rpm")), QStringLiteral("об/мин"), 0)
                : formatted(fan.value(QStringLiteral("percent")), QStringLiteral("%"));
            lines.append(QStringLiteral("    %1: %2 = %3 (%4)")
                .arg(fan.value(QStringLiteral("component")).toString(QStringLiteral("прочее")),
                     fan.value(QStringLiteral("label")).toString(QStringLiteral("Fan")),
                     value,
                     fan.value(QStringLiteral("source")).toString(QStringLiteral("источник не указан"))));
            if (fan.contains(QStringLiteral("quality")))
                lines.append(QStringLiteral("      Качество: %1; время: %2")
                    .arg(fan.value(QStringLiteral("quality")).toString(),
                         fan.value(QStringLiteral("observed_at")).toString(QStringLiteral("н/д"))));
        }
    }

    if (!current.isEmpty()) {
        lines.append(QString {});
        lines.append(QStringLiteral("-- Текущий снимок --"));
        lines.append(QStringLiteral("  CPU=%1; RAM=%2; GPU=%3; CPU temp=%4; GPU temp=%5")
            .arg(formatted(current.value(QStringLiteral("cpu_usage_percent")), QStringLiteral("%")),
                 formatted(current.value(QStringLiteral("ram_usage_percent")), QStringLiteral("%")),
                 formatted(current.value(QStringLiteral("gpu_usage_percent")), QStringLiteral("%")),
                 formatted(current.value(QStringLiteral("cpu_temperature_c")), QStringLiteral("°C")),
                 formatted(current.value(QStringLiteral("gpu_temperature_c")), QStringLiteral("°C"))));
    }

    lines.append(QString {});
    lines.append(QStringLiteral("=== Стресс-тест ==="));
    if (stress.value(QStringLiteral("stopped_early")).toBool()) {
        const auto reason = stress.value(QStringLiteral("safety_stop_reason")).toString();
        lines.append(reason.isEmpty()
            ? QStringLiteral("Остановлен досрочно: %1")
                  .arg(stress.value(QStringLiteral("stop_reason")).toString(QStringLiteral("причина не определена")))
            : QStringLiteral("Остановлен досрочно защитой: %1").arg(reason));
    }
    if (stress.value(QStringLiteral("run_cpu")).toBool()) {
        lines.append(QStringLiteral(
            "CPU: воркеров %1; нагрузочная фаза %2; средняя/пиковая измеренная загрузка %3 / %4.")
            .arg(stress.value(QStringLiteral("cpu_workers")).toInt())
            .arg(formatted(stress.value(QStringLiteral("cpu_actual_seconds")), QStringLiteral("сек")))
            .arg(formatted(stress.value(QStringLiteral("cpu_load_avg_percent")), QStringLiteral("%")),
                 formatted(stress.value(QStringLiteral("cpu_load_peak_percent")), QStringLiteral("%"))));
        lines.append(QStringLiteral("  Частота avg/min/max=%1 / %2 / %3; максимум температуры=%4")
            .arg(formatted(stress.value(QStringLiteral("cpu_avg_freq_mhz")), QStringLiteral("МГц")),
                 formatted(stress.value(QStringLiteral("cpu_min_freq_mhz")), QStringLiteral("МГц")),
                 formatted(stress.value(QStringLiteral("cpu_max_freq_mhz")), QStringLiteral("МГц")),
                 formatted(stress.value(QStringLiteral("cpu_max_temp_c")), QStringLiteral("°C"))));
        lines.append(QStringLiteral("  Падение частоты/workload=%1 / %2; throttling suspected=%3")
            .arg(formatted(stress.value(QStringLiteral("cpu_frequency_drop_percent")), QStringLiteral("%")),
                 formatted(stress.value(QStringLiteral("cpu_workload_drop_percent")), QStringLiteral("%")),
                 stress.contains(QStringLiteral("cpu_throttling_suspected"))
                    ? (stress.value(QStringLiteral("cpu_throttling_suspected")).toBool()
                        ? QStringLiteral("да") : QStringLiteral("не обнаружен по доступным метрикам"))
                    : QStringLiteral("не оценён")));
        lines.append(QStringLiteral("  Замеры: load=%1, frequency=%2, temperature=%3, workload=%4; ошибок воркеров=%5")
            .arg(stress.value(QStringLiteral("cpu_load_sample_count")).toInt())
            .arg(stress.value(QStringLiteral("cpu_frequency_sample_count")).toInt())
            .arg(stress.value(QStringLiteral("cpu_temperature_sample_count")).toInt())
            .arg(stress.value(QStringLiteral("cpu_workload_sample_count")).toInt())
            .arg(stress.value(QStringLiteral("cpu_worker_failures")).toInt()));
    }
    if (stress.value(QStringLiteral("run_gpu")).toBool()) {
        if (!stress.value(QStringLiteral("gpu_supported")).toBool()) {
            lines.append(QStringLiteral("GPU: НЕ ПРОВЕРЕНО — %1")
                .arg(stress.value(QStringLiteral("gpu_worker_error"))
                    .toString(QStringLiteral("аппаратный backend недоступен"))));
        } else {
            lines.append(QStringLiteral("GPU: аппаратный workload выполнен; renderer=%1; backend=%2; фаза %3; операций=%4; скорость=%5.")
                .arg(stress.value(QStringLiteral("gpu_renderer")).toString(QStringLiteral("н/д")),
                     stress.value(QStringLiteral("gpu_backend")).toString(QStringLiteral("н/д")),
                     formatted(stress.value(QStringLiteral("gpu_actual_seconds")), QStringLiteral("сек")),
                     formatted(stress.value(QStringLiteral("gpu_frames")), {}, 0),
                     formatted(stress.value(QStringLiteral("gpu_fps")), QStringLiteral("операций/с"))));
            lines.append(QStringLiteral("  Загрузка avg/peak=%1 / %2; температура max=%3; проверка результата=%4")
                .arg(formatted(stress.value(QStringLiteral("gpu_usage_avg_percent")), QStringLiteral("%")),
                     formatted(stress.value(QStringLiteral("gpu_usage_peak_percent")), QStringLiteral("%")),
                     formatted(stress.value(QStringLiteral("gpu_max_temp_c")), QStringLiteral("°C")),
                     yesNoUnknown(stress.value(QStringLiteral("gpu_output_verified")))));
            lines.append(QStringLiteral("  Замеры: load=%1, usage=%2, temperature=%3; throttling suspected=%4")
                .arg(stress.value(QStringLiteral("gpu_load_sample_count")).toInt())
                .arg(stress.value(QStringLiteral("gpu_usage_sample_count")).toInt())
                .arg(stress.value(QStringLiteral("gpu_temperature_sample_count")).toInt())
                .arg(stress.contains(QStringLiteral("gpu_throttling_suspected"))
                    ? (stress.value(QStringLiteral("gpu_throttling_suspected")).toBool()
                        ? QStringLiteral("да") : QStringLiteral("не обнаружен по доступным метрикам"))
                    : QStringLiteral("не оценён")));
        }
    }
    if (stress.value(QStringLiteral("run_disk")).toBool()) {
        lines.append(QStringLiteral(
            "Диск: раздел %1; устройство %2; filesystem=%3; записано %4; запись с fsync %5; "
            "повторное чтение %6 (чтение могло попасть в кэш ОС).")
            .arg(stress.value(QStringLiteral("disk_target_mountpoint")).toString(QStringLiteral("н/д")),
                 stress.value(QStringLiteral("disk_target_device")).toString(QStringLiteral("н/д")),
                 stress.value(QStringLiteral("disk_target_fstype")).toString(QStringLiteral("н/д")),
                 formatted(stress.value(QStringLiteral("disk_actual_mb")), QStringLiteral("МБ")),
                 formatted(stress.value(QStringLiteral("disk_write_mbps")), QStringLiteral("МБ/с")),
                 formatted(stress.value(QStringLiteral("disk_read_mbps")), QStringLiteral("МБ/с"))));
        if (stress.value(QStringLiteral("disk_target_is_memory_fs")).toBool()) {
            lines.append(QStringLiteral("  ВАЖНО: целевой каталог находится в memory-backed filesystem; скорость не характеризует физический накопитель."));
        }
        lines.append(QStringLiteral("  fsync=%1; проверка данных=%2")
            .arg(stress.value(QStringLiteral("disk_fsync_applied")).toBool()
                    || stress.value(QStringLiteral("disk_fsync_performed")).toBool()
                ? QStringLiteral("да") : QStringLiteral("нет"),
                 yesNoUnknown(stress.value(QStringLiteral("disk_data_verified")))));
    }
    if (!stress.value(QStringLiteral("run_cpu")).toBool()
        && !stress.value(QStringLiteral("run_gpu")).toBool()
        && !stress.value(QStringLiteral("run_disk")).toBool()) {
        lines.append(QStringLiteral("(данных нет)"));
    }

    const auto runtime = report.value(QStringLiteral("runtime")).toObject();
    lines.append(QString {});
    lines.append(QStringLiteral("=== Память во время диагностической сессии ==="));
    if (runtime.isEmpty()) {
        lines.append(QStringLiteral("(динамический замер paging не выполнялся)"));
    } else {
        lines.append(QStringLiteral("Доступно RAM: %1").arg(
            formatted(runtime.value(QStringLiteral("memory_available_percent")), QStringLiteral("%"))));
        lines.append(QStringLiteral("Swap/pagefile занят: %1").arg(
            formatted(runtime.value(QStringLiteral("swap_used_percent")), QStringLiteral("%"))));
        lines.append(QStringLiteral("Commit использован: %1; pagefile использован: %2")
            .arg(formatted(runtime.value(QStringLiteral("commit_used_percent")), QStringLiteral("%")),
                 formatted(runtime.value(QStringLiteral("pagefile_used_percent")), QStringLiteral("%"))));
        lines.append(QStringLiteral("Давление памяти: %1; paging=%2; hard faults=%3")
            .arg(runtime.value(QStringLiteral("memory_pressure")).toString(QStringLiteral("unknown")),
                 runtime.value(QStringLiteral("paging_activity")).toString(QStringLiteral("unknown")),
                 runtime.value(QStringLiteral("hard_fault_activity")).toString(QStringLiteral("unknown"))));
        lines.append(QStringLiteral("Интерпретация: %1")
            .arg(runtime.value(QStringLiteral("paging_interpretation")).toString(QStringLiteral("не удалось оценить"))));
        lines.append(QStringLiteral("Hard-fault Pages Input/sec=%1; Page Reads/sec=%2; context switches/sec=%3")
            .arg(formatted(runtime.value(QStringLiteral("pages_input_per_sec"))),
                 formatted(runtime.value(QStringLiteral("page_reads_per_sec"))),
                 formatted(runtime.value(QStringLiteral("system_context_switches_per_sec")))));
        lines.append(QStringLiteral("Диск: busy=%1; latency read/write=%2 / %3")
            .arg(formatted(runtime.value(QStringLiteral("disk_busy_percent")), QStringLiteral("%")),
                 formatted(runtime.value(QStringLiteral("disk_read_latency_ms")), QStringLiteral("мс")),
                 formatted(runtime.value(QStringLiteral("disk_write_latency_ms")), QStringLiteral("мс"))));
        lines.append(QStringLiteral("Качество/режим измерения: %1 / %2")
            .arg(runtime.value(QStringLiteral("data_quality")).toString(QStringLiteral("unknown")),
                 runtime.value(QStringLiteral("paging_sampling_mode")).toString(QStringLiteral("н/д"))));
    }

    lines.append(QString {});
    lines.append(QStringLiteral("=== Скорость интернета ==="));
    const auto speedValue = report.value(QStringLiteral("speed_test"));
    if (!speedValue.isObject()) {
        lines.append(QStringLiteral("(тест не запускался)"));
    } else {
        const auto speed = speedValue.toObject();
        if (!speed.value(QStringLiteral("ok")).toBool()) {
            lines.append(QStringLiteral("Проверка не удалась: %1")
                .arg(speed.value(QStringLiteral("error")).toString(QStringLiteral("н/д"))));
        } else {
            const auto server = speed.value(QStringLiteral("server")).toObject();
            lines.append(QStringLiteral("Замер: %1").arg(speed.value(QStringLiteral("tested_at")).toString(QStringLiteral("н/д"))));
            lines.append(QStringLiteral("  Загрузка: %1; отдача: %2; пинг: %3")
                .arg(formatted(speed.value(QStringLiteral("download_mbps")), QStringLiteral("Мбит/с")),
                     formatted(speed.value(QStringLiteral("upload_mbps")), QStringLiteral("Мбит/с")),
                     formatted(speed.value(QStringLiteral("ping_ms")), QStringLiteral("мс"))));
            lines.append(QStringLiteral("  Сервер: %1 (%2, %3)")
                .arg(server.value(QStringLiteral("sponsor")).toString(QStringLiteral("н/д")),
                     server.value(QStringLiteral("name")).toString(QStringLiteral("н/д")),
                     server.value(QStringLiteral("country")).toString(QStringLiteral("н/д"))));
        }
    }

    const auto incident = report.value(QStringLiteral("incident")).toObject();
    if (!incident.isEmpty()) {
        const auto summary = incident.value(QStringLiteral("summary")).toObject();
        const auto correlation = incident.value(QStringLiteral("recent_system_error_correlation")).toObject();
        lines.append(QString {});
        lines.append(QStringLiteral("=== Отметка «Проблема произошла сейчас» ==="));
        lines.append(QStringLiteral("Отмечено: %1")
            .arg(incident.value(QStringLiteral("marked_at")).toString(
                incident.value(QStringLiteral("marker_timestamp")).toString(QStringLiteral("н/д")))));
        lines.append(QStringLiteral("Статус: %1; качество окна: %2; замеров: %3")
            .arg(incident.value(QStringLiteral("status")).toString(QStringLiteral("unknown")),
                 summary.value(QStringLiteral("data_quality")).toString(QStringLiteral("unknown")))
            .arg(summary.value(QStringLiteral("sample_count")).toInt()));
        lines.append(QStringLiteral("Новых событий после отметки: %1; событий по времени около отметки: %2; ближайшее смещение=%3")
            .arg(incident.value(QStringLiteral("new_system_errors")).toArray().size())
            .arg(incident.value(QStringLiteral("recent_system_errors")).toArray().size())
            .arg(formatted(correlation.value(QStringLiteral("closest_offset_seconds")), QStringLiteral("с"))));
        if (summary.value("window_contract") == "filtered_temporal_window_v1") {
            lines.append(QStringLiteral("Покрытие относится только к окну времени, не к свежести всех датчиков или исправности ПК."));
            lines.append(QStringLiteral("Пробелы: начало %1; максимальный внутри %2; конец %3")
                .arg(formatted(summary.value("leading_gap_seconds"), QStringLiteral("с")),
                     formatted(summary.value("max_sample_gap_seconds"), QStringLiteral("с")),
                     formatted(summary.value("trailing_gap_seconds"), QStringLiteral("с"))));
            lines.append(QStringLiteral("Фильтрация: вне окна %1; неверное время %2; повторы %3; конфликтные метки %4")
                .arg(summary.value("outside_window_count").toInt()).arg(summary.value("invalid_timestamp_count").toInt())
                .arg(summary.value("duplicate_sample_count").toInt()).arg(summary.value("conflicting_timestamp_count").toInt()));
            lines.append(QStringLiteral("Сеть в фокусе (суммы только известных интервалов, не полный итог): ошибки %1, потери %2")
                .arg(formatted(summary.value("focus_net_error_count"), {}, 0),
                     formatted(summary.value("focus_net_drop_count"), {}, 0)));
            lines.append(QStringLiteral("Известные счётчики: ошибки %1/%3, потери %2/%3 замеров; качество %4 / %5")
                .arg(summary.value("net_errors_known_sample_count").toInt()).arg(summary.value("net_drops_known_sample_count").toInt())
                .arg(summary.value("focus_sample_count").toInt()).arg(summary.value("net_errors_data_quality").toString(),
                     summary.value("net_drops_data_quality").toString()));
        }
        if (summary.value("measurement_contract") == "fresh_incident_observations_v1") {
            lines.append(QStringLiteral("Исходное время проверено: возраст ≤3 сек; совпадения в одном замере с разбросом ≤1 сек. Причина не доказана."));
            lines.append(QStringLiteral("Исключено старых показателей: %1; повторов сетевых интервалов: %2; старых контекстов приложения: %3")
                .arg(summary.value("stale_metric_count").toInt()).arg(summary.value("repeated_interval_count").toInt())
                .arg(summary.value("stale_app_sample_count").toInt()));
            lines.append(QStringLiteral("Свежие совпадения: RAM %1; CPU %2; приложение %3")
                .arg(summary.value("memory_pressure_sample_count").toInt()).arg(summary.value("cpu_thermal_sample_count").toInt())
                .arg(summary.value("app_contributor_sample_count").toInt()));
            for (const auto& value : summary.value("cpu_thermal_observations").toArray()) {
                const auto point = value.toObject();
                lines.append(QStringLiteral("CPU: %1; нагрузка %2; температура %3; частота %4; снижение к baseline %5")
                    .arg(point.value("observed_at").toString(), formatted(point.value("cpu_percent"), "%"),
                        formatted(point.value("temperature_c"), "°C"), formatted(point.value("frequency_mhz"), QStringLiteral("МГц")),
                        formatted(point.value("drop_percent"), "%")));
            }
            for (const auto& value : summary.value("memory_pressure_observations").toArray()) {
                const auto point = value.toObject();
                lines.append(QStringLiteral("RAM: %1; доступно %2; %3 = %4")
                    .arg(point.value("observed_at").toString(), formatted(point.value("available_percent"), "%"),
                        point.value("counter").toString(), formatted(point.value("counter_value"), QStringLiteral("/с"))));
            }
        }
        if (incident.value("timing_contract") == "absolute_marker_steady_v1") {
            lines.append(QStringLiteral("Окно журнала: %1 сек до / %2 сек после; записей без разбираемого времени: %3")
                .arg(incident.value("log_pre_seconds").toDouble(), 0, 'f', 0)
                .arg(incident.value("post_seconds").toDouble(), 0, 'f', 0)
                .arg(correlation.value("unparseable_count").toInt()));
            lines.append(QStringLiteral("Фактически после метки: %1; ожидание окна: %2; задержка начала чтения журнала после срока: %3")
                .arg(formatted(incident.value("capture_elapsed_seconds"), QStringLiteral("с")),
                     formatted(incident.value("post_wait_seconds"), QStringLiteral("с")),
                     formatted(incident.value("log_after_delay_seconds"), QStringLiteral("с"))));
        }
    }

    const auto app = report.value(QStringLiteral("app_monitor")).toObject();
    if (!app.isEmpty()) {
        const auto summary = app.value(QStringLiteral("summary")).toObject();
        lines.append(QString {});
        lines.append(QStringLiteral("=== Наблюдение проблемного приложения ==="));
        lines.append(QStringLiteral("Приложение: %1").arg(app.value(QStringLiteral("exe_path")).toString(QStringLiteral("н/д"))));
        lines.append(QStringLiteral("Период: %1 → %2; вердикт: %3")
            .arg(app.value(QStringLiteral("started_at")).toString(QStringLiteral("н/д")),
                 app.value(QStringLiteral("finished_at")).toString(QStringLiteral("н/д")),
                 app.value(QStringLiteral("verdict")).toString(QStringLiteral("н/д"))));
        lines.append(QStringLiteral("Замеров: %1; CPU avg/peak=%2 / %3; RAM start/end/peak=%4 / %5 / %6")
            .arg(summary.value(QStringLiteral("sample_count")).toInt())
            .arg(formatted(summary.value(QStringLiteral("cpu_avg_percent")), QStringLiteral("%")),
                 formatted(summary.value(QStringLiteral("cpu_peak_percent")), QStringLiteral("%")),
                 formatted(summary.value(QStringLiteral("ram_start_mb")), QStringLiteral("МБ")),
                 formatted(summary.value(QStringLiteral("ram_end_mb")), QStringLiteral("МБ")),
                 formatted(summary.value(QStringLiteral("ram_peak_mb")), QStringLiteral("МБ"))));
        lines.append(QStringLiteral("Private memory start/end/peak=%1 / %2 / %3; handles peak=%4; page faults peak=%5")
            .arg(formatted(summary.value(QStringLiteral("private_start_mb")), QStringLiteral("МБ")),
                 formatted(summary.value(QStringLiteral("private_end_mb")), QStringLiteral("МБ")),
                 formatted(summary.value(QStringLiteral("private_peak_mb")), QStringLiteral("МБ")),
                 formatted(summary.value(QStringLiteral("handle_peak")), {}, 0),
                 formatted(summary.value(QStringLiteral("page_faults_peak_per_sec")), QStringLiteral("/с"))));
        lines.append(QStringLiteral("UI hung=%1; system available RAM min=%2; disk busy peak=%3; новых системных событий=%4")
            .arg(formatted(QJsonValue {summary.value(QStringLiteral("ui_hung_fraction")).toDouble() * 100.0}, QStringLiteral("%")),
                 formatted(summary.value(QStringLiteral("system_available_min_percent")), QStringLiteral("%")),
                 formatted(summary.value(QStringLiteral("system_disk_busy_peak_percent")), QStringLiteral("%")))
            .arg(app.value(QStringLiteral("new_system_errors")).toArray().size()));
        const auto appActions = app.value(QStringLiteral("action_plan")).toArray();
        if (!appActions.isEmpty()) {
            lines.append(QStringLiteral("Действия по приложению:"));
            for (qsizetype index = 0; index < std::min<qsizetype>(7, appActions.size()); ++index) {
                const auto action = appActions.at(index).toObject();
                lines.append(QStringLiteral("  %1. %2")
                    .arg(action.value(QStringLiteral("step")).toInt())
                    .arg(action.value(QStringLiteral("action")).toString()));
            }
        }
        const auto appCoverage = app.value(QStringLiteral("coverage")).toObject();
        if (!appCoverage.isEmpty()) {
            lines.append(QStringLiteral("Покрытие приложения: %1")
                .arg(appCoverage.value(QStringLiteral("message")).toString(QStringLiteral("н/д"))));
        }
    }

    lines.append(QString {});
    lines.append(QStringLiteral("=== Возможные проблемы и причины ==="));
    lines.append(QStringLiteral("-- Стоит ли беспокоиться --"));
    lines.append(risk.value(QStringLiteral("headline")).toString(QStringLiteral("Итог не сформирован")));
    lines.append(risk.value(QStringLiteral("summary")).toString());
    const auto positive = risk.value(QStringLiteral("positive_evidence")).toArray();
    if (!positive.isEmpty()) {
        lines.append(QStringLiteral("Что говорит в пользу нормальной работы:"));
        appendStringValues(lines, positive, QStringLiteral("  + "));
    }
    const auto limitations = risk.value(QStringLiteral("limitations")).toArray();
    if (!limitations.isEmpty()) {
        lines.append(QStringLiteral("Что осталось непроверенным:"));
        appendStringValues(lines, limitations, QStringLiteral("  - "));
    }

    lines.append(QString {});
    lines.append(QStringLiteral("-- Достоверность и покрытие --"));
    lines.append(coverage.value(QStringLiteral("message")).toString(QStringLiteral("н/д")));
    appendStringValues(lines, coverage.value(QStringLiteral("missing_checks")).toArray(), QStringLiteral("  - "));

    lines.append(QString {});
    lines.append(QStringLiteral("-- Главный вывод --"));
    const auto verdict = report.value(QStringLiteral("verdict_findings")).toArray();
    if (verdict.isEmpty()) {
        lines.append(QStringLiteral(
            "Явных критичных проблем по выполненным проверкам не обнаружено. "
            "Это не означает, что недоступные проверки пройдены успешно."));
        if (coverage.value(QStringLiteral("level")).toString() != QStringLiteral("complete")) {
            lines.append(coverage.value(QStringLiteral("message")).toString());
        }
    } else {
        for (int index = 0; index < verdict.size(); ++index) {
            appendFinding(lines, verdict.at(index).toObject(), index + 1);
        }
    }

    lines.append(QString {});
    lines.append(QStringLiteral("-- Что сделать сначала / План действий --"));
    const auto actions = report.value(QStringLiteral("action_plan")).toArray();
    if (actions.isEmpty()) {
        lines.append(QStringLiteral("Нет срочных действий по доступным данным; сохраняйте наблюдение и воспроизводимость."));
    }
    for (const auto& value : actions) {
        const auto action = value.toObject();
        lines.append(QStringLiteral("%1. %2 — %3")
            .arg(action.value(QStringLiteral("step")).toInt())
            .arg(action.value(QStringLiteral("action")).toString(),
                 action.value(QStringLiteral("reason")).toString(QStringLiteral("основание не указано"))));
    }

    lines.append(QString {});
    lines.append(QStringLiteral("-- Все находки / Диагностические находки --"));
    const auto findings = report.value(QStringLiteral("findings")).toArray();
    if (findings.isEmpty()) {
        lines.append(QStringLiteral(
            "Явных проблем по имеющимся данным не обнаружено. Это не гарантия полного здоровья: "
            "проверены только доступные источники, а недоступные данные не считаются нормой."));
    }
    QList<QJsonObject> orderedFindings;
    orderedFindings.reserve(findings.size());
    for (const auto& value : findings) {
        orderedFindings.append(value.toObject());
    }
    const auto severityRank = [](const QString& severity) {
        if (severity == QStringLiteral("critical")) return 0;
        if (severity == QStringLiteral("warning")) return 1;
        return 2;
    };
    std::stable_sort(orderedFindings.begin(), orderedFindings.end(),
        [&severityRank](const QJsonObject& left, const QJsonObject& right) {
            const auto leftRank = severityRank(left.value(QStringLiteral("severity")).toString());
            const auto rightRank = severityRank(right.value(QStringLiteral("severity")).toString());
            if (leftRank != rightRank) return leftRank < rightRank;
            return left.value(QStringLiteral("priority")).toInt()
                > right.value(QStringLiteral("priority")).toInt();
        });
    for (const auto& finding : orderedFindings) {
        appendFinding(lines, finding);
    }
    lines.append(QStringLiteral("Примечание: UNKNOWN означает отсутствие надёжных данных, а не исправность компонента."));
    return lines.join(u'\n');
}

} // namespace orion::diagnostics
