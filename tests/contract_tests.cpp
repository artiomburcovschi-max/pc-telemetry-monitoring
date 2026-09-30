#include "orion/core/metric.h"
#include "orion/diagnostics/diagnostic_engine.h"
#include "orion/diagnostics/report_contract.h"
#include "orion/diagnostics/telemetry_json.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] QJsonObject findingById(const QJsonArray& findings, const QString& id)
{
    for (const auto& value : findings) {
        const auto finding = value.toObject();
        if (finding.value(QStringLiteral("id")).toString() == id) {
            return finding;
        }
    }
    return {};
}

void testReportV3()
{
    const QJsonObject snapshot {
        {QStringLiteral("diagnostics"), QJsonObject {
             {QStringLiteral("temperature"), QJsonObject {
                  {QStringLiteral("cpu"), QJsonObject {{QStringLiteral("level"), QStringLiteral("unknown")}}},
                  {QStringLiteral("gpu"), QJsonObject {{QStringLiteral("level"), QStringLiteral("ok")}}},
              }},
         }},
        {QStringLiteral("stress_test"), QJsonObject {}},
    };
    const auto report = orion::diagnostics::buildReport(snapshot);
    check(orion::diagnostics::isReportV3(report), "report matches schema version 3 envelope");
    check(report.value(QStringLiteral("schema_version")).toInt() == 3,
          "report schema version stays compatible with Python");
    check(report.value(QStringLiteral("findings")).toArray().size() == 1,
          "report contains diagnostic findings");
    check(report.value(QStringLiteral("coverage")).toObject()
              .value(QStringLiteral("level")).toString() == QStringLiteral("partial"),
          "unknown sensor becomes partial coverage");
    check(orion::diagnostics::reportToText(report)
              .contains(QStringLiteral("Диагностические находки")),
          "report v3 has a reusable human-readable representation");
}

