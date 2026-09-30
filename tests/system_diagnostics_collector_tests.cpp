#include "orion/diagnostics/system_diagnostics_collector.h"
#include "orion/diagnostics/system_log_limits.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <iostream>
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

void testAtaSmartParser()
{
    const QByteArray payload = R"json({
        "model_name":"Test Drive",
        "device":{"name":"/dev/sda","protocol":"ATA"},
        "smart_status":{"passed":true},
        "rotation_rate":7200,
        "temperature":{"current":35},
        "ata_smart_attributes":{"table":[
            {"name":"Reallocated_Sector_Ct","raw":{"value":1}},
            {"name":"Current_Pending_Sector","raw":{"value":0}},
            {"name":"Offline_Uncorrectable","raw":{"value":0}}
        ]}
    })json";
    const auto disk = orion::diagnostics::parseSmartctlDeviceJson(
        payload, QStringLiteral("/dev/sda"));
    check(disk.value(QStringLiteral("level")).toString() == QStringLiteral("warn"),
          "reallocated sector alone is a warning");
    check(disk.value(QStringLiteral("disk_type")).toString().startsWith(QStringLiteral("HDD")),
          "rotation rate maps the device to HDD");
    check(disk.value(QStringLiteral("risk_reasons")).toArray().size() == 1,
          "ATA parser emits one focused risk reason");
}

void testNvmeSmartParser()
{
    const QByteArray payload = R"json({
        "model_name":"Test NVMe",
        "device":{"name":"/dev/nvme0","protocol":"NVMe"},
        "smart_status":{"passed":true},
        "temperature":{"current":45},
        "nvme_smart_health_information_log":{
            "critical_warning":1,
            "available_spare":5,
            "available_spare_threshold":10,
            "percentage_used":91,
            "media_errors":2,
            "num_err_log_entries":7,
            "unsafe_shutdowns":3
        }
    })json";
    const auto disk = orion::diagnostics::parseSmartctlDeviceJson(
        payload, QStringLiteral("/dev/nvme0"));
    check(disk.value(QStringLiteral("is_nvme")).toBool(),
          "NVMe protocol is retained");
    check(disk.value(QStringLiteral("nvme_media_errors")).toInt() == 2,
          "NVMe media errors are parsed");
    check(disk.value(QStringLiteral("level")).toString() == QStringLiteral("critical"),
          "NVMe critical fields produce critical level");
}

void testWindowsEventXmlParser()
{
    const QString xml = QStringLiteral(
        "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'>"
        "<System><Provider Name='WHEA-Logger'/><EventID>18</EventID>"
        "<Level>2</Level><TimeCreated SystemTime='2026-08-28T18:00:03.1234567Z'/>"
        "<Channel>System</Channel></System><EventData><Data>Machine Check</Data>"
        "</EventData></Event>");
    const auto event = orion::diagnostics::parseWindowsEventXml(
        xml, QStringLiteral("Hardware error\r\nmore detail"));
    check(event.value(QStringLiteral("event_id")).toInt() == 18,
          "event XML retains Event ID");
    check(event.value(QStringLiteral("provider")).toString() == QStringLiteral("WHEA-Logger"),
          "event XML retains provider");
    check(event.value(QStringLiteral("line")).toString().contains(QStringLiteral("Hardware error"))
              && !event.value(QStringLiteral("line")).toString().contains(QStringLiteral("more detail")),
          "event presentation keeps one stable message line");
}

