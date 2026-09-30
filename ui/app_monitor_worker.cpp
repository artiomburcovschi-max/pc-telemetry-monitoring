#include "app_monitor_worker.h"
#include "app_process_session.h"
#include "orion/core/process_tree_counters.h"
#include "orion/diagnostics/app_monitor_data.h"

#include "orion/diagnostics/diagnostic_engine.h"
#include "orion/diagnostics/diagnostic_schema.h"
#include "orion/diagnostics/runtime_diagnostics.h"
#include "orion/diagnostics/system_diagnostics_collector.h"
#include "orion/platform/process_collector.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QMutexLocker>
#include <QProcess>
#include <QSet>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace orion::app {
namespace {

constexpr double kBytesPerMiB = 1024.0 * 1024.0;
qint64 steadyMilliseconds()
{
    QElapsedTimer clock;
    clock.start();
    return clock.msecsSinceReference();
}

[[nodiscard]] QJsonValue knownNumber(const double value)
{
    return value >= 0.0 && std::isfinite(value)
        ? QJsonValue {value} : QJsonValue {QJsonValue::Null};
}

#ifdef _WIN32
struct WindowHealthContext {
    const QSet<quint32>* pids {nullptr};
    int windows {0};
    int hung {0};
    QJsonArray titles;
};

BOOL CALLBACK inspectWindow(HWND window, LPARAM parameter)
{
    auto* context = reinterpret_cast<WindowHealthContext*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (context == nullptr || context->pids == nullptr
        || !context->pids->contains(static_cast<quint32>(pid))
        || !IsWindowVisible(window)) {
        return TRUE;
    }
    ++context->windows;
    if (IsHungAppWindow(window) != FALSE) {
        ++context->hung;
        wchar_t title[512] {};
        const int length = GetWindowTextW(window, title, 511);
        context->titles.append(length > 0
            ? QString::fromWCharArray(title, length)
            : QStringLiteral("PID %1").arg(pid));
    }
    return TRUE;
}

[[nodiscard]] QJsonObject windowHealth(const QSet<quint32>& pids)
{
    WindowHealthContext context {&pids, 0, 0, {}};
    EnumWindows(inspectWindow, reinterpret_cast<LPARAM>(&context));
    return {
        {QStringLiteral("window_count"), context.windows},
        {QStringLiteral("hung_window_count"), context.hung},
        {QStringLiteral("ui_hung"), context.windows > 0 ? QJsonValue(context.hung > 0) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("hung_window_titles"), context.titles},
    };
}

#else
[[nodiscard]] QJsonObject windowHealth(const QSet<quint32>&)
{
    return {
        {QStringLiteral("window_count"), QJsonValue::Null},
        {QStringLiteral("hung_window_count"), QJsonValue::Null},
        {QStringLiteral("ui_hung"), QJsonValue::Null},
        {QStringLiteral("hung_window_titles"), QJsonArray {}},
    };
}
#endif

[[nodiscard]] QString verdictFor(const QJsonArray& findings, const bool hasSamples)
{
    if (!hasSamples) return QStringLiteral("недостаточно данных");
    bool warning = false;
    for (const auto& value : findings) {
        const QString severity = value.toObject().value(QStringLiteral("severity")).toString();
        if (severity == QStringLiteral("critical")) return QStringLiteral("похоже, проблема здесь");
        warning |= severity == QStringLiteral("warning");
    }
    return warning ? QStringLiteral("есть подозрительные признаки")
                   : QStringLiteral("похоже норма");
}

} // namespace

AppMonitorWorker::AppMonitorWorker(QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<QJsonObject>();
}

void AppMonitorWorker::monitor(const AppMonitorOptions& options)
{
    if (isRunning()) return;
    {
        const QMutexLocker locker(&mutex_);
        options_ = options;
    }
    stopRequested_.store(false, std::memory_order_release);
    {
        const QMutexLocker locker(&clockMutex_);
        observationClock_.reset();
        systemNotBeforeMs_.store(-1, std::memory_order_release);
    }
    start();
}

orion::core::ObservationClock::Snapshot AppMonitorWorker::observationState()
{
    const QMutexLocker locker(&clockMutex_);
    return observationClock_.snapshot(orion::core::ObservationClock::Clock::now());
}

void AppMonitorWorker::updateSystemSample(const QJsonObject& sample)
{
    const QMutexLocker locker(&mutex_);
    systemSample_ = sample;
    systemCondition_.wakeAll();
}

