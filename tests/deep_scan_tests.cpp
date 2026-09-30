#include "deep_scan_dialog.h"
#include "deep_scan_worker.h"
#include "orion/diagnostics/report_contract.h"
#include "orion/core/telemetry_data.h"

#include <QApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace orion::app;

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    bool ok = true;
    const auto check = [&](bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ok = false;
        }
    };
    using namespace orion::core;
    TelemetryData sensorData;
    sensorData.cpuTemperatureC = Metric<double>::valid(61.0, "primary CPU");
    sensorData.gpuTemperatureC = Metric<double>::unavailable(DataQuality::Unsupported, "GPU provider");
    sensorData.temperatures = Metric<std::vector<TemperatureSensor>>::valid({
        {SensorComponent::Cpu, "Core #0", 99.0, {}, {}, "core provider", "core-0"},
        {SensorComponent::Gpu, "GPU Memory", 105.0, {}, {}, "memory provider", "gpu-memory"}});
    sensorData.fans = Metric<std::vector<FanSensor>>::unavailable(
        DataQuality::PermissionDenied, "fan provider", "Fixture access denied");
    sensorData.ramUsagePercent = Metric<double>::valid(42.0);
    const auto converted = DeepScanWorker::sensorSnapshot(sensorData);
    check(converted.value("temperature").toObject().value("cpu").toObject().value("current_value_c") == 61.0
          && converted.value("temperature").toObject().value("gpu").toObject().value("level") == "unknown"
          && converted.value("current").toObject().value("ram_usage_percent") == 42.0
          && !converted.value("current").toObject().contains("gpu_temperature_c"),
          "Primary channel replaced by arbitrary core/memory sensor or current snapshot lost.");
    check(converted.value("sensors").toObject().value("data_quality") == "partial"
          && converted.value("sensors").toObject().value("fan_collection").toObject().value("quality")
              == "permission_denied", "A working temperature hid failed fan collection.");
    sensorData.cpuTemperatureC.quality = DataQuality::Stale;
    sensorData.temperatures.quality = DataQuality::Stale;
    const auto stale = DeepScanWorker::sensorSnapshot(sensorData);
    check(stale.value("temperature").toObject().value("cpu").toObject().value("level") == "unknown"
          && !stale.value("current").toObject().contains("cpu_temperature_c")
          && stale.value("sensors").toObject().value("temperatures").toArray().first()
                 .toObject().value("quality") == "stale",
          "Stale measurements became current or valid channels.");
    auto staleReport = orion::diagnostics::buildReport({{"diagnostics", stale}});
    const auto staleText = DeepScanWorker::reportText(staleReport);
    check(staleText.contains(QStringLiteral("качество stale")) && staleText.contains("Fixture access denied"),
          "Stale evidence or provider error hidden in readable report.");
    sensorData.temperatures.quality = DataQuality::CollectorError;
    check(DeepScanWorker::sensorSnapshot(sensorData).value("sensors").toObject()
              .value("temperatures").toArray().isEmpty(),
          "Retained values from failed collector published as measured temperatures.");
    sensorData.cpuTemperatureC = Metric<double>::valid(0.0, "zero fixture");
    check(DeepScanWorker::sensorSnapshot(sensorData).value("temperature").toObject().value("cpu")
              .toObject().value("current_value_c") == 0.0, "Real zero temperature lost.");

    QVector<DeepScanStep> order;
    const auto fixture = [&](DeepScanStep step, const QJsonObject&, const DeepScanWorker::Cancel&,
                             const DeepScanWorker::Progress& progress) -> QJsonObject
    {
        order.append(step);
        progress(70, QStringLiteral("ТЕСТОВЫЕ ДАННЫЕ — нагрузка не запускается"));
        progress(10, QStringLiteral("Проверка монотонности прогресса"));
        switch (step)
        {
        case DeepScanStep::Hardware:
            return {{"cpu", QJsonObject{{"model", "Fixture CPU"}}}, {"collection_partial", false}};
        case DeepScanStep::Smart:
            return {{"available", false},
                    {"data_quality", "unsupported"},
                    {"note", "Fixture: SMART unavailable"}};
        case DeepScanStep::LogsBefore:
            return {{"data_quality", "valid"}, {"errors", QJsonArray{}}, {"source", "fixture"}};
        case DeepScanStep::LogsAfter:
            return {{"data_quality", "valid"},
                    {"errors", QJsonArray{"Fixture new event"}},
                    {"source", "fixture"}};
        case DeepScanStep::Autostart:
            return {
                {"data_quality", "valid"},
                {"entries", QJsonArray{QJsonObject{{"name", "Fixture app"}, {"command", "fixture.exe"}}}}};
        case DeepScanStep::MemoryBefore:
        case DeepScanStep::MemoryAfter:
            return {
                {"data_quality", "valid"}, {"ram_used_percent", 50}, {"memory_pressure", "unconfirmed"}};
        case DeepScanStep::Stress:
            return {{"run_cpu", true},
                    {"run_gpu", true},
                    {"run_disk", true},
                    {"stopped_early", false},
                    {"cpu_worker_failures", 1}};
        case DeepScanStep::SensorsAfter:
            return {
                {"temperature",
                 QJsonObject{{"cpu", QJsonObject{{"level", "warning"},
                                                  {"status", "warning"},
                                                  {"current_value_c", 72.5},
                                                  {"data_quality", "valid"},
                                                  {"source", "fixture-sensor"}}},
                             {"gpu", QJsonObject{{"level", "unknown"},
                                                  {"status", "unknown"},
                                                  {"data_quality", "unsupported"},
                                                  {"reason", "Fixture GPU sensor unavailable"}}}}},
                {"sensors",
                 QJsonObject{{"captured_at", "2026-09-14T12:00:00.000Z"},
                             {"data_quality", "valid"},
                             {"sources", QJsonArray{"fixture-sensor"}},
                             {"temperatures",
                              QJsonArray{QJsonObject{{"component", "cpu"},
                                                     {"label", "CPU Package"},
                                                     {"value_c", 72.5},
                                                     {"source", "fixture-sensor"}}}},
                             {"fans",
                              QJsonArray{QJsonObject{{"component", "cpu"},
                                                     {"label", "CPU Fan"},
                                                     {"rpm", 1250.0},
                                                     {"source", "fixture-sensor"}}}}}}};
        }
        return {};
    };
    // Hold the first run until the duplicate-start assertion has executed.
    // A fast fixture may otherwise legitimately finish before that assertion.
    std::atomic_bool releaseFirstRun { false };
    DeepScanWorker worker([&](DeepScanStep step, const QJsonObject& seed,
        const DeepScanWorker::Cancel& cancel, const DeepScanWorker::Progress& progress) {
        while (!releaseFirstRun.load() && !cancel()) QThread::msleep(1);
        return fixture(step, seed, cancel, progress);
    });
    QJsonObject result;
    int lastProgress = 0;
    bool monotonic = true;
    QObject::connect(&worker, &DeepScanWorker::progressChanged, &application,
                     [&](int value, const QString&)
                     {
                         monotonic &= value >= lastProgress && value >= 0 && value <= 100;
                         lastProgress = value;
                     });
    QObject::connect(&worker, &DeepScanWorker::reportReady, &application,
                     [&](const QJsonObject& report) { result = report; });
    const auto awaitWorker = [&](DeepScanWorker& target)
    {
        QEventLoop loop;
        QObject::connect(&target, &QThread::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        if (target.isRunning())
            loop.exec();
        if (target.isRunning())
        {
            target.requestStop();
            target.wait();
            ok = false;
        }
        QCoreApplication::processEvents();
    };
    check(!worker.startScan({}, {}, false) && !worker.isRunning() && order.isEmpty(),
          "Unconfirmed scan executed.");
    QWidget parent;
    parent.setStyleSheet(
        QStringLiteral("QWidget { background:#0A0A0C; color:#E8E8EC; font-family:Consolas; "
                       "font-size:13px; } "
                       "QLabel#DeepScanVerdict { background:#141417; border-left:4px solid "
                       "#F5C518; padding:8px; } "
                       "QPlainTextEdit { background:#141417; border:1px solid #333338; } "
                       "QPushButton { padding:6px; border:1px solid #F5C518; }"));
    QPointer<DeepScanDialog> dialog = new DeepScanDialog(&worker, &parent);
    auto* copy = dialog->findChild<QPushButton*>("DeepScanCopy");
    check(copy && !copy->isEnabled(), "Copy enabled before a report.");
    dialog->show();
    const QJsonObject oldSnapshot{
        {"speed_test", QJsonObject{{"old", true}}},
        {"stress_test", QJsonObject{{"old", true}}},
        {"diagnostics",
         QJsonObject{{"current", QJsonObject{{"cpu_temperature_c", 110.0}}},
                     {"temperature", QJsonObject{{"cpu", QJsonObject{{"level", "critical"},
                                                                       {"current_value_c", 110.0}}}}},
                     {"sensors", QJsonObject{{"captured_at", "cached"},
                                              {"data_quality", "stale"}}}}},
        {"tray", QJsonObject{{"icon_enabled", true},
                              {"notifications_enabled", false},
                              {"system_available", true},
                              {"visible", true},
                              {"monitoring_paused", false}}}};
    check(worker.startScan(oldSnapshot, {}, true), "Confirmed fixture did not start.");
    check(!worker.startScan({}, {}, true), "Concurrent scan accepted.");
    releaseFirstRun.store(true);
    awaitWorker(worker);
    check(order == QVector<DeepScanStep>{DeepScanStep::Hardware, DeepScanStep::Smart,
                                         DeepScanStep::LogsBefore, DeepScanStep::Autostart,
                                         DeepScanStep::MemoryBefore, DeepScanStep::Stress,
                                         DeepScanStep::MemoryAfter, DeepScanStep::LogsAfter,
                                         DeepScanStep::SensorsAfter},
          "Deep scan stage order changed.");
    check(monotonic && lastProgress == 100, "Progress regressed or did not complete.");
    check(orion::diagnostics::isReportV3(result) &&
              result.value("scan").toObject().value("state") == "complete",
          "Deep report contract incomplete.");
    check(!result.value("speed_test").isObject() && !result.value("stress_test").toObject().contains("old"),
          "Deep reused old Internet/stress results.");
    check(result.value("runtime").toObject().value("before").toObject().value("ram_used_percent") == 50 &&
              result.value("hardware").toObject().contains("cpu") &&
              result.value("autostart_entries").toArray().size() == 1 &&
              result.value("diagnostics").toObject().value("sensors").toObject().value("captured_at")
                  == "2026-09-14T12:00:00.000Z" &&
              result.value("tray").toObject().value("icon_enabled").toBool(),
          "Memory/hardware/autostart/fresh sensor/tray data lost.");
    check(copy->isEnabled() && !dialog->findChild<QLabel*>("DeepScanVerdict")->isHidden() &&
              dialog->findChild<QPlainTextEdit*>("DeepScanLog")->maximumBlockCount() == 2000,
          "Final copy/verdict/log bound missing.");
    const auto text = DeepScanWorker::reportText(result);
    check(!result.value("verdict_findings").toArray().isEmpty() &&
              dialog->findChild<QLabel*>("DeepScanVerdict")->property("critical").toBool() &&
              dialog->findChild<QLabel*>("DeepScanVerdict")
                  ->text()
                  .contains(result.value("verdict_findings")
                                .toArray()
                                .first()
                                .toObject()
                                .value("title")
                                .toString()),
          "Critical finding missing from the visible verdict banner.");
    check(text.contains("Fixture CPU") && text.contains("Fixture app") &&
              text.contains(QStringLiteral("Датчики после нагрузки")) && text.contains("CPU Package") &&
              text.contains(QStringLiteral("Трей и уведомления")) &&
              text.contains(QStringLiteral("Показывать в трее: да")) &&
              text.contains(QStringLiteral("RAM занято: 50.0% → 50.0%")) &&
              !text.contains(QStringLiteral("\"physical_cores\"")) &&
              text.contains(QStringLiteral("НЕ ПРОВЕРЕНО: Fixture: SMART unavailable")),
          "Readable report text dropped hardware/autostart/sensors/runtime/tray/unknown coverage.");
    const auto args = application.arguments();
    const int shot = args.indexOf(QStringLiteral("--screenshot"));
    if (shot >= 0 && shot + 1 < args.size())
    {
        dialog->setWindowTitle(QStringLiteral("Глубокая диагностика — ТЕСТОВЫЕ ДАННЫЕ"));
        dialog->findChild<QLabel*>("DeepScanStatus")
            ->setText(QStringLiteral("ТЕСТОВЫЕ ДАННЫЕ · реальная нагрузка не запускалась"));
        QCoreApplication::processEvents();
        check(dialog->grab().save(args.at(shot + 1)), "Fixture screenshot failed.");
    }
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    std::atomic_bool enteredStress{false};
    DeepScanWorker cancelledWorker(
        [&](DeepScanStep step, const QJsonObject& seed, const DeepScanWorker::Cancel& cancel,
            const DeepScanWorker::Progress& progress) -> QJsonObject
        {
            if (step == DeepScanStep::Stress)
            {
                enteredStress.store(true);
                while (!cancel())
                    QThread::msleep(2);
                return {{"stopped_early", true}, {"stop_reason", "user"}};
            }
            return fixture(step, seed, cancel, progress);
        });
    QJsonObject cancelledResult;
    QObject::connect(&cancelledWorker, &DeepScanWorker::reportReady, &application,
                     [&](const QJsonObject& report) { cancelledResult = report; });
    auto* cancelledDialog = new DeepScanDialog(&cancelledWorker, &parent);
    cancelledDialog->show();
    cancelledWorker.startScan(oldSnapshot, {}, true);
    QTimer cancelTimer;
    QObject::connect(&cancelTimer, &QTimer::timeout, &application,
                     [&]
                     {
                         if (enteredStress.load())
                         {
                             cancelTimer.stop();
                             cancelledDialog->reject();
                         }
                     });
    cancelTimer.start(5);
    awaitWorker(cancelledWorker);
    check(cancelledResult.value("scan").toObject().value("state") == "cancelled" &&
              cancelledResult.value("runtime").toObject().value("before").toObject().value(
                  "ram_used_percent") == 50 &&
              !cancelledResult.value("scan")
                   .toObject()
                   .value("completed_steps")
                   .toArray()
                   .contains("Журнал ОС после нагрузки"),
          "Cancel continued later stages or claimed completion.");
    check(cancelledResult.value("diagnostics").toObject().value("current").toObject().isEmpty()
          && cancelledResult.value("diagnostics").toObject().value("temperature").toObject().isEmpty()
          && cancelledResult.value("diagnostics").toObject().value("sensors").toObject()
                 .value("data_quality") == "not_collected"
          && DeepScanWorker::reportText(cancelledResult).contains(QStringLiteral("Снимок после нагрузки не собран"))
          && DeepScanWorker::reportText(cancelledResult).contains(QStringLiteral("Собран только снимок до нагрузки")),
          "Cancellation relabelled cached temperature or claimed two memory snapshots.");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    DeepScanWorker safetyWorker(
        [&](DeepScanStep step, const QJsonObject& seed, const DeepScanWorker::Cancel& cancel,
            const DeepScanWorker::Progress& progress) -> QJsonObject
        {
            if (step == DeepScanStep::Stress)
                return {{"stopped_early", true},
                        {"stop_reason", "safety"},
                        {"safety_stop_reason", "Fixture: high temperature"}};
            return fixture(step, seed, cancel, progress);
        });
    QJsonObject safetyResult;
    QObject::connect(&safetyWorker, &DeepScanWorker::reportReady, &application,
                     [&](const QJsonObject& report) { safetyResult = report; });
    safetyWorker.startScan({}, {}, true);
    awaitWorker(safetyWorker);
    check(safetyResult.value("scan").toObject().value("state") == "partial" &&
              DeepScanWorker::reportText(safetyResult).contains(QStringLiteral("частичный отчёт")),
          "Safety stop claimed a complete scan.");

    DeepScanWorker failedWorker([](DeepScanStep, const QJsonObject&, const DeepScanWorker::Cancel&,
                                   const DeepScanWorker::Progress&) -> QJsonObject
                                { throw std::runtime_error("fixture failure"); });
    QJsonObject failed;
    QObject::connect(&failedWorker, &DeepScanWorker::reportReady, &application,
                     [&](const QJsonObject& report) { failed = report; });
    failedWorker.startScan(oldSnapshot, {}, true);
    awaitWorker(failedWorker);
    check(failed.value("scan").toObject().value("state") == "failed" &&
              DeepScanWorker::reportText(failed).contains("fixture failure"),
          "Collector exception escaped or was hidden.");
    check(failed.value("diagnostics").toObject().value("sensors").toObject().value("data_quality")
              == "not_collected" && failed.value("diagnostics").toObject().value("current").toObject().isEmpty(),
          "Early collector failure retained old evidence.");

    DeepScanWorker sensorFailure([&](DeepScanStep step, const QJsonObject& seed,
        const DeepScanWorker::Cancel& cancel, const DeepScanWorker::Progress& progress) -> QJsonObject
    {
        if (step == DeepScanStep::SensorsAfter)
            throw std::runtime_error("fixture sensor failure");
        return fixture(step, seed, cancel, progress);
    });
    QJsonObject sensorFailed;
    QObject::connect(&sensorFailure, &DeepScanWorker::reportReady, &application,
                     [&](const QJsonObject& report) { sensorFailed = report; });
    sensorFailure.startScan(oldSnapshot, {}, true);
    awaitWorker(sensorFailure);
    check(sensorFailed.value("scan").toObject().value("state") == "failed"
          && sensorFailed.value("runtime").toObject().value("before").isObject()
          && sensorFailed.value("diagnostics").toObject().value("sensors").toObject()
                 .value("data_quality") == "not_collected"
          && sensorFailed.value("diagnostics").toObject().value("temperature").toObject().isEmpty(),
          "Failed final sensor collection restored cache or lost completed memory evidence.");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
