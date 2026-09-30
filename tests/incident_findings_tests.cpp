#include "orion/diagnostics/diagnostic_engine.h"
#include "orion/diagnostics/incident_data.h"
#include "orion/diagnostics/runtime_diagnostics.h"
#include "orion/diagnostics/system_diagnostics_collector.h"
#include "orion/diagnostics/report_contract.h"

#include <QJsonDocument>
#include <iostream>

namespace {
using namespace orion::diagnostics;
int failures = 0;
void check(bool ok, const char* why) { if (!ok) { ++failures; std::cerr << why << '\n'; } }
QJsonObject find(const QJsonArray& findings, const QString& id) {
    for (const auto& value : findings) if (value.toObject().value("id") == id) return value.toObject();
    return {};
}
QJsonObject finding(const QJsonObject& incident, const QString& id) {
    return find(analyzeSnapshot({{"incident", incident}}), id);
}
const QString marker = "2026-09-26T12:00:00.000Z";
QString event(double offset, const QString& text) {
    return parseSystemErrorTimestamp(marker).addMSecs(qRound64(offset * 1000)).toString(Qt::ISODateWithMs) + " " + text;
}
QJsonObject logs(const QJsonArray& entries, const QString& quality = "valid", bool limited = false) {
    return {{"errors", entries}, {"data_quality", quality}, {"collection_limited", limited}};
}
// Follow the real IncidentWorker path without accessing OS logs or waiting.
QJsonObject incident(const QJsonObject& before, const QJsonObject& after) {
    const auto diff = diffSystemErrorReports(before, after);
    const auto newWindow = filterSystemErrorReportWindow(diff, marker, 0, 15);
    auto recent = filterSystemErrorReportWindow(mergeSystemErrorReports(before, after), marker, 120, 15);
    recent.insert("comparison_supported", diff.value("comparison_supported"));
    recent.insert("comparison_quality", diff.value("data_quality"));
    return {{"marker_timestamp", marker}, {"post_seconds", 15}, {"status", "complete"},
        {"log_before", before}, {"log_after", after}, {"new_system_errors", newWindow.value("errors")},
        {"recent_system_errors", recent.value("errors")}, {"recent_system_error_correlation", recent},
        {"summary", QJsonObject{{"data_quality", "valid"}}}};
}
QJsonValue evidence(const QJsonObject& result, const QString& label) {
    for (const auto& value : result.value("evidence").toArray()) {
        const auto item = value.toObject();
        if (item.value("label") == label) return item.value("value");
    }
    return {};
}
void logFindings() {
    const auto oldWhea = event(-90, "[System] Provider=WHEA-Logger Machine Check");
    const auto nearDisplay = event(-2, "[System] display driver TDR");
    auto data = incident(logs({oldWhea, nearDisplay}), logs({oldWhea, nearDisplay}));
    auto whea = finding(data, "incident.logs.whea");
    auto display = finding(data, "incident.logs.display");
    check(whea.value("confidence") == "medium" && display.value("confidence") == "high",
        "An unrelated nearby display record elevated old WHEA confidence.");
    check(evidence(whea, QStringLiteral("Ближайшая запись этой категории относительно отметки, с")).toDouble() == -90,
        "Closest offset was not category-specific.");
    check(evidence(whea, QStringLiteral("Связанных уникальных строк категории")).toInt() == 1,
        "Other categories contaminated WHEA count.");
    check(whea.value("detail").toString().contains(QStringLiteral("по времени записи")), "Existing event called new.");

    const auto newWhea = event(3, "[System] WHEA-Logger Machine Check");
    data = incident(logs({oldWhea, nearDisplay}), logs({oldWhea, nearDisplay, newWhea, newWhea}));
    whea = finding(data, "incident.logs.whea");
    check(whea.value("confidence") == "high", "Complete new WHEA did not retain high confidence.");
    check(evidence(whea, QStringLiteral("Связанных уникальных строк категории")).toInt() == 2
        && evidence(whea, QStringLiteral("Строк категории в разнице снимков")).toInt() == 1,
        "Duplicate strings or recent/new overlap inflated the category count.");
    check(whea.value("evidence").toArray().first().toObject().value("source") == "before/after system log diff",
        "New-event evidence source missing.");
    check(whea.value("detail").toString().contains(QStringLiteral("причина сбоя этим не установлены")),
        "Temporal correlation claims an established cause.");

    data = incident(logs({}, "estimated", true), logs({newWhea}));
    whea = finding(data, "incident.logs.whea");
    check(whea.value("confidence") == "medium" && !finding(data, "coverage.incident.system_log").isEmpty(),
        "Partial diff failed to limit confidence and expose coverage.");
    check(whea.value("detail").toString().contains(QStringLiteral("новизна не подтверждена")),
        "Partial snapshots asserted event novelty.");
    data = incident(logs({}, "valid", true), logs({newWhea}));
    check(finding(data, "incident.logs.whea").value("confidence") == "medium", "valid+limited upgraded to high.");
    data = incident(logs({}, "permission_denied"), logs({newWhea}));
    check(finding(data, "incident.logs.whea").value("confidence") == "low", "Unreadable baseline kept high confidence.");
    check(evidence(finding(data, "incident.logs.whea"), QStringLiteral("Строк категории в разнице снимков")).toInt() == 0,
        "Unavailable baseline invented a new event.");
    data.insert("new_system_errors", QJsonArray{newWhea}); // contradictory imported field
    check(evidence(finding(data, "incident.logs.whea"), QStringLiteral("Строк категории в разнице снимков")).toInt() == 0,
        "Explicitly unsupported comparison trusted a contradictory new array.");

    for (double offset : {-120.001, 15.001}) {
        const auto outside = event(offset, "WHEA Machine Check");
        data = incident(logs({outside}), logs({outside}));
        data.insert("recent_system_errors", QJsonArray{outside}); // guard imported arrays too
        data.insert("new_system_errors", QJsonArray{outside});
        check(finding(data, "incident.logs.whea").isEmpty(), "Out-of-window log used in a finding.");
    }
    for (double offset : {-120.0, -15.001, -15.0, 0.0, 15.0}) {
        const auto entry = event(offset, "WHEA Machine Check");
        data = incident(logs({entry}), logs({entry}));
        check(finding(data, "incident.logs.whea").value("confidence") == (offset < -15 ? "medium" : "high"),
            "Inclusive window/near-time boundary wrong.");
    }
    data = incident(logs({oldWhea, "no timestamp WHEA"}), logs({oldWhea, "no timestamp WHEA"}));
    check(!finding(data, "coverage.incident.log_timestamps").isEmpty(), "Mixed parseable/unparseable timestamps hidden.");
    data.insert("recent_system_errors", QJsonArray{"no timestamp WHEA"});
    data.insert("new_system_errors", QJsonArray{"no timestamp WHEA"});
    check(finding(data, "incident.logs.whea").isEmpty(), "Untimestamped modern row inherited another row's offset.");

    const auto hang = event(-1, "Application Hang hung fixture.exe");
    const auto mce = event(-1, "mce: Machine-check");
    data = incident(logs({hang, mce}), logs({hang, mce}));
    check(!finding(data, "incident.logs.app_hang").isEmpty() && !finding(data, "incident.logs.whea").isEmpty(),
        "Incident and general log classification disagree on hung/mce/machine-check.");
    const auto mixed = event(-1, "WHEA hardware error display driver hung");
    data = incident(logs({mixed}), logs({mixed}));
    check(!finding(data, "incident.logs.whea").isEmpty() && finding(data, "incident.logs.display").isEmpty()
        && finding(data, "incident.logs.app_hang").isEmpty(), "First-match category precedence lost.");
    const auto noise = event(-1, "EventID=10029 DistributedCOM BcastDVRUserService AppCaptureShell hung");
    data = incident(logs({noise}), logs({noise}));
    check(finding(data, "incident.logs.app_hang").isEmpty(), "Known background noise became an incident finding.");

    QJsonObject legacy{{"recent_system_errors", QJsonArray{oldWhea}},
        {"recent_system_error_correlation", QJsonObject{{"closest_offset_seconds", -1}, {"data_quality", "valid"}}}};
    check(finding(legacy, "incident.logs.whea").isEmpty(), "Legacy global closest offset accepted as per-event proof.");
    check(!finding(legacy, "coverage.incident.log_timestamps").isEmpty(), "Unavailable legacy time relation was silently discarded.");
    auto correlation = legacy.value("recent_system_error_correlation").toObject();
    correlation.insert("matches", QJsonArray{QJsonObject{{"entry", oldWhea}, {"offset_seconds", -90}}});
    legacy.insert("recent_system_error_correlation", correlation);
    check(finding(legacy, "incident.logs.whea").value("confidence") == "medium", "Legacy exact-row metadata no longer supported.");
    legacy.insert("marker_timestamp", "invalid");
    check(finding(legacy, "incident.logs.whea").isEmpty(), "Invalid explicit marker silently used legacy offsets.");

    const auto late = event(25, "WHEA Machine Check");
    data = incident(logs({}), logs({late}));
    data.insert("post_seconds", 30);
    data.insert("new_system_errors", QJsonArray{late});
    check(!finding(data, "incident.logs.whea").isEmpty(), "Supported custom post window was silently shortened.");
    data = incident(logs({}, "permission_denied"), logs({newWhea}, "unknown"));
    check(finding(data, "incident.logs.whea").value("confidence") == "low", "Unknown quality masked a previous error.");

    data = incident(logs({}, "estimated", true), logs({newWhea}));
    const auto report = buildReport({{"incident", data}});
    const auto text = reportToText(report);
    check(text.contains(QStringLiteral("новизна не подтверждена")) && text.contains(QStringLiteral("журнал около отметки проверен не полностью")),
        "TXT report dropped qualified finding or coverage.");
    const auto roundTrip = QJsonDocument::fromJson(QJsonDocument(report).toJson()).object();
    check(roundTrip == report, "Finding evidence lost during JSON export.");
}
void cpuFindings() {
    const auto checkSeries = [](const QJsonArray& cpuValues, bool expected) {
        QJsonArray rows;
        for (qsizetype index = 0; index < cpuValues.size(); ++index)
            rows.append(QJsonObject{{"monotonic", 90 + index}, {"cpu_usage_percent", cpuValues.at(index)}});
        const auto summary = summarizeIncidentSamples(rows, 100);
        const auto result = finding({{"summary", summary}}, "incident.cpu.saturation");
        check(!result.isEmpty() == expected, "CPU incident fraction does not match known sample rows.");
        if (expected) check(result.value("detail").toString().contains(QStringLiteral("не доля времени")), "Sample fraction described as duration.");
    };
    checkSeries({95, 95, 10, 10, 10}, true); // mean 44: old C++ rule missed this
    checkSeries({100, 89, 89, 89, 89}, false); // mean 91.2: old C++ rule fired
    checkSeries({95, 90, 10, 10, 10}, true); // threshold is inclusive >=90
    checkSeries({94, 94, 94, 94, 94}, false); // peak <95
    checkSeries({95, 90, QJsonValue::Null, QJsonValue::Null, 10}, true);
    checkSeries({QJsonValue::Null, QJsonValue::Null}, false);
    QJsonObject stat{{"count", 10}, {"avg", 44}, {"max", 95}, {"above_90_fraction", 0.4}};
    QJsonObject summary{{"data_quality", "valid"}, {"focus", QJsonObject{{"cpu_usage_percent", stat}}}};
    auto result = finding({{"summary", summary}}, "incident.cpu.saturation");
    check(result.value("confidence") == "high" && evidence(result, QStringLiteral("Доля известных замеров CPU ≥90%")).toDouble() == 0.4,
        "Complete CPU fraction evidence missing.");
    for (const auto& fraction : {QJsonValue(QJsonValue::Null), QJsonValue(0.399), QJsonValue(1.001), QJsonValue(-1)}) {
        auto copy = stat; copy.insert("above_90_fraction", fraction);
        auto changed = summary; changed.insert("focus", QJsonObject{{"cpu_usage_percent", copy}});
        check(finding({{"summary", changed}}, "incident.cpu.saturation").isEmpty(), "Invalid/low CPU fraction used or replaced with mean.");
    }
    summary.insert("data_quality", "estimated");
    check(finding({{"summary", summary}}, "incident.cpu.saturation").value("confidence") == "medium", "Incomplete CPU window kept high confidence.");
    stat.insert("count", 1); summary.insert("data_quality", "valid");
    summary.insert("focus", QJsonObject{{"cpu_usage_percent", stat}});
    check(finding({{"summary", summary}}, "incident.cpu.saturation").value("confidence") == "medium", "Single CPU row kept high confidence.");
    stat.insert("count", 0); summary.insert("focus", QJsonObject{{"cpu_usage_percent", stat}});
    check(finding({{"summary", summary}}, "incident.cpu.saturation").isEmpty(), "Zero CPU count contradicted by summary was trusted.");
    stat.insert("count", 10); summary.insert("focus", QJsonObject{{"cpu_usage_percent", stat}});
    summary.insert("measurement_contract", kIncidentMeasurementContract);
    summary.insert("cpu_thermal_sample_count", 1);
    check(finding({{"summary", summary}}, "incident.cpu.saturation").isEmpty()
        && !finding({{"summary", summary}}, "incident.cpu.thermal_throttling").isEmpty(), "Stage44 thermal precedence regressed.");
}
}
int main() {
    logFindings(); cpuFindings();
    if (failures) return 1;
    std::cout << "Incident event provenance, category confidence and CPU fraction contracts passed.\n";
    return 0;
}