void testFullReportSnapshotParity()
{
    const QJsonObject snapshot {
        {QStringLiteral("diagnostics"), QJsonObject {
             {QStringLiteral("smart"), QJsonObject {
                  {QStringLiteral("available"), true},
                  {QStringLiteral("disks"), QJsonArray {QJsonObject {
                       {QStringLiteral("device"), QStringLiteral("Disk 0")},
                       {QStringLiteral("model"), QStringLiteral("Test NVMe")},
                       {QStringLiteral("level"), QStringLiteral("ok")},
                       {QStringLiteral("health"), QStringLiteral("PASSED")},
                       {QStringLiteral("disk_type"), QStringLiteral("NVMe SSD")},
                       {QStringLiteral("temperature_c"), QJsonValue::Null},
                       {QStringLiteral("risk_reasons"), QJsonArray {}},
                  }}},
              }},
             {QStringLiteral("log_errors"), QJsonObject {
                  {QStringLiteral("source"), QStringLiteral("Windows Event Log")},
                  {QStringLiteral("data_quality"), QStringLiteral("valid")},
                  {QStringLiteral("errors"), QJsonArray {}},
                  {QStringLiteral("note"), QStringLiteral("Ошибок не найдено")},
              }},
             {QStringLiteral("temperature"), QJsonObject {
                  {QStringLiteral("cpu"), QJsonObject {{QStringLiteral("level"), QStringLiteral("unknown")}}},
                  {QStringLiteral("gpu"), QJsonObject {{QStringLiteral("level"), QStringLiteral("unknown")}}},
              }},
         }},
        {QStringLiteral("stress_test"), QJsonObject {
             {QStringLiteral("run_cpu"), true},
             {QStringLiteral("run_gpu"), false},
             {QStringLiteral("run_disk"), true},
             {QStringLiteral("cpu_workers"), 16},
             {QStringLiteral("cpu_actual_seconds"), 30.0},
             {QStringLiteral("cpu_load_avg_percent"), 99.3},
             {QStringLiteral("cpu_load_peak_percent"), 100.0},
             {QStringLiteral("cpu_worker_failures"), 0},
             {QStringLiteral("cpu_forced_terminations"), 0},
             {QStringLiteral("cpu_throttling_suspected"), false},
             {QStringLiteral("cpu_frequency_drop_percent"), 1.0},
             {QStringLiteral("cpu_workload_drop_percent"), 2.0},
             {QStringLiteral("system_errors_checked_after"), true},
             {QStringLiteral("new_system_errors"), QJsonArray {}},
             {QStringLiteral("disk_target_mountpoint"), QStringLiteral("C:\\")},
             {QStringLiteral("disk_target_fstype"), QStringLiteral("NTFS")},
             {QStringLiteral("disk_actual_mb"), 200.0},
             {QStringLiteral("disk_write_mbps"), 778.2},
             {QStringLiteral("disk_read_mbps"), 1345.8},
             {QStringLiteral("disk_fsync_applied"), true},
             {QStringLiteral("disk_data_verified"), true},
         }},
        {QStringLiteral("runtime"), QJsonObject {
             {QStringLiteral("data_quality"), QStringLiteral("estimated")},
             {QStringLiteral("memory_available_percent"), 51.6},
             {QStringLiteral("swap_used_percent"), 0.9},
             {QStringLiteral("commit_used_percent"), 44.0},
             {QStringLiteral("pagefile_used_percent"), 0.9},
             {QStringLiteral("paging_activity"), QStringLiteral("unconfirmed")},
             {QStringLiteral("hard_fault_activity"), QStringLiteral("active")},
             {QStringLiteral("memory_pressure"), QStringLiteral("not_observed")},
             {QStringLiteral("paging_sampling_mode"), QStringLiteral("endpoint_rates")},
             {QStringLiteral("paging_interpretation"), QStringLiteral("hard_fault_reads_without_memory_shortage")},
             {QStringLiteral("pages_input_per_sec"), 1533.0},
             {QStringLiteral("page_reads_per_sec"), 12.0},
             {QStringLiteral("disk_busy_percent"), 17.0},
             {QStringLiteral("disk_read_latency_ms"), 1.2},
         }},
        {QStringLiteral("incident"), QJsonObject {
             {QStringLiteral("status"), QStringLiteral("complete")},
             {QStringLiteral("marker_timestamp"), QStringLiteral("2026-08-28T18:00:00+00:00")},
             {QStringLiteral("summary"), QJsonObject {
                  {QStringLiteral("data_quality"), QStringLiteral("valid")},
                  {QStringLiteral("sample_count"), 75},
              }},
             {QStringLiteral("new_system_errors"), QJsonArray {}},
             {QStringLiteral("recent_system_errors"), QJsonArray {}},
         }},
        {QStringLiteral("app_monitor"), QJsonObject {
             {QStringLiteral("exe_path"), QStringLiteral("C:/Games/game.exe")},
             {QStringLiteral("started_at"), QStringLiteral("2026-08-28T18:00:00")},
             {QStringLiteral("finished_at"), QStringLiteral("2026-08-28T18:01:00")},
             {QStringLiteral("verdict"), QStringLiteral("явных проблем не видно")},
             {QStringLiteral("summary"), QJsonObject {
                  {QStringLiteral("sample_count"), 30},
                  {QStringLiteral("cpu_avg_percent"), 45.0},
                  {QStringLiteral("cpu_peak_percent"), 72.0},
              }},
         }},
    };

    const auto report = orion::diagnostics::buildReport(snapshot);
    const auto risk = report.value(QStringLiteral("risk_assessment")).toObject();
    const auto positive = risk.value(QStringLiteral("positive_evidence")).toArray();
    const auto text = orion::diagnostics::reportToText(report);

    check(!risk.value(QStringLiteral("should_worry")).toBool(),
          "coverage gaps alone do not become a fault");
    check(positive.size() >= 5,
          "risk summary retains positive CPU, memory, log, and disk evidence");
    check(text.contains(QStringLiteral("CPU выдержал измеренную нагрузку 99.3%")),
          "human report explains successful measured CPU load");
    check(text.contains(QStringLiteral("не считается подтверждённой подкачкой")),
          "hard faults without memory shortage are not called active paging");
    check(text.contains(QStringLiteral("=== Память во время диагностической сессии ==="))
              && text.contains(QStringLiteral("=== Отметка «Проблема произошла сейчас» ==="))
              && text.contains(QStringLiteral("=== Наблюдение проблемного приложения ===")),
          "full report renders runtime, incident, and app-monitor sections");
    check(text.contains(QStringLiteral("temp=н/д"))
              && !text.contains(QStringLiteral("None°C")),
          "missing SMART temperature remains explicitly unavailable");
}

