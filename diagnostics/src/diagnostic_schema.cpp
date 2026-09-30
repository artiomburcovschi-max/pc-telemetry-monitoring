#include "orion/diagnostics/diagnostic_schema.h"

#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <tuple>

namespace orion::diagnostics {
namespace {

[[nodiscard]] int score(
    const QString& value,
    const std::initializer_list<std::pair<QStringView, int>> values) noexcept
{
    for (const auto& [candidate, result] : values) {
        if (value == candidate) {
            return result;
        }
    }
    return 0;
}

[[nodiscard]] auto rankFinding(const QJsonObject& finding) noexcept
{
    return std::tuple {
        score(finding.value(QStringLiteral("severity")).toString(), {
            {u"critical", 300}, {u"warning", 200}, {u"info", 100}}),
        finding.value(QStringLiteral("priority")).toInt(),
        score(finding.value(QStringLiteral("urgency")).toString(), {
            {u"immediate", 30}, {u"soon", 20}, {u"monitor", 10}, {u"none", 0}}),
        score(finding.value(QStringLiteral("confidence")).toString(), {
            {u"high", 9}, {u"medium", 6}, {u"low", 3}, {u"unknown", 0}}),
    };
}

[[nodiscard]] QString findingGroup(const QJsonObject& finding)
{
    for (const auto& field : {"group_key", "id", "title"}) {
        const auto value = finding.value(QLatin1StringView(field)).toString();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString slug(const QString& text)
{
    QString result;
    bool previousSeparator = false;
    for (const auto character : text.toLower()) {
        if (character.isLetterOrNumber()) {
            result += character;
            previousSeparator = false;
        } else if (!previousSeparator && !result.isEmpty()) {
            result += u'-';
            previousSeparator = true;
        }
    }
    while (result.endsWith(u'-')) {
        result.chop(1);
    }
    return result.isEmpty() ? QStringLiteral("finding") : result;
}

[[nodiscard]] QJsonArray stringArray(const QStringList& values)
{
    QJsonArray result;
    for (const auto& value : values) {
        if (!value.trimmed().isEmpty()) {
            result.append(value);
        }
    }
    return result;
}

[[nodiscard]] QJsonArray deduplicate(const QJsonArray& values)
{
    QJsonArray result;
    for (const auto& value : values) {
        if (!result.contains(value)) {
            result.append(value);
        }
    }
    return result;
}

[[nodiscard]] int actionImpactScore(const QString& impact) noexcept
{
    return score(impact, {
        {u"data_loss", 1000},
        {u"data_integrity", 950},
        {u"stability", 800},
        {u"data_reliability", 780},
        {u"performance_and_stability", 700},
        {u"security", 650},
        {u"responsiveness", 600},
        {u"network_responsiveness", 500},
        {u"performance", 400},
        {u"performance_risk", 300},
        {u"test_validity", 100},
        {u"unknown", 0},
    });
}

[[nodiscard]] QString normalizedActionKey(const QString& text)
{
    static const QRegularExpression nonWord(
        QStringLiteral("[^a-zа-яё0-9]+"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression orderingPrefix(
        QStringLiteral("^(?:(?:в первую очередь|сначала|немедленно|срочно|прежде всего)\\s+)+"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::UseUnicodePropertiesOption);
    QString key = text.toLower();
    key.replace(nonWord, QStringLiteral(" "));
    key = key.trimmed();
    key.remove(orderingPrefix);
    return key.trimmed();
}

} // namespace

QJsonObject makeEvidence(
    const QString& label,
    const QJsonValue value,
    const QString& source,
    const QString& quality,
    const QString& observedAt)
{
    QJsonObject evidence {
        {QStringLiteral("label"), label},
        {QStringLiteral("quality"), quality},
    };
    if (!value.isUndefined() && !value.isNull()) {
        evidence.insert(QStringLiteral("value"), value);
    }
    if (!source.isEmpty()) {
        evidence.insert(QStringLiteral("source"), source);
    }
    if (!observedAt.isEmpty()) {
        evidence.insert(QStringLiteral("observed_at"), observedAt);
    }
    return evidence;
}

QJsonObject makeFinding(
    const QString& severity,
    const QString& title,
    const QString& detail,
    const FindingOptions& options)
{
    const QString id = options.id.isEmpty() ? slug(title) : options.id;
    QString status;
    if (options.status.has_value()) {
        status = *options.status;
    } else if (severity == QStringLiteral("critical")) {
        status = QString::fromLatin1(kStatusFail);
    } else if (severity == QStringLiteral("warning")) {
        status = QString::fromLatin1(kStatusWarning);
    } else {
        status = QString::fromLatin1(kStatusPass);
    }

    QJsonObject finding {
        {QStringLiteral("id"), id},
        {QStringLiteral("domain"), options.domain},
        {QStringLiteral("status"), status},
        {QStringLiteral("severity"), severity},
        {QStringLiteral("confidence"), options.confidence},
        {QStringLiteral("urgency"), options.urgency},
        {QStringLiteral("impact"), options.impact},
        {QStringLiteral("priority"), options.priority},
        {QStringLiteral("group_key"), options.groupKey.isEmpty() ? id : options.groupKey},
        {QStringLiteral("title"), title},
        {QStringLiteral("detail"), detail},
        {QStringLiteral("evidence"), options.evidence},
        {QStringLiteral("counter_evidence"), options.counterEvidence},
        {QStringLiteral("actions"), stringArray(options.actions)},
        {QStringLiteral("verification_steps"), stringArray(options.verificationSteps)},
        {QStringLiteral("source_checks"), stringArray(options.sourceChecks)},
    };
    if (!options.rootCause.isEmpty()) {
        finding.insert(QStringLiteral("root_cause"), options.rootCause);
    }
    if (!options.affectedDevice.isEmpty()) {
        finding.insert(QStringLiteral("affected_device"), options.affectedDevice);
    }
    return finding;
}

QJsonArray mergeSameGroup(const QJsonArray& findings)
{
    QHash<QString, QList<QJsonObject>> grouped;
    QStringList order;
    for (const auto& value : findings) {
        if (!value.isObject()) {
            continue;
        }
        const auto finding = value.toObject();
        const auto group = findingGroup(finding);
        if (!grouped.contains(group)) {
            order.append(group);
        }
        grouped[group].append(finding);
    }

    QJsonArray merged;
    for (const auto& group : order) {
        auto items = grouped.value(group);
        std::sort(items.begin(), items.end(), [](const auto& left, const auto& right) {
            return rankFinding(left) > rankFinding(right);
        });
        QJsonObject base = items.front();
        if (items.size() > 1) {
            QJsonArray related;
            for (qsizetype index = 1; index < items.size(); ++index) {
                const auto id = items[index].value(QStringLiteral("id")).toString();
                if (!id.isEmpty() && id != base.value(QStringLiteral("id")).toString()) {
                    related.append(id);
                }
            }
            if (!related.isEmpty()) {
                base.insert(QStringLiteral("related_findings"), deduplicate(related));
            }
            for (const auto& field : {
                     "evidence", "counter_evidence", "actions",
                     "verification_steps", "source_checks"}) {
                QJsonArray combined;
                for (const auto& item : items) {
                    const auto entries = item.value(QLatin1StringView(field)).toArray();
                    for (const auto& entry : entries) {
                        combined.append(entry);
                    }
                }
                base.insert(QLatin1StringView(field), deduplicate(combined));
            }
        }
        merged.append(base);
    }
    return merged;
}

QJsonArray selectVerdictFindings(const QJsonArray& findings, const int maximumItems)
{
    QList<QJsonObject> ordered;
    for (const auto& value : findings) {
        const auto item = value.toObject();
        const auto severity = item.value(QStringLiteral("severity")).toString();
        if (severity == QStringLiteral("critical") || severity == QStringLiteral("warning")) {
            ordered.append(item);
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return rankFinding(left) > rankFinding(right);
    });

    QJsonArray selected;
    QSet<QString> seenGroups;
    for (const auto& item : ordered) {
        const auto group = findingGroup(item);
        if (seenGroups.contains(group)) {
            continue;
        }
        selected.append(item);
        seenGroups.insert(group);
        if (selected.size() >= std::max(0, maximumItems)) {
            break;
        }
    }
    return selected;
}

QJsonArray buildActionPlan(const QJsonArray& findings, const int maximumSteps)
{
    struct Candidate {
        QJsonObject item;
        std::tuple<int, int, int, int, int, int, int> rank;
    };
    QHash<QString, Candidate> candidates;

    for (const auto& value : findings) {
        const auto finding = value.toObject();
        const auto severity = finding.value(QStringLiteral("severity")).toString(QStringLiteral("info"));
        const auto urgency = finding.value(QStringLiteral("urgency")).toString(QStringLiteral("none"));
        const auto impact = finding.value(QStringLiteral("impact")).toString(QStringLiteral("unknown"));
        const auto domain = finding.value(QStringLiteral("domain")).toString(QStringLiteral("general"));
        const auto id = finding.value(QStringLiteral("id")).toString();
        const bool dataSafety = impact == QStringLiteral("data_loss")
            || impact == QStringLiteral("data_integrity")
            || (domain == QStringLiteral("storage") && severity == QStringLiteral("critical"));
        const auto actions = finding.value(QStringLiteral("actions")).toArray();
        for (qsizetype index = 0; index < actions.size(); ++index) {
            const auto text = actions[index].toString().trimmed();
            if (text.isEmpty()) {
                continue;
            }
            const auto key = normalizedActionKey(text);
            const auto rank = std::tuple {
                dataSafety ? 1 : 0,
                actionImpactScore(impact),
                score(severity, {{u"critical", 300}, {u"warning", 200}, {u"info", 100}}),
                score(urgency, {{u"immediate", 30}, {u"soon", 20}, {u"monitor", 10}}),
                finding.value(QStringLiteral("priority")).toInt(),
                score(finding.value(QStringLiteral("confidence")).toString(),
                      {{u"high", 9}, {u"medium", 6}, {u"low", 3}}),
                -static_cast<int>(index),
            };

            if (!candidates.contains(key)) {
                QJsonArray findingIds;
                if (!id.isEmpty()) {
                    findingIds.append(id);
                }
                candidates.insert(key, Candidate {
                    QJsonObject {
                        {QStringLiteral("action"), text},
                        {QStringLiteral("reason"), finding.value(QStringLiteral("title")).toString(id)},
                        {QStringLiteral("finding_ids"), findingIds},
                        {QStringLiteral("severity"), severity},
                        {QStringLiteral("urgency"), urgency},
                        {QStringLiteral("domain"), domain},
                    },
                    rank,
                });
                continue;
            }

            auto& candidate = candidates[key];
            auto findingIds = candidate.item.value(QStringLiteral("finding_ids")).toArray();
            if (!id.isEmpty() && !findingIds.contains(id)) {
                findingIds.append(id);
                candidate.item.insert(QStringLiteral("finding_ids"), findingIds);
            }
            if (rank > candidate.rank) {
                candidate.item.insert(QStringLiteral("action"), text);
                candidate.item.insert(
                    QStringLiteral("reason"),
                    finding.value(QStringLiteral("title")).toString(id));
                candidate.item.insert(QStringLiteral("severity"), severity);
                candidate.item.insert(QStringLiteral("urgency"), urgency);
                candidate.item.insert(QStringLiteral("domain"), domain);
                candidate.rank = rank;
            }
        }
    }

    auto ordered = candidates.values();
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.rank > right.rank;
    });

    QJsonArray result;
    const int limit = std::min(std::max(0, maximumSteps), static_cast<int>(ordered.size()));
    for (int index = 0; index < limit; ++index) {
        auto item = ordered[index].item;
        item.insert(QStringLiteral("step"), index + 1);
        result.append(item);
    }
    return result;
}

QJsonObject buildCoverageSummary(const QJsonArray& findings)
{
    QHash<QString, QString> unknownGroups;
    QStringList groupOrder;
    int estimatedEvidence = 0;
    int degradedEvidence = 0;

    for (const auto& value : findings) {
        const auto finding = value.toObject();
        const auto status = finding.value(QStringLiteral("status")).toString();
        if (status == QString::fromLatin1(kStatusUnknown)
            || status == QString::fromLatin1(kStatusSkipped)) {
            const auto group = findingGroup(finding);
            if (!unknownGroups.contains(group)) {
                unknownGroups.insert(
                    group,
                    finding.value(QStringLiteral("title")).toString(group));
                groupOrder.append(group);
            }
        }
        const auto evidence = finding.value(QStringLiteral("evidence")).toArray();
        for (const auto& evidenceValue : evidence) {
            const auto quality = evidenceValue.toObject()
                                     .value(QStringLiteral("quality"))
                                     .toString();
            if (quality == QString::fromLatin1(kQualityEstimated)) {
                ++estimatedEvidence;
            } else if (
                quality == QString::fromLatin1(kQualityStale)
                || quality == QString::fromLatin1(kQualityUnsupported)
                || quality == QString::fromLatin1(kQualityPermissionDenied)
                || quality == QString::fromLatin1(kQualityCollectorError)) {
                ++degradedEvidence;
            }
        }
    }

    QStringList missing;
    for (const auto& group : groupOrder) {
        missing.append(unknownGroups.value(group));
    }
    const QString level = missing.size() >= 3 || degradedEvidence >= 2
        ? QStringLiteral("limited")
        : (!missing.isEmpty() || estimatedEvidence > 0 || degradedEvidence > 0)
            ? QStringLiteral("partial")
            : QStringLiteral("complete");

    QString message;
    if (level == QStringLiteral("complete")) {
        message = QStringLiteral("Ключевые доступные источники отработали без явных пробелов.");
    } else {
        const auto shown = missing.isEmpty()
            ? QStringLiteral("часть доказательств имеет оценочное качество")
            : missing.sliced(0, std::min<qsizetype>(3, missing.size())).join(QStringLiteral("; "));
        const auto extra = missing.size() > 3
            ? QStringLiteral("; ещё %1").arg(missing.size() - 3)
            : QString {};
        const auto prefix = level == QStringLiteral("limited")
            ? QStringLiteral("Достоверность ограничена")
            : QStringLiteral("Покрытие частичное");
        message = QStringLiteral("%1: %2%3.").arg(prefix, shown, extra);
    }

    return {
        {QStringLiteral("level"), level},
        {QStringLiteral("unknown_group_count"), missing.size()},
        {QStringLiteral("estimated_evidence_count"), estimatedEvidence},
        {QStringLiteral("degraded_evidence_count"), degradedEvidence},
        {QStringLiteral("missing_checks"), stringArray(missing)},
        {QStringLiteral("message"), message},
    };
}

QJsonObject buildRiskAssessment(
    const QJsonArray& findings,
    const QJsonObject& suppliedCoverage,
    const QJsonObject& stressTest,
    const QJsonObject& runtime)
{
    const auto coverage = suppliedCoverage.isEmpty()
        ? buildCoverageSummary(findings)
        : suppliedCoverage;
    int criticalCount = 0;
    int warningCount = 0;
    for (const auto& value : findings) {
        const auto finding = value.toObject();
        const auto status = finding.value(QStringLiteral("status")).toString();
        const auto severity = finding.value(QStringLiteral("severity")).toString();
        if (status == QString::fromLatin1(kStatusUnknown)
            || status == QString::fromLatin1(kStatusSkipped)
            || finding.value(QStringLiteral("domain")).toString() == QStringLiteral("coverage")) {
            continue;
        }
        criticalCount += severity == QStringLiteral("critical") ? 1 : 0;
        warningCount += severity == QStringLiteral("warning") ? 1 : 0;
    }

    QJsonArray positiveEvidence;
    const auto appendPositive = [&positiveEvidence](const QString& text) {
        if (!text.isEmpty() && !positiveEvidence.contains(text)) {
            positiveEvidence.append(text);
        }
    };

    const auto available = runtime.value(QStringLiteral("memory_available_percent"));
    if (runtime.value(QStringLiteral("memory_pressure")).toString() == QStringLiteral("not_observed")
        && available.isDouble() && available.toDouble() >= 20.0) {
        appendPositive(QStringLiteral(
            "Во время проверки доступно %1% RAM; признаков дефицита памяти не наблюдалось.")
            .arg(available.toDouble(), 0, 'f', 1));
    }
    if (runtime.value(QStringLiteral("paging_interpretation")).toString()
        == QStringLiteral("hard_fault_reads_without_memory_shortage")) {
        appendPositive(QStringLiteral(
            "Дисковые hard page faults наблюдались без низкой RAM/высокого commit; "
            "это не считается подтверждённой подкачкой или проблемой памяти."));
    }

    const auto cpuAverageValue = stressTest.value(QStringLiteral("cpu_load_avg_percent")).isDouble()
        ? stressTest.value(QStringLiteral("cpu_load_avg_percent"))
        : stressTest.value(QStringLiteral("cpu_avg_usage_percent"));
    if (stressTest.value(QStringLiteral("run_cpu")).toBool()
        && cpuAverageValue.isDouble() && cpuAverageValue.toDouble() >= 90.0
        && stressTest.value(QStringLiteral("cpu_worker_failures")).toInt() == 0
        && stressTest.value(QStringLiteral("cpu_forced_terminations")).toInt() == 0) {
        appendPositive(QStringLiteral(
            "CPU выдержал измеренную нагрузку %1% без ошибок вычислительных воркеров.")
            .arg(cpuAverageValue.toDouble(), 0, 'f', 1));
    }
    const auto frequencyDrop = stressTest.value(QStringLiteral("cpu_frequency_drop_percent"));
    const auto workloadDrop = stressTest.value(QStringLiteral("cpu_workload_drop_percent"));
    if (stressTest.value(QStringLiteral("run_cpu")).toBool()
        && stressTest.contains(QStringLiteral("cpu_throttling_suspected"))
        && !stressTest.value(QStringLiteral("cpu_throttling_suspected")).toBool()
        && frequencyDrop.isDouble() && workloadDrop.isDouble()
        && frequencyDrop.toDouble() < 10.0 && workloadDrop.toDouble() < 10.0) {
        appendPositive(QStringLiteral(
            "По частоте и скорости workload косвенных признаков CPU throttling не обнаружено "
            "(падение %1% / %2%).")
            .arg(frequencyDrop.toDouble(), 0, 'f', 1)
            .arg(workloadDrop.toDouble(), 0, 'f', 1));
    }

    if (stressTest.value(QStringLiteral("run_gpu")).toBool()
        && stressTest.value(QStringLiteral("gpu_supported")).toBool()
        && stressTest.value(QStringLiteral("gpu_frames")).toDouble() > 0.0
        && stressTest.value(QStringLiteral("gpu_output_verified")).toBool()
        && stressTest.value(QStringLiteral("gpu_worker_error")).toString().isEmpty()
        && !stressTest.value(QStringLiteral("gpu_forced_termination")).toBool()) {
        const auto renderer = stressTest.value(QStringLiteral("gpu_renderer"))
                                  .toString(QStringLiteral("аппаратный GPU renderer"));
        const auto fps = stressTest.value(QStringLiteral("gpu_fps"));
        const auto speed = fps.isDouble()
            ? QStringLiteral(", средняя скорость %1 операций/с").arg(fps.toDouble(), 0, 'f', 1)
            : QString {};
        appendPositive(QStringLiteral(
            "GPU выполнил проверенный аппаратный workload (%1%2); контрольный результат считан успешно.")
            .arg(renderer, speed));
    }
    const auto gpuUsage = stressTest.value(QStringLiteral("gpu_usage_avg_percent"));
    if (stressTest.value(QStringLiteral("run_gpu")).toBool()
        && stressTest.value(QStringLiteral("gpu_supported")).toBool()
        && stressTest.value(QStringLiteral("gpu_renderer_matches_telemetry")) != QJsonValue {false}
        && gpuUsage.isDouble() && gpuUsage.toDouble() >= 70.0) {
        appendPositive(QStringLiteral("Драйвер подтвердил заметную среднюю загрузку GPU %1%.")
            .arg(gpuUsage.toDouble(), 0, 'f', 1));
    }
    const auto gpuWorkloadDrop = stressTest.value(QStringLiteral("gpu_fps_drop_percent"));
    const auto gpuClockDrop = stressTest.value(QStringLiteral("gpu_clock_drop_percent"));
    if (stressTest.value(QStringLiteral("run_gpu")).toBool()
        && stressTest.value(QStringLiteral("gpu_supported")).toBool()
        && stressTest.contains(QStringLiteral("gpu_throttling_suspected"))
        && !stressTest.value(QStringLiteral("gpu_throttling_suspected")).toBool()
        && gpuWorkloadDrop.isDouble() && gpuWorkloadDrop.toDouble() < 15.0
        && (!gpuClockDrop.isDouble() || gpuClockDrop.toDouble() < 12.0)) {
        const auto clock = gpuClockDrop.isDouble()
            ? QStringLiteral(", частота %1%").arg(gpuClockDrop.toDouble(), 0, 'f', 1)
            : QString {};
        appendPositive(QStringLiteral(
            "К концу GPU-теста не обнаружено выраженного падения workload (скорость %1%%2).")
            .arg(gpuWorkloadDrop.toDouble(), 0, 'f', 1)
            .arg(clock));
    }
    if (stressTest.value(QStringLiteral("system_errors_checked_after")).toBool()
        && stressTest.value(QStringLiteral("new_system_errors")).toArray().isEmpty()) {
        appendPositive(QStringLiteral("После нагрузки новых системных ошибок не появилось."));
    }
    if (stressTest.value(QStringLiteral("run_disk")).toBool()
        && stressTest.value(QStringLiteral("disk_data_verified")).toBool()) {
        const auto writeSpeed = stressTest.value(QStringLiteral("disk_write_mbps"));
        const auto suffix = writeSpeed.isDouble()
            ? QStringLiteral(" (%1 МБ/с с fsync)").arg(writeSpeed.toDouble(), 0, 'f', 1)
            : QString {};
        appendPositive(QStringLiteral(
            "Тестовый файл диска записан и прочитан без нарушения целостности%1.").arg(suffix));
    }

    QString status;
    QString worryLevel;
    QString headline;
    QString summary;
    bool shouldWorry = false;
    if (criticalCount > 0) {
        status = QStringLiteral("danger");
        worryLevel = QStringLiteral("high");
        shouldWorry = true;
        headline = QStringLiteral("Есть подтверждённая проблема, требующая действий");
        for (const auto& value : findings) {
            const auto finding = value.toObject();
            if (finding.value(QStringLiteral("severity")).toString() == QStringLiteral("critical")
                && finding.value(QStringLiteral("status")).toString() != QString::fromLatin1(kStatusUnknown)
                && finding.value(QStringLiteral("status")).toString() != QString::fromLatin1(kStatusSkipped)
                && finding.value(QStringLiteral("domain")).toString() != QStringLiteral("coverage")) {
                summary = finding.value(QStringLiteral("title")).toString(
                    QStringLiteral("Обнаружена критическая проблема"));
                break;
            }
        }
    } else if (warningCount > 0) {
        status = QStringLiteral("attention");
        worryLevel = QStringLiteral("medium");
        shouldWorry = true;
        headline = QStringLiteral("Есть признаки, которые стоит проверить");
        for (const auto& value : findings) {
            const auto finding = value.toObject();
            if (finding.value(QStringLiteral("severity")).toString() == QStringLiteral("warning")
                && finding.value(QStringLiteral("status")).toString() != QString::fromLatin1(kStatusUnknown)
                && finding.value(QStringLiteral("status")).toString() != QString::fromLatin1(kStatusSkipped)
                && finding.value(QStringLiteral("domain")).toString() != QStringLiteral("coverage")) {
                summary = finding.value(QStringLiteral("title")).toString(
                    QStringLiteral("Обнаружено предупреждение"));
                break;
            }
        }
    } else if (coverage.value(QStringLiteral("level")).toString() == QStringLiteral("limited")) {
        status = QStringLiteral("no_issue_observed_limited");
        worryLevel = QStringLiteral("low");
        headline = QStringLiteral("Срочных признаков проблемы не обнаружено");
        summary = QStringLiteral(
            "По доступным данным система прошла проверку без явной неисправности, но часть важных "
            "источников недоступна, поэтому результат не является полной гарантией.");
    } else if (coverage.value(QStringLiteral("level")).toString() == QStringLiteral("partial")) {
        status = QStringLiteral("no_issue_observed_partial");
        worryLevel = QStringLiteral("low");
        headline = QStringLiteral("Явной проблемы по доступным данным не видно");
        summary = QStringLiteral(
            "Наблюдайте только при повторяемых симптомах; часть проверки имела ограниченное покрытие.");
    } else {
        status = QStringLiteral("no_issue_observed");
        worryLevel = QStringLiteral("low");
        headline = QStringLiteral("Причин для беспокойства по этой проверке не найдено");
        summary = QStringLiteral(
            "Критичных или предупреждающих причин по доступным измерениям не обнаружено.");
    }

    return {
        {QStringLiteral("status"), status},
        {QStringLiteral("worry_level"), worryLevel},
        {QStringLiteral("should_worry"), shouldWorry},
        {QStringLiteral("headline"), headline},
        {QStringLiteral("summary"), summary},
        {QStringLiteral("actionable_finding_count"), criticalCount + warningCount},
        {QStringLiteral("critical_count"), criticalCount},
        {QStringLiteral("warning_count"), warningCount},
        {QStringLiteral("positive_evidence"), positiveEvidence},
        {QStringLiteral("limitations"), coverage.value(QStringLiteral("missing_checks")).toArray()},
    };
}

} // namespace orion::diagnostics
