#include "app_monitor_worker.h"
#include "app_process_session.h"
#include "orion/diagnostics/app_monitor_data.h"

#include "orion/platform/process_collector.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QFileInfo>
#include <QJsonObject>
#include <QTimer>
#include <QTimeZone>
#include <QThread>

#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#include <QProcess>
#endif

namespace {

bool processAlive(const quint32 pid)
{
    auto collector = orion::platform::makeProcessCollector();
    if (!collector) return false;
    const auto snapshot = collector->sample();
    return std::ranges::any_of(snapshot.processes, [pid](const auto& process) {
        return process.pid == pid;
    });
}

void terminateFixture(const quint32 pid)
{
#ifdef _WIN32
    const HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (process != nullptr) {
        TerminateProcess(process, 0);
        CloseHandle(process);
    }
#else
    if (pid > 1) ::kill(static_cast<pid_t>(pid), SIGKILL);
#endif
}

QJsonObject runMonitor(
    orion::app::AppMonitorWorker& worker,
    const orion::app::AppMonitorOptions& options,
    const bool stopAfterFirstSample,
    const int timeoutMilliseconds,
    const std::function<void(const QJsonObject&)>& onSample = {},
    const std::function<void(const QString&)>& onPhase = {})
{
    QEventLoop loop;
    QJsonObject report;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto sampleConnection = QObject::connect(
        &worker, &orion::app::AppMonitorWorker::sampleReady, &loop,
        [&worker, stopAfterFirstSample, onSample](const QJsonObject& sample) {
            if (onSample) onSample(sample);
            if (stopAfterFirstSample) worker.stopObservation();
        });
    const auto phaseConnection = QObject::connect(&worker, &orion::app::AppMonitorWorker::phaseChanged,
        &loop, [onPhase](const QString& phase) { if (onPhase) onPhase(phase); });
    const auto reportConnection = QObject::connect(
        &worker, &orion::app::AppMonitorWorker::reportReady, &loop,
        [&report, &loop](const QJsonObject& value) {
            report = value;
            loop.quit();
        });
    timeout.start(timeoutMilliseconds);
    worker.monitor(options);
    loop.exec();
    QObject::disconnect(sampleConnection);
    QObject::disconnect(phaseConnection);
    QObject::disconnect(reportConnection);
    if (worker.isRunning()) {
        worker.stopObservation();
        worker.wait(12000);
    }
    return report;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QString fixture = QCoreApplication::applicationDirPath()
        + QStringLiteral("/orion_app_monitor_fixture")
#ifdef Q_OS_WIN
        + QStringLiteral(".exe")
#endif
        ;
    if (!QFileInfo::exists(fixture)) {
        std::cerr << "App-monitor fixture executable is missing.\n";
        return EXIT_FAILURE;
    }

    orion::app::AppMonitorWorker worker;
    const auto manualReport = runMonitor(worker, {fixture, 60, false}, true, 15000);
    const quint32 manualPid = static_cast<quint32>(manualReport.value(QStringLiteral("root_pid")).toInteger());
    if (manualReport.isEmpty() || !manualReport.value(QStringLiteral("stopped_manually")).toBool()
        || manualPid == 0 || !processAlive(manualPid)) {
        terminateFixture(manualPid);
        std::cerr << "Manual observation stop did not leave the launched application running.\n";
        return EXIT_FAILURE;
    }
    terminateFixture(manualPid);
    const auto missingMemory = manualReport.value("runtime_memory").toObject();
    const auto firstProcessSample = manualReport.value("samples").toArray().first().toObject();
    if (!firstProcessSample.value("process_observed_monotonic_ms").isDouble()
        || !QDateTime::fromString(firstProcessSample.value("process_observed_at").toString(), Qt::ISODateWithMs).isValid()) {
        std::cerr << "Process producer timestamps missing from the actual worker.\n"; return EXIT_FAILURE;
    }
    if (missingMemory.value("data_quality") != "unknown"
        || manualReport.value("samples").toArray().first().toObject().value("read_total_mb").isDouble()) {
        std::cerr << "Missing endpoints or lifetime I/O presented as measured data.\n"; return EXIT_FAILURE;
    }

    qputenv("ORION_APP_FIXTURE_MODE", "tree");
    const auto timeoutReport = runMonitor(worker, {fixture, 1, true}, false, 20000);
    qunsetenv("ORION_APP_FIXTURE_MODE");
    const auto termination = timeoutReport.value(QStringLiteral("termination")).toObject();
    if (timeoutReport.isEmpty() || !timeoutReport.value(QStringLiteral("timed_out")).toBool()
        || !termination.value(QStringLiteral("requested")).toBool()
        || !termination.value(QStringLiteral("all_exited")).toBool()
        || termination.value(QStringLiteral("killed_pids")).toArray().size() < 2) {
        terminateFixture(static_cast<quint32>(timeoutReport.value(QStringLiteral("root_pid")).toInteger()));
        std::cerr << "Timeout auto-close did not finish its own launched process tree.\n";
        return EXIT_FAILURE;
    }
#ifndef _WIN32
    // Cooperative path: a tree that honours SIGTERM must end gracefully with no forced kills.
    qputenv("ORION_APP_FIXTURE_MODE", "tree-graceful");
    const auto gracefulReport = runMonitor(worker, {fixture, 1, true}, false, 20000);
    qunsetenv("ORION_APP_FIXTURE_MODE");
    const auto gracefulTermination = gracefulReport.value(QStringLiteral("termination")).toObject();
    if (gracefulReport.isEmpty() || !gracefulTermination.value(QStringLiteral("all_exited")).toBool()
        || !gracefulTermination.value(QStringLiteral("graceful")).toBool()
        || gracefulTermination.value(QStringLiteral("close_messages_sent")).toInt() < 2
        || !gracefulTermination.value(QStringLiteral("killed_pids")).toArray().isEmpty()) {
        terminateFixture(static_cast<quint32>(gracefulReport.value(QStringLiteral("root_pid")).toInteger()));
        std::cerr << "A SIGTERM-responsive tree was not closed gracefully without forced kills.\n";
        return EXIT_FAILURE;
    }
#endif

    bool pauseStarted = false;
    const auto feedSystem = [&] {
        orion::core::TelemetryData data;
        const auto now = std::chrono::system_clock::now();
        data.ramAvailablePercent = orion::core::Metric<double>::valid(50, "worker-fixture", now);
        data.ramUsagePercent = orion::core::Metric<double>::valid(50, "worker-fixture", now);
        QElapsedTimer clock; clock.start();
        const auto utc = QDateTime::fromMSecsSinceEpoch(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count(), QTimeZone::UTC);
        worker.updateSystemSample(orion::diagnostics::appSystemEnvelope(data, clock.msecsSinceReference(), utc));
    };
    feedSystem();
    QTimer systemFeed;
    systemFeed.setInterval(100);
    QObject::connect(&systemFeed, &QTimer::timeout, &application, feedSystem);
    systemFeed.start();
    QElapsedTimer pauseWall;
    pauseWall.start();
    const auto pauseReport = runMonitor(worker, {fixture, 2, false}, false, 15000,
        [&](const QJsonObject&) {
            if (pauseStarted) return;
            pauseStarted = true;
            systemFeed.stop();
            worker.setPaused(true);
            QTimer::singleShot(2200, &worker, [&] { worker.setPaused(false); });
        }, [&](const QString& phase) {
            if (phase.startsWith(QStringLiteral("Сравниваю память"))) { feedSystem(); systemFeed.start(); }
        });
    systemFeed.stop();
    const auto pausedSamples = pauseReport.value("samples").toArray();
    if (pausedSamples.size() < 2 || !pausedSamples[1].toObject().value("read_mbps").isNull()) {
        std::cerr << "First resumed I/O interval was not rebaselined.\n"; return EXIT_FAILURE;
    }
    const auto pausePid = static_cast<quint32>(pauseReport.value("root_pid").toInteger());
    const bool pauseSurvived = processAlive(pausePid);
    terminateFixture(pausePid);
    const auto pauseMemory = pauseReport.value("runtime_memory").toObject();
    if (pauseMemory.value("data_quality") != "valid"
        || pauseMemory.value("after").toObject().value("system_source").toString().isEmpty()) {
        for (const auto& name : {"before", "after"}) {
            const auto point = pauseMemory.value(QLatin1String(name)).toObject();
            std::cerr << name << ": quality=" << point.value("system_data_quality").toString().toStdString()
                << " reason=" << point.value("system_sample_reason").toString().toStdString()
                << " ram=" << point.value("system_available_percent").toDouble(-1)
                << " age=" << point.value("system_age_ms").toDouble(-1)
                << " stamp=" << point.value("system_captured_monotonic_ms").toInteger(-1) << '\n';
        }
        std::cerr << "Fresh post-observation endpoint was not consumed.\n"; return EXIT_FAILURE;
    }
    if (!pauseSurvived || !pauseReport.value("timed_out").toBool()
        || pauseReport.value("paused_seconds").toDouble() < 2.0
        || pauseWall.elapsed() < 4000 || pauseReport.value("active_seconds").toDouble() < 2.0) {
        std::cerr << "Pause consumed active observation time or closed the application.\n"; return EXIT_FAILURE;
    }
    bool rapidStarted = false;
    qint64 rapidPauseNanoseconds = 0;
    const auto rapidReport = runMonitor(worker, {fixture, 2, false}, false, 15000,
        [&](const QJsonObject&) {
            if (rapidStarted) return;
            rapidStarted = true;
            // Each transition is far shorter than the worker's old 200 ms pause poll.
            // Exact arithmetic is separately tested without real sleeps.
            for (int i = 0; i < 20; ++i) {
                QElapsedTimer pause; pause.start();
                worker.setPaused(true);
                worker.setPaused(true); // Idempotent, not a new baseline/time origin.
                QThread::msleep(5);
                worker.setPaused(false);
                rapidPauseNanoseconds += pause.nsecsElapsed();
                QThread::msleep(3);
            }
        });
    const auto rapidPid = static_cast<quint32>(rapidReport.value("root_pid").toInteger());
    terminateFixture(rapidPid);
    const double requestedPauseSeconds = rapidPauseNanoseconds / 1e9;
    if (!rapidStarted || !rapidReport.value("timed_out").toBool()
        || rapidReport.value("paused_seconds").toDouble() < 0.08
        || std::abs(rapidReport.value("paused_seconds").toDouble() - requestedPauseSeconds) > 0.04
        || rapidReport.value("active_seconds").toDouble() < 2.0
        || rapidReport.value("timing_contract") != "pause_transitions_steady_clock_v1"
        || std::abs(rapidReport.value("observation_wall_seconds").toDouble()
            - rapidReport.value("active_seconds").toDouble() - rapidReport.value("paused_seconds").toDouble()) > 1e-6) {
        std::cerr << "Rapid pause integration lost transition-based time accounting.\n"; return EXIT_FAILURE;
    }
    bool stopPaused = false;
    const auto stoppedPausedReport = runMonitor(worker, {fixture, 60, false}, false, 15000,
        [&](const QJsonObject&) {
            if (stopPaused) return;
            stopPaused = true;
            worker.setPaused(true);
            QTimer::singleShot(180, &worker, [&] { worker.stopObservation(); });
        });
    const auto stoppedPausedPid = static_cast<quint32>(stoppedPausedReport.value("root_pid").toInteger());
    const bool stoppedPausedSurvived = processAlive(stoppedPausedPid);
    terminateFixture(stoppedPausedPid);
    if (!stoppedPausedSurvived || !stoppedPausedReport.value("stopped_manually").toBool()
        || stoppedPausedReport.value("paused_seconds").toDouble() < 0.1
        || stoppedPausedReport.value("paused_seconds").toDouble() > 0.8
        || stoppedPausedReport.value("active_seconds").toDouble() > 1.0) {
        std::cerr << "Stop while paused included report work or closed the application.\n"; return EXIT_FAILURE;
    }
#ifndef _WIN32
    // The process must survive the close request for cancellation during the grace period
    // to be observable; a SIGTERM-honouring fixture would simply exit.
    qputenv("ORION_APP_FIXTURE_MODE", "ignore-term");
#endif
    for (const int cancelMode : {0, 1, 2}) {
        bool requested = false;
        const auto cancelled = runMonitor(worker, {fixture, 1, true}, false, 15000, {},
            [&](const QString& phase) {
                if (requested || !phase.startsWith(QStringLiteral("Время вышло"))) return;
                requested = true;
                QTimer::singleShot(150, &worker, [&worker, cancelMode] {
                    if (cancelMode == 0) worker.stopObservation();
                    else {
                        worker.setPaused(true);
                        if (cancelMode == 2) worker.setPaused(false);
                    }
                });
            });
        const auto pid = static_cast<quint32>(cancelled.value("root_pid").toInteger());
        const auto closed = cancelled.value("termination").toObject();
        const bool survived = processAlive(pid);
        terminateFixture(pid);
        if (!survived || !closed.value("cancelled").toBool() || !closed.value("killed_pids").toArray().isEmpty()
            || closed.value("all_exited").toBool()
            || closed.value("cancel_reason").toString() != (cancelMode ? "global_pause" : "manual_stop")
            || cancelled.value("paused_seconds").toDouble() != 0) {
            std::cerr << "Stop/Pause during grace did not cancel forced termination.\n"; return EXIT_FAILURE;
        }
    }
    qputenv("ORION_APP_FIXTURE_MODE", "quick-exit");
    const auto early = runMonitor(worker, {fixture, 10, false}, false, 15000);
    qunsetenv("ORION_APP_FIXTURE_MODE");
    // exit_details (hex/NTSTATUS decoding) exists only on Windows; the numeric code is portable.
#ifdef _WIN32
    const bool exitDetailsOk = early.value("exit_details").toObject().value("hex") == "0x00000025";
#else
    const bool exitDetailsOk = true;
#endif
    if (!early.value("exited_early").toBool() || early.value("exit_code").toInt() != 37
        || !exitDetailsOk
        || !early.value("report_text").toString().contains(QStringLiteral("раньше"))) {
        std::cerr << "Root exit status was lost after early process exit.\n"; return EXIT_FAILURE;
    }
    const auto missing = runMonitor(worker, {fixture + ".missing", 1, false}, false, 10000);
    if (missing.value("launch_error").toString().isEmpty()
        || !missing.value("report_text").toString().contains(missing.value("launch_error").toString())) {
        std::cerr << "Launch failure was omitted from the readable report.\n"; return EXIT_FAILURE;
    }
    orion::app::AppProcessSession owned, foreign;
    if (!owned.launch(fixture) || !foreign.launch(fixture)) return EXIT_FAILURE;
    auto collector = orion::platform::makeProcessCollector();
    auto snapshot = collector->sample();
    orion::core::ProcessSnapshot forged;
    for (auto process : snapshot.processes) {
        if (process.pid == foreign.rootPid()) {
            process.parentPid = owned.rootPid();
            ++process.creationIdentity;
            forged.processes.push_back(process);
        }
    }
    owned.observe(forged);
    const bool rejected = !owned.alivePids().contains(foreign.rootPid())
        && !owned.forceTerminate(foreign.rootPid()) && foreign.alivePids().contains(foreign.rootPid());
    owned.forceTerminate(owned.rootPid());
    foreign.forceTerminate(foreign.rootPid());
    if (!rejected || forged.processes.empty()) {
        std::cerr << "Stale process identity was admitted into the observed tree.\n"; return EXIT_FAILURE;
    }
#ifndef _WIN32
    {
        // Long executable names must survive: the kernel truncates comm to 15 characters.
        orion::app::AppProcessSession named;
        if (!named.launch(fixture)) return EXIT_FAILURE;
        bool fullName = false, hasIdentity = false;
        for (const auto& process : collector->sample().processes) {
            if (process.pid != named.rootPid()) continue;
            fullName = process.name == "orion_app_monitor_fixture";
            hasIdentity = process.creationIdentity != 0;
        }
        // An unrelated process must never be signalled, even though its PID is valid.
        QProcess unrelated;
        unrelated.start(QStringLiteral("sleep"), {QStringLiteral("30")});
        const bool started = unrelated.waitForStarted(5000);
        const auto unrelatedPid = static_cast<quint32>(unrelated.processId());
        const bool refused = started && !named.forceTerminate(unrelatedPid) && processAlive(unrelatedPid);
        unrelated.kill();
        unrelated.waitForFinished(3000);
        named.forceTerminate(named.rootPid());
        if (!fullName || !hasIdentity) {
            std::cerr << "Linux process collector lost the full executable name or start-time identity.\n"; return EXIT_FAILURE;
        }
        if (!refused) {
            std::cerr << "A process outside the launched tree was accepted for termination.\n"; return EXIT_FAILURE;
        }
    }
#endif
    std::cout << "App-monitor lifecycle, ownership, pause and cancellation contracts passed.\n";
    return EXIT_SUCCESS;
}