void testTelemetryJson()
{
    orion::core::TelemetryData telemetry;
    telemetry.osName = "Windows";
    telemetry.cpuUsagePercent = orion::core::Metric<double>::valid(37.5, "test-counter");
    telemetry.gpuName = "Test GPU";
    telemetry.gpuUsagePercent = orion::core::Metric<double>::valid(42.0, "test-gpu-provider");
    telemetry.gpuTemperatureC = orion::core::Metric<double>::valid(61.0, "test-gpu-provider");
    telemetry.gpuMemoryTotalMb = orion::core::Metric<double>::valid(8192.0, "test-gpu-provider");
    telemetry.gpuMemoryUsedMb = orion::core::Metric<double>::valid(2048.0, "test-gpu-provider");
    telemetry.gpuMemoryPercent = orion::core::Metric<double>::valid(25.0, "test-gpu-provider");
    telemetry.commitUsedPercent = orion::core::Metric<double>::valid(52.5, "test-commit");
    telemetry.pagefileUsedPercent = orion::core::Metric<double>::valid(4.5, "test-pagefile");
    telemetry.pagesInputPerSecond = orion::core::Metric<double>::estimated(
        120.0, "test-paging", "hard faults include file-backed reads");
    telemetry.systemContextSwitchesPerSecond =
        orion::core::Metric<double>::valid(1234.0, "test-kernel");
    telemetry.diskBusyPercent = orion::core::Metric<double>::valid(17.0, "test-disk");
    telemetry.netPingMs = orion::core::Metric<double>::valid(12.5, "tcp_connect:53");
    telemetry.cpuTemperatureC = orion::core::Metric<double>::unavailable(
        orion::core::DataQuality::Unsupported,
        "native-provider",
        "no package sensor");
    orion::core::DiskVolume volume;
    volume.name = "System";
    volume.mountPoint = "C:\\";
    volume.fileSystem = "NTFS";
    volume.storageType = "NVMe SSD";
    volume.totalBytes = 1000;
    volume.usedBytes = 400;
    volume.freeBytes = 600;
    volume.usedPercent = 40.0;
    volume.readBytesPerSecond = 128.0;
    volume.busyPercent = 17.0;
    volume.readLatencyMs = 1.25;
    telemetry.disks = orion::core::Metric<std::vector<orion::core::DiskVolume>>::valid(
        {volume}, "test-volume-collector");
    orion::core::TemperatureSensor temperature;
    temperature.component = orion::core::SensorComponent::Cpu;
    temperature.label = "CPU Package";
    temperature.valueC = 58.5;
    temperature.criticalC = 100.0;
    temperature.source = "test-hwmon";
    orion::core::FanSensor fan;
    fan.component = orion::core::SensorComponent::Cpu;
    fan.label = "CPU Fan";
    fan.rpm = 1250.0;
    fan.source = "test-hwmon";
    telemetry.temperatures = orion::core::Metric<std::vector<orion::core::TemperatureSensor>>::valid(
        {temperature}, "test-hwmon");
    telemetry.fans = orion::core::Metric<std::vector<orion::core::FanSensor>>::valid(
        {fan}, "test-hwmon");
    const auto json = orion::diagnostics::telemetryToJson(telemetry);
    const auto metrics = json.value(QStringLiteral("metrics")).toObject();
    check(json.value(QStringLiteral("schema_version")).toInt() == 1,
          "telemetry schema has a stable version");
    check(metrics.value(QStringLiteral("cpu_usage_percent")).toObject()
              .value(QStringLiteral("value")).toDouble() == 37.5,
          "available metric retains its value");
    const auto missing = metrics.value(QStringLiteral("cpu_temp_c")).toObject();
    check(!missing.contains(QStringLiteral("value")),
          "unsupported metric does not serialize an invented zero");
    check(missing.value(QStringLiteral("quality")).toString() == QStringLiteral("unsupported"),
          "unsupported metric keeps explicit quality");
    check(missing.contains(QStringLiteral("reason")),
          "unavailable metric explains why it is missing");
    check(json.value(QStringLiteral("system")).toObject()
              .value(QStringLiteral("gpu_name")).toString() == QStringLiteral("Test GPU"),
          "GPU identity is retained in the telemetry contract");
    check(metrics.value(QStringLiteral("gpu_usage_percent")).toObject()
              .value(QStringLiteral("value")).toDouble() == 42.0,
          "GPU load is retained in the telemetry contract");
    check(metrics.value(QStringLiteral("gpu_vram_used_percent")).toObject()
              .value(QStringLiteral("value")).toDouble() == 25.0,
          "GPU VRAM load is retained in the telemetry contract");
    const auto diskMetric = metrics.value(QStringLiteral("disk_volumes")).toObject();
    const auto disks = diskMetric.value(QStringLiteral("value")).toArray();
    check(disks.size() == 1,
          "disk telemetry serializes every mounted volume");
    check(disks.first().toObject().value(QStringLiteral("storage_type")).toString()
              == QStringLiteral("NVMe SSD"),
          "disk telemetry retains native storage type");
    check(disks.first().toObject().contains(QStringLiteral("read_bytes_per_second")),
          "available disk I/O rate is serialized");
    check(disks.first().toObject().value(QStringLiteral("read_latency_ms")).toDouble() == 1.25,
          "disk latency is serialized");
    check(metrics.value(QStringLiteral("commit_used_percent")).toObject()
              .value(QStringLiteral("value")).toDouble() == 52.5,
          "commit occupancy is serialized separately from pagefile occupancy");
    check(metrics.value(QStringLiteral("pages_input_per_second")).toObject()
              .value(QStringLiteral("quality")).toString() == QStringLiteral("estimated"),
          "Windows hard-fault rate keeps its estimated quality");
    check(metrics.value(QStringLiteral("net_ping_ms")).toObject()
              .value(QStringLiteral("value")).toDouble() == 12.5
              && metrics.value(QStringLiteral("net_ping_ms")).toObject()
                  .value(QStringLiteral("source")).toString() == QStringLiteral("tcp_connect:53"),
          "network latency keeps its value, quality, and producer in telemetry JSON");
    const auto temperatures = metrics.value(QStringLiteral("temperature_sensors"))
                                  .toObject().value(QStringLiteral("value")).toArray();
    check(temperatures.size() == 1
              && temperatures.first().toObject().value(QStringLiteral("component")).toString()
                  == QStringLiteral("cpu")
              && temperatures.first().toObject().value(QStringLiteral("value_c")).toDouble()
                  == 58.5,
          "temperature inventory retains component and measured value");
    const auto fans = metrics.value(QStringLiteral("fan_sensors"))
                          .toObject().value(QStringLiteral("value")).toArray();
    check(fans.size() == 1
              && fans.first().toObject().value(QStringLiteral("rpm")).toDouble() == 1250.0,
          "fan inventory retains native RPM");
}