void testErrorDiffAndGrouping()
{
    const QJsonObject before {
        {QStringLiteral("errors"), QJsonArray {QStringLiteral("old"), QStringLiteral("same")}},
        {QStringLiteral("data_quality"), QStringLiteral("valid")},
    };
    const QJsonObject after {
        {QStringLiteral("errors"), QJsonArray {
             QStringLiteral("same"), QStringLiteral("new"), QStringLiteral("new")}},
        {QStringLiteral("data_quality"), QStringLiteral("valid")},
    };
    const auto delta = orion::diagnostics::diffSystemErrorReports(before, after);
    check(delta.value(QStringLiteral("comparison_supported")).toBool(),
          "valid system-log snapshots are comparable");
    check(delta.value(QStringLiteral("errors")).toArray().size() == 2,
          "multiset diff preserves repeated new events");

    QJsonArray repeated;
    for (const auto& suffix : {QStringLiteral("ca0ba"), QStringLiteral("DEADB")}) {
        repeated.append(QStringLiteral(
            "2026-08-28T12:01:00Z [System] EventID=10029 Provider=DistributedCOM "
            "BcastDVRUserService_%1 Windows.Media.Capture.AppCaptureManager timeout").arg(suffix));
    }
    const auto groups = orion::diagnostics::summarizeSystemErrorEntries(repeated);
    check(groups.size() == 1 && groups.first().toObject()
              .value(QStringLiteral("count")).toInt() == 2,
          "volatile BcastDVR instance suffixes group together");
}

void testLiveSystemLogContract()
{
    const auto report = orion::diagnostics::collectSystemErrorReport(10);
    check(report.value(QStringLiteral("errors")).isArray(),
          "live system-log report always returns an errors array");
    check(report.value(QStringLiteral("data_quality")).isString(),
          "live system-log report declares data quality");
    check(report.value(QStringLiteral("source")).isString(),
          "live system-log report declares its source");
#ifdef _WIN32
    check(report.value("collection_limits").isObject() && report.value("read_stats").isObject(),
          "native log report omitted limits and read statistics");
    check(report.value("errors").toArray().size() <= 10, "native log exceeded requested count");
    for (const auto& error : report.value("errors").toArray())
        check(error.toString().size() <= orion::diagnostics::kLogLineUnits, "native event escaped line cap");
    if (report.value("collection_limited").toBool())
        check(report.value("data_quality") != "valid" && !report.value("note").toString().isEmpty(),
            "native limited collection hid coverage loss");
#endif
}

void testTimestampCorrelationWindow()
{
    const QString near = QStringLiteral(
        "2026-08-28T17:59:58Z [System] EventID=18 WHEA-Logger Machine Check");
    const QJsonObject report {
        {QStringLiteral("errors"), QJsonArray {
             QStringLiteral("2026-08-28T17:40:00Z [System] EventID=18 old"),
             near,
             QStringLiteral("legacy unstructured line")}},
        {QStringLiteral("data_quality"), QStringLiteral("valid")},
    };
    const auto filtered = orion::diagnostics::filterSystemErrorReportWindow(
        report, QStringLiteral("2026-08-28T18:00:00+00:00"), 120.0, 15.0);
    check(filtered.value(QStringLiteral("errors")).toArray() == QJsonArray {near},
          "timestamp window keeps only the nearby event");
    check(filtered.value(QStringLiteral("timestamped_count")).toInt() == 2
              && filtered.value(QStringLiteral("unparseable_count")).toInt() == 1,
          "timestamp coverage remains explicit");
    check(filtered.value(QStringLiteral("closest_offset_seconds")).toDouble() == -2.0,
          "closest event offset is measured against the marker");

    const auto windows = orion::diagnostics::parseSystemErrorTimestamp(
        QStringLiteral("2026-08-28T18:00:03.1234567Z [System] event"));
    const auto offset = orion::diagnostics::parseSystemErrorTimestamp(
        QStringLiteral("2026-08-28T21:00:03.123+03:00 host kernel event"));
    check(windows.isValid() && offset.isValid() && windows == offset,
          "Windows UTC and offset timestamps correlate at millisecond precision");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    testAtaSmartParser();
    testNvmeSmartParser();
    testWindowsEventXmlParser();
    testErrorDiffAndGrouping();
    testLiveSystemLogContract();
    testTimestampCorrelationWindow();
    if (failures != 0) {
        std::cerr << failures << " system diagnostics collector test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All ORION system diagnostics collector tests passed.\n";
    return EXIT_SUCCESS;
}