QJsonObject AppMonitorWorker::systemSample(qint64 notBeforeMs, int waitMs)
{
    const auto deadline = steadyMilliseconds() + waitMs;
    const QMutexLocker locker(&mutex_);
    QJsonObject result;
    do {
        notBeforeMs = std::max(notBeforeMs, systemNotBeforeMs_.load(std::memory_order_acquire));
        result = orion::diagnostics::freshAppSystemSample(systemSample_, steadyMilliseconds(), notBeforeMs);
        if ((result.value("system_sample_reason") == "timestamped_cache" && result.value("system_available_percent").isDouble()) || waitMs <= 0
            || observationState().paused) break;
        const auto remaining = deadline - steadyMilliseconds();
        if (remaining <= 0) break;
        systemCondition_.wait(&mutex_, static_cast<unsigned long>(std::min<qint64>(remaining, 100)));
    } while (true);
    return result;
}

void AppMonitorWorker::setPaused(const bool paused)
{
    {
        const QMutexLocker locker(&clockMutex_);
        if (observationClock_.setPaused(paused, orion::core::ObservationClock::Clock::now()))
            systemNotBeforeMs_.store(steadyMilliseconds(), std::memory_order_release);
    }
    waitCondition_.wakeAll();
    systemCondition_.wakeAll();
}

void AppMonitorWorker::stopObservation()
{
    {
        const QMutexLocker locker(&clockMutex_);
        stopRequested_.store(true, std::memory_order_release);
        observationClock_.finish(orion::core::ObservationClock::Clock::now());
    }
    waitCondition_.wakeAll();
    systemCondition_.wakeAll();
}