void testThermalSafetyStopFinding()
{
    const QJsonObject snapshot {
        {QStringLiteral("stress_test"), QJsonObject {
             {QStringLiteral("stop_reason"), QStringLiteral("safety")},
             {QStringLiteral("safety_stop_reason"), QStringLiteral("CPU reached 100 C twice")},
         }},
    };
    const auto finding = findingById(
        orion::diagnostics::analyzeSnapshot(snapshot),
        QStringLiteral("stress.safety_stop"));
    check(!finding.isEmpty(), "thermal safety stop creates a focused finding");
    check(finding.value(QStringLiteral("severity")).toString() == QStringLiteral("critical"),
          "thermal safety stop is critical");
    check(finding.value(QStringLiteral("status")).toString() == QStringLiteral("fail"),
          "thermal safety stop has fail status");
}

void testUnavailableGpuBackendFinding()
{
    const QJsonObject snapshot {
        {QStringLiteral("stress_test"), QJsonObject {
             {QStringLiteral("run_gpu"), true},
             {QStringLiteral("gpu_supported"), false},
             {QStringLiteral("gpu_worker_error"), QStringLiteral("D3D11 compute unavailable")},
         }},
    };
    const auto finding = findingById(
        orion::diagnostics::analyzeSnapshot(snapshot),
        QStringLiteral("coverage.stress_gpu_unavailable"));
    check(!finding.isEmpty(), "unavailable GPU backend creates a coverage finding");
    check(finding.value(QStringLiteral("domain")).toString() == QStringLiteral("coverage"),
          "unavailable GPU backend is classified as coverage");
    check(finding.value(QStringLiteral("status")).toString() == QStringLiteral("unknown"),
          "unavailable GPU backend remains UNKNOWN");
}

