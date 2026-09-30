#include "deep_scan_worker.h"
#include "hardware_inventory_worker.h"
#include "orion/core/memory_pressure.h"
#include "orion/core/thresholds.h"
#include "orion/diagnostics/report_contract.h"
#include "orion/diagnostics/system_diagnostics_collector.h"
#include "orion/diagnostics/telemetry_json.h"
#include "orion/platform/autostart_collector.h"
#include "orion/platform/system_backend.h"
#include "scan_report_sections.h"
#include "stress_worker.h"

#include <QDateTime>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace orion::app
{
namespace
{

[[nodiscard]] bool usableQuality(const QString& quality)
{
    return quality == QStringLiteral("valid") || quality == QStringLiteral("estimated");
}

[[nodiscard]] QString combinedSensorQuality(const QJsonObject& temperatures,
                                            const QJsonObject& fans)
{
    const auto temperatureQuality = temperatures.value(QStringLiteral("quality")).toString();
    const auto fanQuality = fans.value(QStringLiteral("quality")).toString();
    if (temperatureQuality == fanQuality)
        return temperatureQuality.isEmpty() ? QStringLiteral("unknown") : temperatureQuality;
    return QStringLiteral("partial");
}

[[nodiscard]] QJsonObject temperatureState(const QJsonObject& metric,
                                           const QString& component)
{
    const QString metricQuality = metric.value(QStringLiteral("quality")).toString(
        QStringLiteral("unsupported"));
    std::optional<double> value;
    QString source = metric.value(QStringLiteral("source")).toString();
    QString reason = metric.value(QStringLiteral("reason")).toString();
    QString quality = metricQuality;
    if (metricQuality == QStringLiteral("stale") && reason.isEmpty())
        reason = QStringLiteral("Последний замер устарел; текущая температура неизвестна.");
    if (metric.value(QStringLiteral("value")).isDouble() && usableQuality(metricQuality)
        && std::isfinite(metric.value(QStringLiteral("value")).toDouble()))
        value = metric.value(QStringLiteral("value")).toDouble();
    // The backend owns primary-channel selection. A CPU core or GPU memory/hotspot
    // channel must not replace a missing CPU Package / GPU Core measurement.
    const auto level = orion::core::levelForTemperature(
        value, component == QStringLiteral("cpu") ? std::string_view{"cpu"} : std::string_view{"gpu"});
    const auto levelText = QString::fromLatin1(orion::core::toString(level).data(),
                                               static_cast<qsizetype>(orion::core::toString(level).size()));
    QJsonObject state{{QStringLiteral("level"), levelText},
                      {QStringLiteral("status"), levelText},
                      {QStringLiteral("duration_seconds"), 0.0},
                      {QStringLiteral("data_quality"), quality}};
    state.insert("observed_at", metric.value("observed_at"));
    if (value.has_value())
        state.insert(QStringLiteral("current_value_c"), *value);
    if (!source.isEmpty())
        state.insert(QStringLiteral("source"), source);
    if (!reason.isEmpty())
        state.insert(QStringLiteral("reason"), reason);
    return state;
}

} // namespace

QJsonObject DeepScanWorker::sensorSnapshot(const orion::core::TelemetryData& data)
{
    const auto telemetry = orion::diagnostics::telemetryToJson(data);
    const auto metrics = telemetry.value(QStringLiteral("metrics")).toObject();
    auto temperatureMetric = metrics.value(QStringLiteral("temperature_sensors")).toObject();
    auto fanMetric = metrics.value(QStringLiteral("fan_sensors")).toObject();
    const auto rows = [](const QJsonObject& metric)
    {
        QJsonArray result;
        const auto quality = metric.value("quality").toString();
        // Keep stale evidence identifiable; never publish unusable retained values.
        if (!usableQuality(quality) && quality != QStringLiteral("stale"))
            return result;
        for (const auto& value : metric.value("value").toArray())
        {
            auto row = value.toObject();
            row.insert("quality", quality);
            row.insert("observed_at", metric.value("observed_at"));
            result.append(row);
        }
        return result;
    };
    const auto temperatures = rows(temperatureMetric);
    const auto fans = rows(fanMetric);
    temperatureMetric.remove("value");
    fanMetric.remove("value");
    QJsonArray sources;
    const auto appendSources = [&sources](const QJsonArray& rows)
    {
        for (const auto& value : rows)
        {
            const auto source = value.toObject().value(QStringLiteral("source")).toString();
            if (!source.isEmpty() && !sources.contains(source))
                sources.append(source);
        }
    };
    appendSources(temperatures);
    appendSources(fans);
    QJsonArray notes;
    for (const auto& metric : {temperatureMetric, fanMetric})
    {
        const auto source = metric.value("source").toString();
        if (!source.isEmpty() && !sources.contains(source))
            sources.append(source);
        const auto reason = metric.value(QStringLiteral("reason")).toString();
        if (!reason.isEmpty() && !notes.contains(reason))
            notes.append(reason);
    }
    QJsonObject sensors{{QStringLiteral("captured_at"), telemetry.value(QStringLiteral("observed_at"))},
                        {QStringLiteral("data_quality"),
                         combinedSensorQuality(temperatureMetric, fanMetric)},
                        {QStringLiteral("sources"), sources},
                        {QStringLiteral("temperatures"), temperatures},
                        {QStringLiteral("fans"), fans},
                        {QStringLiteral("temperature_collection"), temperatureMetric},
                        {QStringLiteral("fan_collection"), fanMetric},
                        {QStringLiteral("notes"), notes}};
    const auto temperature = QJsonObject{
        {QStringLiteral("cpu"),
         temperatureState(metrics.value(QStringLiteral("cpu_temp_c")).toObject(),
                          QStringLiteral("cpu"))},
        {QStringLiteral("gpu"),
         temperatureState(metrics.value(QStringLiteral("gpu_temp_c")).toObject(),
                          QStringLiteral("gpu"))}};
    QJsonObject current;
    for (const auto& component : {QStringLiteral("cpu"), QStringLiteral("gpu")})
    {
        const auto measured = temperature.value(component).toObject().value("current_value_c");
        if (measured.isDouble())
            current.insert(component + QStringLiteral("_temperature_c"), measured);
    }
    for (const auto& pair : {std::pair{"cpu_usage_percent", "cpu_usage_percent"},
                             std::pair{"gpu_usage_percent", "gpu_usage_percent"},
                             std::pair{"ram_used_percent", "ram_usage_percent"}})
    {
        const auto metric = metrics.value(QLatin1String(pair.first)).toObject();
        if (usableQuality(metric.value("quality").toString()) && metric.value("value").isDouble())
            current.insert(QLatin1String(pair.second), metric.value("value"));
    }
    return {{QStringLiteral("temperature"), temperature}, {QStringLiteral("sensors"), sensors},
            {QStringLiteral("current"), current}};
}

DeepScanWorker::DeepScanWorker(QObject* parent) : DeepScanWorker(executeNative, parent) {}
DeepScanWorker::DeepScanWorker(StepFunction execute, QObject* parent)
    : QThread(parent), execute_(std::move(execute))
{
    qRegisterMetaType<QJsonObject>();
}

bool DeepScanWorker::startScan(const QJsonObject& snapshot, const QJsonObject& hardwareSeed, bool confirmed)
{
    if (!confirmed || isRunning() || !execute_)
        return false;
    snapshot_ = snapshot;
    hardwareSeed_ = hardwareSeed;
    cancelled_.store(false, std::memory_order_release);
    start();
    return true;
}

void DeepScanWorker::requestStop() { cancelled_.store(true, std::memory_order_release); }

QJsonObject DeepScanWorker::executeNative(DeepScanStep step, const QJsonObject& seed,
                                          const Cancel& cancelled, const Progress& progress)
{
    switch (step)
    {
    case DeepScanStep::Hardware:
    {
        HardwareInventoryWorker collector;
        QObject::connect(
            &collector, &HardwareInventoryWorker::progressChanged, &collector,
            [&](int completed, int total, const QString& label)
            { progress(total ? completed * 100 / total : 0, label); }, Qt::DirectConnection);
        return collector.collectReport(seed, cancelled);
    }
    case DeepScanStep::Smart:
        return orion::diagnostics::collectSmartReport();
    case DeepScanStep::LogsBefore:
    case DeepScanStep::LogsAfter:
        return orion::diagnostics::collectSystemErrorReport(100);
    case DeepScanStep::Autostart:
    {
        QJsonArray entries;
        auto collector = orion::platform::makeAutostartCollector();
        if (!collector)
            return {{"data_quality", "unsupported"}, {"entries", entries}};
        const auto result = collector->scan();
        for (const auto& entry : result.entries)
        {
            entries.append(QJsonObject{
                {"name", QString::fromStdString(entry.name)},
                {"command", QString::fromStdString(entry.command)},
                {"source", QString::fromStdString(entry.source)},
                {"category", QString::fromLatin1(orion::core::toString(entry.category))},
                {"enabled", entry.enabled ? QJsonValue{*entry.enabled} : QJsonValue{QJsonValue::Null}}});
        }
        return {{"data_quality", result.reason.empty() ? "valid" : "unknown"},
                {"entries", entries},
                {"source", QString::fromStdString(result.source)},
                {"note", QString::fromStdString(result.reason)}};
    }
    case DeepScanStep::MemoryBefore:
    case DeepScanStep::MemoryAfter:
    {
        auto backend = orion::platform::makeSystemBackend();
        if (!backend)
            return {{"data_quality", "unsupported"}};
        const auto data = backend->sample(std::chrono::milliseconds{200});
        const auto value = [](const orion::core::Metric<double>& metric)
        { return metric.usable() ? QJsonValue{*metric.value} : QJsonValue{QJsonValue::Null}; };
        const auto optional = [](const orion::core::Metric<double>& metric)
        { return metric.usable() ? metric.value : std::optional<double>{}; };
        const auto memory = orion::core::assessWindowsPaging(
            optional(data.ramAvailablePercent), optional(data.commitUsedPercent),
            optional(data.pagesInputPerSecond), optional(data.pageReadsPerSecond),
            optional(data.pagesPerSecond));
        return {{"data_quality", data.pagesInputPerSecond.usable() || data.pageReadsPerSecond.usable() ||
                                         data.pagesPerSecond.usable()
                                     ? "estimated"
                                     : "unknown"},
                {"memory_data_quality", data.ramUsagePercent.usable() ? "valid" : "unknown"},
                {"observed_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"ram_used_percent", value(data.ramUsagePercent)},
                {"ram_available_percent", value(data.ramAvailablePercent)},
                {"memory_available_percent", value(data.ramAvailablePercent)},
                {"swap_used_percent", value(data.swapUsedPercent)},
                {"paging_sampling_mode", "endpoint_rates"},
                {"commit_used_percent", value(data.commitUsedPercent)},
                {"pagefile_used_percent", value(data.pagefileUsedPercent)},
                {"pages_input_per_sec", value(data.pagesInputPerSecond)},
                {"page_reads_per_sec", value(data.pageReadsPerSecond)},
                {"memory_pressure", QString::fromStdString(memory.memoryPressure)},
                {"paging_activity", QString::fromStdString(memory.pagingActivity)},
                {"hard_fault_activity", QString::fromStdString(memory.hardFaultActivity)},
                {"paging_interpretation", QString::fromStdString(memory.interpretation)}};
    }
    case DeepScanStep::Stress:
    {
        // This branch is reachable only after startScan's explicit confirmation.
        StressWorker worker;
        QJsonObject report;
        QObject::connect(&worker, &StressWorker::progressChanged, &worker, progress, Qt::DirectConnection);
        QObject::connect(
            &worker, &StressWorker::testCompleted, &worker,
            [&](const QJsonObject& result) { report = result; }, Qt::DirectConnection);
        StressOptions options;
        options.runCpu = options.runGpu = options.runDisk = true;
        options.durationSeconds = 30;
        options.diskSizeMiB = 200;
        options.diskDirectory = seed.value(QStringLiteral("disk_directory")).toString();
        if (cancelled())
            return {};
        worker.startTest(options);
        while (!worker.wait(50))
        {
            if (cancelled())
                worker.requestStop();
        }
        return report;
    }
    case DeepScanStep::SensorsAfter:
    {
        auto backend = orion::platform::makeSystemBackend();
        if (!backend)
        {
            return {{QStringLiteral("temperature"),
                     QJsonObject{{QStringLiteral("cpu"),
                                  temperatureState({}, QStringLiteral("cpu"))},
                                 {QStringLiteral("gpu"),
                                  temperatureState({}, QStringLiteral("gpu"))}}},
                    {QStringLiteral("sensors"),
                     QJsonObject{{QStringLiteral("captured_at"),
                                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                                 {QStringLiteral("data_quality"), QStringLiteral("unsupported")},
                                 {QStringLiteral("sources"), QJsonArray{}},
                                 {QStringLiteral("temperatures"), QJsonArray{}},
                                 {QStringLiteral("fans"), QJsonArray{}},
                                 {QStringLiteral("notes"),
                                  QJsonArray{QStringLiteral("Системный сборщик недоступен")}}}}};
        }
        return sensorSnapshot(backend->sample(std::chrono::milliseconds{200}));
    }
    }
    throw std::runtime_error("Unknown deep scan step");
}

void DeepScanWorker::run()
{
    const Cancel cancelled = [this] { return cancelled_.load(std::memory_order_acquire); };
    QJsonObject snapshot = snapshot_;
    // Deep is a fresh local run, never a rebranding of cached stress/Internet
    // results.
    for (const auto* key : {"stress_test", "speed_test", "network", "incident", "app_monitor", "runtime",
                            "hardware", "autostart_entries", "autostart_collection", "autostart_collected"})
        snapshot.remove(QLatin1String(key));
    // No cached evidence can masquerade as an endpoint of this new scan.
    QJsonObject diagnostics{
        {"sensors", QJsonObject{{"data_quality", "not_collected"},
                                 {"notes", QJsonArray{QStringLiteral("Снимок после нагрузки не собран.")}}}}};
    QJsonObject beforeMemory;
    QJsonObject beforeLogs;
    QString failure;
    QStringList completed;
    int progressValue = 0;
    const auto execute = [&](DeepScanStep step, int begin, int end, const QString& label,
                             const QJsonObject& seed = QJsonObject{})
    {
        if (cancelled())
            return QJsonObject{};
        emit logLine(label);
        emit progressChanged(begin, label);
        auto result = execute_(step, seed, cancelled,
                               [&](int value, const QString& text)
                               {
                                   progressValue =
                                       std::max(progressValue,
                                                begin + std::clamp(value, 0, 100) * (end - begin) / 100);
                                   emit progressChanged(progressValue, text);
                                   if (!text.isEmpty())
                                       emit logLine(text);
                               });
        if (!cancelled())
        {
            progressValue = end;
            completed.append(label);
            emit progressChanged(end, label);
        }
        return result;
    };
    emit logLine(QStringLiteral("Глубокая диагностика — локальная проверка без интернет-запросов."));
    try
    {
        snapshot.insert("hardware", execute(DeepScanStep::Hardware, 0, 8,
                                            QStringLiteral("Характеристики ПК"), hardwareSeed_));
        diagnostics.insert("smart",
                           execute(DeepScanStep::Smart, 8, 20, QStringLiteral("SMART накопителей")));
        beforeLogs = execute(DeepScanStep::LogsBefore, 20, 28, QStringLiteral("Журнал ОС до нагрузки"));
        diagnostics.insert("log_errors", beforeLogs);
        const auto autostart = execute(DeepScanStep::Autostart, 28, 36, QStringLiteral("Автозагрузка"));
        snapshot.insert("autostart_entries", autostart.value("entries"));
        snapshot.insert("autostart_collection", autostart);
        snapshot.insert("autostart_collected", autostart.value("data_quality") == "valid");
        beforeMemory = execute(DeepScanStep::MemoryBefore, 36, 36, QStringLiteral("Память до нагрузки"));
        snapshot.insert("runtime", QJsonObject{{"before", beforeMemory},
                                               {"observation_kind", "before_only"},
                                               {"data_quality", "unknown"}});
        const auto stress =
            execute(DeepScanStep::Stress, 36, 90, QStringLiteral("Нагрузка CPU → GPU → диск"),
                    {{"disk_directory", snapshot_.value("deep_scan_disk_directory")}});
        snapshot.insert("stress_test", stress);
        if (!cancelled())
        {
            auto after =
                execute(DeepScanStep::MemoryAfter, 90, 92, QStringLiteral("Память после нагрузки"));
            after.insert("before", beforeMemory);
            after.insert("observation_kind", "before_after_snapshots");
            snapshot.insert("runtime", after);
            const auto afterLogs =
                execute(DeepScanStep::LogsAfter, 92, 95, QStringLiteral("Журнал ОС после нагрузки"));
            diagnostics.insert("log_errors", afterLogs);
            diagnostics.insert("log_errors_before_stress", beforeLogs);
            diagnostics.insert("log_errors_after_stress",
                               orion::diagnostics::diffSystemErrorReports(beforeLogs, afterLogs));
            const auto sensors = execute(DeepScanStep::SensorsAfter, 95, 98,
                                         QStringLiteral("Датчики после нагрузки"));
            if (!sensors.isEmpty())
            {
                diagnostics.insert(QStringLiteral("temperature"), sensors.value("temperature"));
                diagnostics.insert(QStringLiteral("sensors"), sensors.value("sensors"));
                diagnostics.insert(QStringLiteral("current"), sensors.value("current"));
            }
        }
    }
    catch (const std::exception& error)
    {
        failure = QString::fromUtf8(error.what());
    }
    catch (...)
    {
        failure = QStringLiteral("Неизвестная ошибка сборщика");
    }
    snapshot.insert("diagnostics", diagnostics);
    auto report = orion::diagnostics::buildReport(snapshot);
    report.insert("hardware", snapshot.value("hardware"));
    report.insert("autostart_entries", snapshot.value("autostart_entries"));
    report.insert("autostart_collection", snapshot.value("autostart_collection"));
    report.insert("tray", snapshot.value("tray"));
    const bool interrupted = cancelled();
    const bool safety = snapshot.value("stress_test").toObject().value("stopped_early").toBool();
    const QString state = interrupted          ? QStringLiteral("cancelled")
                          : !failure.isEmpty() ? QStringLiteral("failed")
                          : safety             ? QStringLiteral("partial")
                                               : QStringLiteral("complete");
    report.insert("scan", QJsonObject{{"kind", "deep_local"},
                                      {"state", state},
                                      {"error", failure},
                                      {"completed_steps", QJsonArray::fromStringList(completed)},
                                      {"includes_internet", false}});
    if (!failure.isEmpty())
        emit logLine(QStringLiteral("Ошибка: %1").arg(failure));
    if (!interrupted && failure.isEmpty())
        emit progressChanged(100,
                             safety ? QStringLiteral("Проверка завершена досрочно защитой — отчёт неполный")
                                    : QStringLiteral("Отчёт сформирован"));
    emit reportReady(report);
}

QString DeepScanWorker::reportText(const QJsonObject& report)
{
    const auto scan = report.value("scan").toObject();
    const auto state = scan.value("state").toString();
    const QString stateText =
        state == QStringLiteral("complete") ? QStringLiteral("этапы завершены; полнота данных указана ниже")
        : state == QStringLiteral("cancelled") ? QStringLiteral("остановлено, частичный отчёт")
        : state == QStringLiteral("failed")    ? QStringLiteral("ошибка сборщика, частичный отчёт")
                                               : QStringLiteral("досрочное завершение, частичный отчёт");
    QString text = QStringLiteral("O.R.I.O.N. — ГЛУБОКАЯ ДИАГНОСТИКА\nСостояние: "
                                  "%1\nБез интернет-запросов.\n\n")
                       .arg(stateText);
    if (!scan.value("error").toString().isEmpty())
        text += QStringLiteral("Ошибка: %1\n").arg(scan.value("error").toString());
    text += orion::diagnostics::reportToText(report);
    text += formatExtendedScanSections(report);
    return text;
}

} // namespace orion::app