void AppMonitorWorker::run()
{
    AppMonitorOptions options;
    QJsonObject systemBefore;
    {
        const QMutexLocker locker(&mutex_);
        options = options_;
    }
    const int durationSeconds = std::clamp(options.durationSeconds, 1, 8 * 60 * 60);
    const double intervalSeconds = orion::diagnostics::appMonitorSampleInterval(durationSeconds);
    const QString startedAt = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    emit phaseChanged(QStringLiteral("Снимаю baseline памяти и системного журнала…"));
    const auto logBefore = orion::diagnostics::collectSystemErrorReport(80);

    QJsonObject report {
        {QStringLiteral("schema_version"), 3},
        {QStringLiteral("exe_path"), options.executablePath},
        {QStringLiteral("started_at"), startedAt},
        {QStringLiteral("duration_seconds"), durationSeconds},
        {QStringLiteral("sample_interval"), intervalSeconds},
        {QStringLiteral("close_on_timeout"), options.closeOnTimeout},
        {QStringLiteral("log_before"), logBefore},
    };
    emit phaseChanged(QStringLiteral("Запускаю приложение…"));
    while (observationState().paused && !stopRequested_.load(std::memory_order_acquire)) {
        waitMutex_.lock();
        waitCondition_.wait(&waitMutex_, 100);
        waitMutex_.unlock();
    }
    if (stopRequested_.load(std::memory_order_acquire)) {
        report.insert(QStringLiteral("samples"), QJsonArray {});
        report.insert(QStringLiteral("summary"), QJsonObject {{QStringLiteral("sample_count"), 0}});
        report.insert(QStringLiteral("stopped_manually"), true);
        report.insert(QStringLiteral("finished_at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        report.insert(QStringLiteral("verdict"), QStringLiteral("недостаточно данных"));
        report.insert(QStringLiteral("reasons"), orion::diagnostics::appMonitorReasons(report));
        report.insert(QStringLiteral("report_text"), orion::diagnostics::appMonitorReportToText(report));
        emit reportReady(report);
        return;
    }

    systemBefore = systemSample(); // Consume immediately before launch, after logs/pause.
    AppProcessSession session;
    const bool launched = session.launch(options.executablePath);
    if (!launched) {
        report.insert(QStringLiteral("launch_error"),
            session.errors().join(QStringLiteral("; ")));
        report.insert(QStringLiteral("samples"), QJsonArray {});
        report.insert(QStringLiteral("summary"), QJsonObject {{QStringLiteral("sample_count"), 0}});
        report.insert(QStringLiteral("finished_at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        report.insert(QStringLiteral("verdict"), QStringLiteral("не удалось запустить"));
        report.insert(QStringLiteral("reasons"), orion::diagnostics::appMonitorReasons(report));
        report.insert(QStringLiteral("report_text"), orion::diagnostics::appMonitorReportToText(report));
        emit reportReady(report);
        return;
    }

    const quint32 rootPid = session.rootPid();
    report.insert(QStringLiteral("root_pid"), static_cast<qint64>(rootPid));
    auto collector = orion::platform::makeProcessCollector();
    {
        const QMutexLocker locker(&clockMutex_);
        const auto now = orion::core::ObservationClock::Clock::now();
        observationClock_.start(now);
        if (stopRequested_.load(std::memory_order_acquire)) observationClock_.finish(now);
    }
    QJsonArray samples;
    orion::core::ProcessTreeCounters counters;
    auto counterGeneration = observationState().generation;
    bool timedOut = false;
    bool exitedEarly = false;
    bool stoppedManually = false;
    bool rebaseline = true;
    QSet<quint32> lastAlive {rootPid};
    emit phaseChanged(QStringLiteral("Наблюдаю дерево процессов…"));

    while (true) {
        if (stopRequested_.load(std::memory_order_acquire)) {
            stoppedManually = true;
            break;
        }
        const auto beforeCollection = observationState();
        if (beforeCollection.paused) {
            rebaseline = true;
            waitMutex_.lock();
            waitCondition_.wait(&waitMutex_, 200);
            waitMutex_.unlock();
            continue;
        }
        const auto generation = beforeCollection.generation;
        if (generation != counterGeneration) {
            rebaseline = true;
            collector = orion::platform::makeProcessCollector();
            counterGeneration = generation;
        }
        if (beforeCollection.activeSeconds() >= durationSeconds) {
            timedOut = true;
            break;
        }
        QElapsedTimer processSourceClock;
        processSourceClock.start();
        const auto processObservedAt = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
        const auto snapshot = collector ? collector->sample() : orion::core::ProcessSnapshot {};
        const auto afterCollection = observationState();
        if (stopRequested_.load(std::memory_order_acquire)
            || afterCollection.paused || counterGeneration != afterCollection.generation)
            continue; // Includes a quick pause/resume that happened inside collection.
        const double elapsed = afterCollection.activeSeconds();
        session.observe(snapshot);
        const auto alive = session.alivePids();
        if (alive.isEmpty() && elapsed > 2.0) {
            exitedEarly = true;
            lastAlive.clear();
            break;
        }
        lastAlive = alive;

        double cpu = 0.0;
        double ramBytes = 0.0;
        double privateBytes = 0.0;
        quint64 handles = 0;
        double contextSwitchesPerSecond = 0.0;
        int contextKnown = 0;
        int handleKnown = 0;
        int cpuKnown = 0, ramKnown = 0, privateKnown = 0;
        int threads = 0;
        QVector<QJsonObject> top;
        std::vector<orion::core::ProcessInfo> measuredProcesses;
        for (const auto& process : snapshot.processes) {
            if (!alive.contains(process.pid) || !session.matches(process)) continue;
            measuredProcesses.push_back(process);
            cpuKnown += process.cpuPercent.has_value();
            ramKnown += process.workingSetBytes.has_value();
            privateKnown += process.privateBytes.has_value();
            cpu += process.cpuPercent.value_or(0.0);
            ramBytes += static_cast<double>(process.workingSetBytes.value_or(0));
            privateBytes += static_cast<double>(process.privateBytes.value_or(0));
            threads += static_cast<int>(process.threadCount);
            if (process.handleCount.has_value()) {
                handles += *process.handleCount;
                ++handleKnown;
            }
            if (process.contextSwitchesPerSecond.has_value()) {
                contextSwitchesPerSecond += *process.contextSwitchesPerSecond;
                ++contextKnown;
            }
            top.append(QJsonObject {
                {QStringLiteral("pid"), static_cast<qint64>(process.pid)},
                {QStringLiteral("name"), QString::fromUtf8(process.name)},
                {QStringLiteral("cpu_percent"), knownNumber(process.cpuPercent.value_or(-1.0))},
                {QStringLiteral("rss_mb"), process.workingSetBytes.has_value()
                     ? QJsonValue {static_cast<double>(*process.workingSetBytes) / kBytesPerMiB}
                     : QJsonValue {QJsonValue::Null}},
                {QStringLiteral("private_mb"), process.privateBytes.has_value()
                     ? QJsonValue {static_cast<double>(*process.privateBytes) / kBytesPerMiB} : QJsonValue {QJsonValue::Null}},
                {QStringLiteral("handle_count"), process.handleCount.has_value()
                     ? QJsonValue {static_cast<qint64>(*process.handleCount)} : QJsonValue {QJsonValue::Null}},
            });
        }
        const auto counterSample = counters.update(measuredProcesses, alive.size(), elapsed, rebaseline);
        std::ranges::sort(top, [](const auto& left, const auto& right) {
            const auto memory = [](const QJsonObject& row) {
                return row.value(QStringLiteral("private_mb")).isDouble() ? row.value(QStringLiteral("private_mb")).toDouble()
                    : row.value(QStringLiteral("rss_mb")).toDouble(-1);
            };
            return memory(left) > memory(right);
        });
        QJsonArray topArray;
        for (int index = 0; index < std::min(8, static_cast<int>(top.size())); ++index) topArray.append(top[index]);

        const auto system = systemSample();
        QJsonObject sample = system;
        sample.insert(QStringLiteral("exe_path"), options.executablePath);
        sample.insert("process_observed_monotonic_ms", processSourceClock.msecsSinceReference());
        sample.insert("process_observed_at", processObservedAt);
        sample.insert(QStringLiteral("observed_at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        sample.insert(QStringLiteral("elapsed"), elapsed);
        sample.insert(QStringLiteral("duration"), durationSeconds);
        sample.insert(QStringLiteral("sample_interval"), intervalSeconds);
        const auto completeMetric = [&alive](double value, int count) -> QJsonValue {
            return !alive.isEmpty() && count == alive.size() ? knownNumber(value) : QJsonValue(QJsonValue::Null);
        };
        sample.insert(QStringLiteral("cpu_percent"), completeMetric(cpu, cpuKnown));
        sample.insert(QStringLiteral("ram_mb"), completeMetric(ramBytes / kBytesPerMiB, ramKnown));
        sample.insert(QStringLiteral("private_mb"), completeMetric(privateBytes / kBytesPerMiB, privateKnown));
        sample.insert(QStringLiteral("process_data_quality"), cpuKnown == alive.size() && ramKnown == alive.size()
            && privateKnown == alive.size() && !alive.isEmpty() ? QStringLiteral("valid") : QStringLiteral("partial"));
        sample.insert(QStringLiteral("process_count"), alive.size());
        sample.insert(QStringLiteral("thread_count"), completeMetric(threads, static_cast<int>(top.size())));
        sample.insert(QStringLiteral("handle_count"), handleKnown > 0 && handleKnown == alive.size()
            ? QJsonValue {static_cast<qint64>(handles)} : QJsonValue {QJsonValue::Null});
        const auto optionalNumber = [](const auto& value, double divisor = 1) -> QJsonValue {
            return value.has_value() ? knownNumber(static_cast<double>(*value) / divisor) : QJsonValue(QJsonValue::Null);
        };
        sample.insert("read_mbps", optionalNumber(counterSample.readBytesPerSecond, kBytesPerMiB));
        sample.insert("write_mbps", optionalNumber(counterSample.writeBytesPerSecond, kBytesPerMiB));
        sample.insert("read_total_mb", optionalNumber(counterSample.observedReadBytes, kBytesPerMiB));
        sample.insert("write_total_mb", optionalNumber(counterSample.observedWriteBytes, kBytesPerMiB));
        sample.insert("page_fault_total", optionalNumber(counterSample.observedFaults));
        sample.insert("page_faults_per_sec", optionalNumber(counterSample.faultsPerSecond));
        sample.insert("read_rate_quality", counterSample.complete[0] ? "valid" : "unavailable");
        sample.insert("write_rate_quality", counterSample.complete[1] ? "valid" : "unavailable");
        sample.insert("fault_rate_quality", counterSample.complete[2] ? "valid" : "unavailable");
        sample.insert("io_total_scope", "observed_intervals_lower_bound");
        sample.insert("context_switches_per_sec", !rebaseline && contextKnown > 0 && contextKnown == alive.size()
            ? QJsonValue {contextSwitchesPerSecond} : QJsonValue {QJsonValue::Null});
        const double totalRamMb = system.value(QStringLiteral("system_ram_total_mb")).toDouble();
        sample.insert(QStringLiteral("ram_share_percent"), totalRamMb > 0.0 && ramKnown == alive.size() && !alive.isEmpty()
            ? QJsonValue {(ramBytes / kBytesPerMiB) / totalRamMb * 100.0}
            : QJsonValue {QJsonValue::Null});
        const auto health = windowHealth(alive);
        for (auto it = health.constBegin(); it != health.constEnd(); ++it) sample.insert(it.key(), it.value());
        sample.insert(QStringLiteral("top_processes"), topArray);
        const auto beforePublish = observationState();
        if (stopRequested_.load(std::memory_order_acquire)
            || beforePublish.paused || counterGeneration != beforePublish.generation)
            continue; // Do not publish a sample assembled across a pause transition.
        samples.append(sample);
        emit sampleReady(sample);
        emit progressChanged(std::clamp(static_cast<int>(elapsed / durationSeconds * 100.0), 0, 99));
        rebaseline = false;

        const double nextSample = std::min(static_cast<double>(durationSeconds),
            observationState().activeSeconds() + intervalSeconds);
        while (!stopRequested_.load(std::memory_order_acquire)) {
            const auto state = observationState();
            const double remaining = nextSample - state.activeSeconds();
            if (state.paused || state.generation != counterGeneration || remaining <= 0) break;
            waitMutex_.lock();
            waitCondition_.wait(&waitMutex_, static_cast<unsigned long>(std::clamp(remaining * 1000.0, 1.0, 50.0)));
            waitMutex_.unlock();
        }
    }

    orion::core::ObservationClock::Snapshot observationEnd;
    {
        const QMutexLocker locker(&clockMutex_);
        observationEnd = observationClock_.finish(orion::core::ObservationClock::Clock::now());
    }
    QJsonObject termination {
        {QStringLiteral("requested"), timedOut && options.closeOnTimeout},
        {QStringLiteral("attempted"), false},
        {QStringLiteral("close_messages_sent"), 0},
        {QStringLiteral("graceful"), false},
        {QStringLiteral("all_exited"), lastAlive.isEmpty()},
        {QStringLiteral("terminated_pids"), QJsonArray {}},
        {QStringLiteral("killed_pids"), QJsonArray {}},
        {QStringLiteral("survivor_pids"), QJsonArray {}},
    };
    const auto closureCancelled = [this, generation = observationEnd.generation] {
        const auto state = observationState();
        return stopRequested_.load(std::memory_order_acquire) || state.paused || state.generation != generation;
    };
    if (timedOut && options.closeOnTimeout) {
        // Fresh discovery at the deadline, including children created since the last sample.
        session.observe(collector ? collector->sample() : orion::core::ProcessSnapshot {});
        lastAlive = session.alivePids();
        emit phaseChanged(QStringLiteral("Время вышло — закрываю запущенное дерево процессов…"));
        termination.insert(QStringLiteral("attempted"), !closureCancelled() && !lastAlive.isEmpty());
        termination.insert(QStringLiteral("close_messages_sent"), session.requestClose(closureCancelled));
        QElapsedTimer grace;
        grace.start();
        while (grace.elapsed() < 7000 && !lastAlive.isEmpty() && !closureCancelled()) {
            waitMutex_.lock();
            waitCondition_.wait(&waitMutex_, 100);
            waitMutex_.unlock();
            lastAlive = session.alivePids();
        }
        termination.insert(QStringLiteral("graceful"), lastAlive.isEmpty());
        QJsonArray killed;
        if (!lastAlive.isEmpty() && !closureCancelled()) {
            for (const quint32 pid : lastAlive) {
                if (closureCancelled()) break;
                if (session.forceTerminate(pid)) killed.append(static_cast<qint64>(pid));
            }
            waitMutex_.lock();
            waitCondition_.wait(&waitMutex_, 200);
            waitMutex_.unlock();
            lastAlive = session.alivePids();
        }
        termination.insert(QStringLiteral("cancelled"), closureCancelled());
        termination.insert(QStringLiteral("cancel_reason"), stopRequested_.load() ? QStringLiteral("manual_stop")
            : closureCancelled() ? QStringLiteral("global_pause") : QString {});
        stoppedManually = stopRequested_.load(std::memory_order_acquire);
        termination.insert(QStringLiteral("killed_pids"), killed);
        QJsonArray survivors;
        for (const quint32 pid : lastAlive) survivors.append(static_cast<qint64>(pid));
        termination.insert(QStringLiteral("survivor_pids"), survivors);
        termination.insert(QStringLiteral("all_exited"), lastAlive.isEmpty() && session.errors().isEmpty());
    }
    termination.insert(QStringLiteral("errors"), QJsonArray::fromStringList(session.errors()));
    report.insert(QStringLiteral("process_tracking_quality"), session.errors().isEmpty()
        ? QStringLiteral("valid") : QStringLiteral("partial"));
    report.insert(QStringLiteral("paused_seconds"), observationEnd.pausedSeconds());
    report.insert(QStringLiteral("active_seconds"), observationEnd.activeSeconds());
    report.insert("observation_wall_seconds", observationEnd.activeSeconds() + observationEnd.pausedSeconds());
    report.insert("timing_contract", "pause_transitions_steady_clock_v1");

    const qint64 afterBoundary = steadyMilliseconds();
    emit phaseChanged(QStringLiteral("Сравниваю память и системный журнал…"));
    const auto logAfter = orion::diagnostics::collectSystemErrorReport(80);
    const auto logDiff = orion::diagnostics::diffSystemErrorReports(logBefore, logAfter);
    const double observedLogSeconds = std::max(
        0.0,
        static_cast<double>(QDateTime::fromString(startedAt, Qt::ISODateWithMs)
            .msecsTo(QDateTime::currentDateTime())) / 1000.0);
    const auto logCorrelation = orion::diagnostics::filterSystemErrorReportWindow(
        orion::diagnostics::mergeSystemErrorReports(logBefore, logAfter),
        startedAt,
        5.0,
        observedLogSeconds + 15.0);
    const auto systemAfter = systemSample(afterBoundary, 1500);
    const auto runtimeMemory = orion::diagnostics::appMemoryComparison(systemBefore, systemAfter);
    const auto summary = orion::diagnostics::summarizeAppMonitorSamples(samples);
    const auto exitCode = session.rootExitCode();
    report.insert(QStringLiteral("samples"), samples);
    report.insert(QStringLiteral("summary"), summary);
    report.insert("measurement_contract", "identified_adjacent_intervals_v1");
    report.insert("io_total_scope", "observed_intervals_lower_bound");
    report.insert(QStringLiteral("launch_error"), QJsonValue::Null);
    report.insert(QStringLiteral("exited_early"), exitedEarly);
    report.insert(QStringLiteral("exit_code"), exitCode.has_value()
        ? QJsonValue {*exitCode} : QJsonValue {QJsonValue::Null});
#ifdef _WIN32
    report.insert("exit_details", exitCode.has_value()
        ? QJsonValue(orion::diagnostics::decodeWindowsExitCode(QJsonValue(*exitCode))) : QJsonValue(QJsonValue::Null));
#endif
    report.insert(QStringLiteral("stopped_manually"), stoppedManually);
    report.insert(QStringLiteral("timed_out"), timedOut);
    report.insert(QStringLiteral("termination"), termination);
    report.insert(QStringLiteral("finished_at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    report.insert(QStringLiteral("runtime_memory"), runtimeMemory);
    report.insert(QStringLiteral("log_after"), logAfter);
    report.insert(QStringLiteral("new_system_errors"), logDiff.value(QStringLiteral("errors")));
    report.insert(QStringLiteral("system_error_correlation"), logCorrelation);
    report.insert(QStringLiteral("system_errors_checked_after"), logDiff.value(QStringLiteral("comparison_supported")));
    report.insert(QStringLiteral("system_error_comparison_quality"), logDiff.value(QStringLiteral("data_quality")));
    const QJsonArray findings = orion::diagnostics::analyzeSnapshot(
        QJsonObject {{QStringLiteral("app_monitor"), report}});
    report.insert(QStringLiteral("findings"), findings);
    report.insert(QStringLiteral("verdict_findings"), orion::diagnostics::selectVerdictFindings(findings));
    report.insert(QStringLiteral("action_plan"), orion::diagnostics::buildActionPlan(findings));
    report.insert(QStringLiteral("coverage"), orion::diagnostics::buildCoverageSummary(findings));
    report.insert(QStringLiteral("verdict"), verdictFor(findings, samples.size() >= 2
        && summary.value(QStringLiteral("cpu_avg_percent")).isDouble()
        && summary.value(QStringLiteral("ram_peak_mb")).isDouble()));
    report.insert(QStringLiteral("reasons"), orion::diagnostics::appMonitorReasons(report));
    report.insert(QStringLiteral("report_text"), orion::diagnostics::appMonitorReportToText(report));
    emit progressChanged(100);
    emit reportReady(report);
}

} // namespace orion::app