void testGpuOutputVerificationFailureFinding()
{
    const QJsonObject snapshot {
        {QStringLiteral("stress_test"), QJsonObject {
             {QStringLiteral("run_gpu"), true},
             {QStringLiteral("gpu_supported"), true},
             {QStringLiteral("gpu_output_verified"), false},
         }},
    };
    const auto finding = findingById(
        orion::diagnostics::analyzeSnapshot(snapshot),
        QStringLiteral("stress.gpu.output_mismatch"));
    check(!finding.isEmpty(), "failed GPU output verification creates a finding");
    check(finding.value(QStringLiteral("severity")).toString() == QStringLiteral("critical"),
          "failed GPU output verification is critical");
    check(finding.value(QStringLiteral("status")).toString() == QStringLiteral("fail"),
          "failed GPU output verification has fail status");
}

void testSystemLogCategories()
{
    const QJsonObject wheaSnapshot {
        {QStringLiteral("diagnostics"), QJsonObject {
             {QStringLiteral("log_errors"), QJsonObject {
                  {QStringLiteral("data_quality"), QStringLiteral("valid")},
                  {QStringLiteral("errors"), QJsonArray {
                       QStringLiteral("2026-08-28T18:00:03Z [System] EventID=18 Provider=WHEA-Logger A fatal hardware error occurred")}},
              }},
         }},
    };
    const auto whea = findingById(
        orion::diagnostics::analyzeSnapshot(wheaSnapshot),
        QStringLiteral("logs.whea"));
    check(!whea.isEmpty(), "one WHEA event creates an actionable finding");
    check(whea.value(QStringLiteral("severity")).toString() == QStringLiteral("critical"),
          "WHEA category is critical");

    const QJsonObject displaySnapshot {
        {QStringLiteral("diagnostics"), QJsonObject {
             {QStringLiteral("log_errors"), QJsonObject {
                  {QStringLiteral("data_quality"), QStringLiteral("valid")},
                  {QStringLiteral("errors"), QJsonArray {
                       QStringLiteral("2026-08-28T18:00:03Z [System] EventID=4101 Provider=Display Display driver nvlddmkm stopped responding"),
                       QStringLiteral("2026-08-28T18:01:03Z [System] EventID=4101 Provider=Display Display driver nvlddmkm stopped responding")}},
              }},
         }},
    };
    const auto display = findingById(
        orion::diagnostics::analyzeSnapshot(displaySnapshot),
        QStringLiteral("logs.display"));
    check(!display.isEmpty(), "two display-driver events cross the warning threshold");
    check(display.value(QStringLiteral("severity")).toString() == QStringLiteral("warning"),
          "display-driver category is a warning");

    // Audited fix: a category's confidence must follow the actual log collection
    // quality, not just its own fixed definition. "valid" and an absent/omitted
    // data_quality field both mean the collector didn't flag a problem, so neither
    // should downgrade confidence; only a stated non-valid quality should.
    const auto wheaWith = [](const QString& quality) {
        QJsonObject logErrors {
            {QStringLiteral("errors"), QJsonArray {
                 QStringLiteral("2026-08-28T18:00:03Z [System] EventID=18 Provider=WHEA-Logger A fatal hardware error occurred")}},
        };
        if (!quality.isEmpty()) logErrors.insert(QStringLiteral("data_quality"), quality);
        const QJsonObject snapshot {
            {QStringLiteral("diagnostics"), QJsonObject {{QStringLiteral("log_errors"), logErrors}}},
        };
        return findingById(orion::diagnostics::analyzeSnapshot(snapshot), QStringLiteral("logs.whea"));
    };
    check(wheaWith(QString {}).value(QStringLiteral("confidence")).toString() == QStringLiteral("high"),
          "Omitted data_quality was treated as unreliable instead of as no reported problem.");
    check(wheaWith(QStringLiteral("valid")).value(QStringLiteral("confidence")).toString() == QStringLiteral("high"),
          "Explicit valid data_quality lost high confidence.");
    check(wheaWith(QStringLiteral("estimated")).value(QStringLiteral("confidence")).toString() == QStringLiteral("medium"),
          "Estimated log collection still claimed high confidence.");
    check(wheaWith(QStringLiteral("collector_error")).value(QStringLiteral("confidence")).toString() == QStringLiteral("low"),
          "Failed log collection still claimed above-low confidence.");
    const auto estimatedFinding = wheaWith(QStringLiteral("estimated"));
    bool hasQualityEvidence = false;
    for (const auto& item : estimatedFinding.value(QStringLiteral("evidence")).toArray())
        if (item.toObject().value(QStringLiteral("label")).toString() == QStringLiteral("Качество источника журнала"))
            hasQualityEvidence = true;
    check(hasQualityEvidence, "Log category finding did not disclose the log source quality it relied on.");
}

