#include "orion/diagnostics/diagnostic_schema.h"

#include <QJsonArray>
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

void testDefaultStatus()
{
    const auto finding = orion::diagnostics::makeFinding(
        QStringLiteral("info"),
        QStringLiteral("Факт"),
        QStringLiteral("Наблюдаемое условие"));
    check(
        finding.value(QStringLiteral("status")).toString() == QStringLiteral("pass"),
        "informational observation is pass by default, not unknown");
}

void testCausalMerge()
{
    orion::diagnostics::FindingOptions weakOptions;
    weakOptions.groupKey = QStringLiteral("memory");
    weakOptions.evidence = {
        orion::diagnostics::makeEvidence(QStringLiteral("RAM"), 4),
    };
    const auto weak = orion::diagnostics::makeFinding(
        QStringLiteral("warning"),
        QStringLiteral("Low RAM"),
        QStringLiteral("capacity"),
        weakOptions);

    orion::diagnostics::FindingOptions strongOptions;
    strongOptions.groupKey = QStringLiteral("memory");
    strongOptions.evidence = {
        orion::diagnostics::makeEvidence(QStringLiteral("Page-in"), 10),
    };
    const auto strong = orion::diagnostics::makeFinding(
        QStringLiteral("critical"),
        QStringLiteral("Confirmed pressure"),
        QStringLiteral("paging"),
        strongOptions);

    const auto merged = orion::diagnostics::mergeSameGroup({weak, strong});
    check(merged.size() == 1, "same causal group merges to one finding");
    const auto result = merged.first().toObject();
    check(
        result.value(QStringLiteral("title")).toString()
            == QStringLiteral("Confirmed pressure"),
        "strongest causal finding becomes merge base");
    const auto evidence = result.value(QStringLiteral("evidence")).toArray();
    check(evidence.size() == 2, "causal merge retains evidence from both symptoms");
}

void testCoverage()
{
    orion::diagnostics::FindingOptions options;
    options.id = QStringLiteral("coverage.temp");
    options.domain = QStringLiteral("coverage");
    options.status = QStringLiteral("unknown");
    options.groupKey = QStringLiteral("coverage.temperature");
    const auto missing = orion::diagnostics::makeFinding(
        QStringLiteral("info"),
        QStringLiteral("Температура CPU не измерена"),
        QStringLiteral("missing sensor"),
        options);

    const auto coverage = orion::diagnostics::buildCoverageSummary({missing});
    check(
        coverage.value(QStringLiteral("level")).toString() == QStringLiteral("partial"),
        "one coverage gap is partial, not a hardware failure");
    check(
        coverage.value(QStringLiteral("unknown_group_count")).toInt() == 1,
        "coverage counts missing causal groups");
}

void testActionPlan()
{
    orion::diagnostics::FindingOptions storage;
    storage.id = QStringLiteral("storage.disk.critical");
    storage.domain = QStringLiteral("storage");
    storage.urgency = QStringLiteral("immediate");
    storage.impact = QStringLiteral("data_loss");
    storage.actions = {
        QStringLiteral("Немедленно создать резервную копию важных данных"),
    };
    const auto storageFinding = orion::diagnostics::makeFinding(
        QStringLiteral("critical"),
        QStringLiteral("SMART FAILED"),
        QStringLiteral("disk danger"),
        storage);

    orion::diagnostics::FindingOptions performance;
    performance.id = QStringLiteral("performance.background");
    performance.domain = QStringLiteral("performance");
    performance.impact = QStringLiteral("performance");
    performance.actions = {
        QStringLiteral("Закрыть тяжёлый фоновый процесс"),
    };
    const auto performanceFinding = orion::diagnostics::makeFinding(
        QStringLiteral("warning"),
        QStringLiteral("Высокая нагрузка"),
        QStringLiteral("background process"),
        performance);

    const auto plan = orion::diagnostics::buildActionPlan(
        {performanceFinding, storageFinding});
    check(plan.size() == 2, "action plan includes both unique actions");
    check(
        plan.first().toObject().value(QStringLiteral("domain")).toString()
            == QStringLiteral("storage"),
        "data-safety action ranks before performance tuning");
    check(
        plan.first().toObject().value(QStringLiteral("step")).toInt() == 1,
        "action plan assigns stable step numbers");
}

} // namespace

int main()
{
    testDefaultStatus();
    testCausalMerge();
    testCoverage();
    testActionPlan();

    if (failures != 0) {
        std::cerr << failures << " diagnostic schema test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All ORION diagnostic schema tests passed.\n";
    return EXIT_SUCCESS;
}