void testStressNewSystemErrorsConfidence()
{
    // Audited fix: a "new" event in the stress-session before/after diff must not
    // claim high confidence or unqualified causal wording unless the comparison
    // itself was supported and fully valid. Absence of the quality/support fields
    // (older/simple reports) must still default to fully valid, matching the same
    // absence-is-not-untrusted convention used for general-log confidence.
    const auto stressWith = [](std::optional<bool> checkedAfter, const QString& quality) {
        QJsonObject stress {
            {QStringLiteral("new_system_errors"), QJsonArray {
                 QStringLiteral("2026-08-28T18:00:03Z [System] EventID=18 Provider=WHEA-Logger A fatal hardware error occurred")}},
        };
        if (checkedAfter.has_value()) stress.insert(QStringLiteral("system_errors_checked_after"), *checkedAfter);
        if (!quality.isEmpty()) stress.insert(QStringLiteral("system_errors_comparison_quality"), quality);
        const QJsonObject snapshot {{QStringLiteral("stress_test"), stress}};
        return findingById(orion::diagnostics::analyzeSnapshot(snapshot), QStringLiteral("stress.new_system_errors"));
    };
    check(stressWith(std::nullopt, QString {}).value(QStringLiteral("confidence")).toString() == QStringLiteral("high"),
          "Absent checked_after/quality fields were treated as unsupported instead of the legacy-valid default.");
    check(stressWith(true, QStringLiteral("valid")).value(QStringLiteral("confidence")).toString() == QStringLiteral("high"),
          "Explicitly supported and valid stress-session comparison lost high confidence.");
    check(stressWith(true, QStringLiteral("estimated")).value(QStringLiteral("confidence")).toString() == QStringLiteral("medium"),
          "Estimated stress-session comparison still claimed high confidence.");
    check(stressWith(false, QString {}).value(QStringLiteral("confidence")).toString() == QStringLiteral("medium"),
          "Unsupported stress-session comparison still claimed high confidence.");
    const auto degraded = stressWith(false, QString {});
    check(degraded.value(QStringLiteral("detail")).toString().contains(QStringLiteral("не доказывает")),
          "Degraded stress-session comparison lost its non-causal wording caveat.");
}

void testStorageCausalConfidence()
{
    // Audited fix: causal.storage.degradation claims three sources (SMART, slow
    // write, log errors) agree on one disk. That claim is only as strong as the
    // weakest source, so a degraded SMART read or log collection must not still
    // be presented as three independently confirmed signals.
    const auto storageWith = [](const QString& smartQuality, const QString& logQuality) {
        QJsonObject disk {
            {QStringLiteral("device"), QStringLiteral("/dev/sda")},
            {QStringLiteral("disk_type"), QStringLiteral("SSD")},
            {QStringLiteral("risk_reasons"), QJsonArray {
                 QJsonObject {{QStringLiteral("severity"), QStringLiteral("critical")},
                     {QStringLiteral("text"), QStringLiteral("pending sectors")}}}},
        };
        if (!smartQuality.isEmpty()) disk.insert(QStringLiteral("data_quality"), smartQuality);
        QJsonObject logErrors {
            {QStringLiteral("errors"), QJsonArray {QStringLiteral("disk I/O error on /dev/sda")}},
        };
        if (!logQuality.isEmpty()) logErrors.insert(QStringLiteral("data_quality"), logQuality);
        const QJsonObject snapshot {
            {QStringLiteral("diagnostics"), QJsonObject {
                 {QStringLiteral("smart"), QJsonObject {{QStringLiteral("disks"), QJsonArray {disk}}}},
                 {QStringLiteral("log_errors"), logErrors},
             }},
            {QStringLiteral("stress_test"), QJsonObject {
                 {QStringLiteral("run_disk"), true},
                 {QStringLiteral("disk_target_device"), QStringLiteral("/dev/sda")},
                 {QStringLiteral("disk_target_type"), QStringLiteral("SSD")},
                 {QStringLiteral("disk_fsync_applied"), true},
                 {QStringLiteral("disk_target_is_memory_fs"), false},
                 {QStringLiteral("disk_write_mbps"), 25},
             }},
        };
        return findingById(orion::diagnostics::analyzeSnapshot(snapshot), QStringLiteral("causal.storage.degradation"));
    };
    check(storageWith(QString {}, QString {}).value(QStringLiteral("confidence")).toString() == QStringLiteral("high"),
          "Absent SMART/log quality fields were treated as unreliable instead of the legacy-valid default.");
    check(storageWith(QStringLiteral("valid"), QStringLiteral("valid")).value(QStringLiteral("confidence")).toString() == QStringLiteral("high"),
          "Explicitly valid SMART and log quality lost high confidence.");
    check(storageWith(QStringLiteral("estimated"), QString {}).value(QStringLiteral("confidence")).toString() == QStringLiteral("medium"),
          "Estimated SMART read still claimed high confidence for the causal storage chain.");
    check(storageWith(QString {}, QStringLiteral("collector_error")).value(QStringLiteral("confidence")).toString() == QStringLiteral("medium"),
          "Failed log collection still claimed high confidence for the causal storage chain.");
    bool hasSmartQualityEvidence = false, hasLogQualityEvidence = false;
    for (const auto& item : storageWith(QStringLiteral("estimated"), QString {}).value(QStringLiteral("evidence")).toArray()) {
        const auto label = item.toObject().value(QStringLiteral("label")).toString();
        if (label == QStringLiteral("Качество SMART-снимка")) hasSmartQualityEvidence = true;
        if (label == QStringLiteral("Качество журнала")) hasLogQualityEvidence = true;
    }
    check(hasSmartQualityEvidence && hasLogQualityEvidence,
          "Causal storage finding did not disclose the SMART/log quality it relied on.");
    check(storageWith(QStringLiteral("estimated"), QString {}).value(QStringLiteral("detail")).toString()
              .contains(QStringLiteral("не три независимо")),
          "Degraded causal storage chain lost its not-independently-confirmed caveat.");
}

void testKnownBackgroundEventNoiseIsSuppressed()
{
    QJsonArray errors;
    for (int index = 0; index < 10; ++index) {
        errors.append(QStringLiteral(
            "2026-08-28T18:%1:03Z [System] EventID=10029 Provider=DistributedCOM "
            "BcastDVRUserService_ca0ba Windows.Media.Capture.AppCaptureManager timeout")
                          .arg(index, 2, 10, QLatin1Char('0')));
    }
    const QJsonObject snapshot {
        {QStringLiteral("diagnostics"), QJsonObject {
             {QStringLiteral("log_errors"), QJsonObject {
                  {QStringLiteral("data_quality"), QStringLiteral("valid")},
                  {QStringLiteral("errors"), errors},
              }},
         }},
    };
    const auto uncategorized = findingById(
        orion::diagnostics::analyzeSnapshot(snapshot),
        QStringLiteral("logs.uncategorized"));
    check(uncategorized.isEmpty(),
          "known BcastDVR DCOM background events do not create a generic warning");
}

void testCriticalSmartFinding()
{
    const QJsonObject snapshot {
        {QStringLiteral("diagnostics"), QJsonObject {
             {QStringLiteral("smart"), QJsonObject {
                  {QStringLiteral("available"), true},
                  {QStringLiteral("disks"), QJsonArray {QJsonObject {
                       {QStringLiteral("device"), QStringLiteral("Disk 0")},
                       {QStringLiteral("health"), QStringLiteral("ОТКАЗ")},
                       {QStringLiteral("data_quality"), QStringLiteral("valid")},
                       {QStringLiteral("level"), QStringLiteral("critical")},
                       {QStringLiteral("risk_reasons"), QJsonArray {QJsonObject {
                            {QStringLiteral("severity"), QStringLiteral("critical")},
                            {QStringLiteral("text"), QStringLiteral("SMART overall-health сообщает отказ")},
                       }}},
                  }}},
              }},
         }},
    };
    const auto smart = findingById(
        orion::diagnostics::analyzeSnapshot(snapshot),
        QStringLiteral("storage.Disk 0.critical"));
    check(!smart.isEmpty(), "critical SMART evidence creates a device-specific finding");
    check(smart.value(QStringLiteral("status")).toString() == QStringLiteral("fail"),
          "critical SMART evidence has fail status");
}

void testTimestampCorrelatedIncidentFindings()
{
    const QString whea = QStringLiteral(
        "2026-08-28T17:59:58Z [System] EventID=18 Provider=WHEA-Logger Machine Check");
    const QJsonObject snapshot {
        {QStringLiteral("incident"), QJsonObject {
             {QStringLiteral("status"), QStringLiteral("complete")},
             {QStringLiteral("summary"), QJsonObject {
                  {QStringLiteral("data_quality"), QStringLiteral("valid")},
                  {QStringLiteral("missing_metrics"), QJsonArray {}},
              }},
             {QStringLiteral("new_system_errors"), QJsonArray {}},
             {QStringLiteral("recent_system_errors"), QJsonArray {whea}},
             {QStringLiteral("recent_system_error_correlation"), QJsonObject {
                  {QStringLiteral("data_quality"), QStringLiteral("valid")},
                  {QStringLiteral("errors"), QJsonArray {whea}},
                  {QStringLiteral("matches"), QJsonArray {QJsonObject {{"entry", whea}, {"offset_seconds", -2.0}}}},
                  {QStringLiteral("timestamped_count"), 1},
                  {QStringLiteral("unparseable_count"), 0},
                  {QStringLiteral("closest_offset_seconds"), -2.0},
              }},
         }},
    };
    const auto findings = orion::diagnostics::analyzeSnapshot(snapshot);
    const auto finding = findingById(findings, QStringLiteral("incident.logs.whea"));
    check(!finding.isEmpty()
              && finding.value(QStringLiteral("confidence")).toString() == QStringLiteral("high"),
          "timestamp-correlated WHEA event creates a high-confidence incident finding");
    const auto evidence = finding.value(QStringLiteral("evidence")).toArray();
    check(!evidence.isEmpty()
              && evidence.first().toObject().value(QStringLiteral("source")).toString()
                  == QStringLiteral("timestamped system-log window"),
          "incident finding identifies timestamp correlation as its evidence source");

    const QJsonObject unknownSnapshot {
        {QStringLiteral("incident"), QJsonObject {
             {QStringLiteral("status"), QStringLiteral("complete")},
             {QStringLiteral("summary"), QJsonObject {
                  {QStringLiteral("data_quality"), QStringLiteral("valid")},
                  {QStringLiteral("missing_metrics"), QJsonArray {}},
              }},
             {QStringLiteral("recent_system_error_correlation"), QJsonObject {
                  {QStringLiteral("errors"), QJsonArray {}},
                  {QStringLiteral("timestamped_count"), 0},
                  {QStringLiteral("unparseable_count"), 1},
              }},
         }},
    };
    check(!findingById(orion::diagnostics::analyzeSnapshot(unknownSnapshot),
            QStringLiteral("coverage.incident.log_timestamps")).isEmpty(),
          "unparseable event time becomes explicit incident coverage UNKNOWN");
}

} // namespace

int main()
{
    testReportV3();
    testFullReportSnapshotParity();
    testTelemetryJson();
    testThermalSafetyStopFinding();
    testUnavailableGpuBackendFinding();
    testGpuOutputVerificationFailureFinding();
    testSystemLogCategories();
    testStressNewSystemErrorsConfidence();
    testStorageCausalConfidence();
    testKnownBackgroundEventNoiseIsSuppressed();
    testCriticalSmartFinding();
    testTimestampCorrelatedIncidentFindings();
    if (failures != 0) {
        std::cerr << failures << " contract test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All ORION report and telemetry contract tests passed.\n";
    return EXIT_SUCCESS;
}
