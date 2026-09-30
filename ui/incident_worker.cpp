#include "incident_worker.h"

#include "orion/diagnostics/system_diagnostics_collector.h"
#include "orion/diagnostics/runtime_diagnostics.h"

#include <QDateTime>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <utility>

namespace orion::app {

IncidentWorker::IncidentWorker(QObject* parent, LogCollector collector)
    : QThread(parent), collector_(std::move(collector))
{
    qRegisterMetaType<QJsonObject>();
}

void IncidentWorker::capture(const IncidentOptions& options)
{
    if (isRunning()) return;
    {
        const QMutexLocker locker(&mutex_);
        captureEntered_ = std::chrono::steady_clock::now();
        options_ = options;
    }
    cancelled_.store(false, std::memory_order_release);
    start();
}

void IncidentWorker::cancel()
{
    const QMutexLocker locker(&waitMutex_);
    cancelled_.store(true, std::memory_order_release);
    waitCondition_.wakeAll();
}

void IncidentWorker::run()
{
    using Clock = std::chrono::steady_clock;
    IncidentOptions options;
    Clock::time_point entered;
    {
        const QMutexLocker locker(&mutex_);
        options = options_;
        entered = captureEntered_;
    }
    const auto marker = options.markerSteady.value_or(entered);
    QJsonObject report {
        {"schema_version", 2}, {"incident_id", options.incidentId},
        {"marker_timestamp", options.markerTimestamp}, {"marker_monotonic", options.markerMonotonic},
        {"pre_seconds", options.preSeconds}, {"post_seconds", options.postSeconds},
        {"log_pre_seconds", 120.0}, {"timing_contract", "absolute_marker_steady_v1"},
        {"marker_clock_source", options.markerSteady ? "caller_steady" : "capture_entry_steady"},
    };
    const auto stamp = QDateTime::fromString(options.markerTimestamp, Qt::ISODateWithMs);
    if (!orion::diagnostics::validIncidentWindow(options.markerMonotonic, options.preSeconds, options.postSeconds)
        || !stamp.isValid() || marker > entered) {
        report.insert("status", "invalid");
        report.insert("capture_error", "invalid_marker_or_window");
        emit captureReady(report);
        return;
    }
    const auto deadline = marker + std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(options.postSeconds));
    const auto collect = [this] { return collector_ ? collector_() : orion::diagnostics::collectSystemErrorReport(100); };
    const auto cancelled = [this] { return cancelled_.load(std::memory_order_acquire); };
    QJsonObject before, after;
    if (!cancelled()) {
        emit phaseChanged(QStringLiteral("⚡ Сохраняю baseline системного журнала…"));
        before = collect();
    }
    const auto waitStarted = Clock::now();
    if (!cancelled() && waitStarted < deadline)
        emit phaseChanged(QStringLiteral("⚡ Собираю оставшиеся %1 сек после метки…")
            .arg(std::ceil(std::chrono::duration<double>(deadline - waitStarted).count()), 0, 'f', 0));
    {
        const QMutexLocker locker(&waitMutex_);
        while (!cancelled()) {
            const auto remaining = std::chrono::duration<double, std::milli>(deadline - Clock::now()).count();
            if (remaining <= 0) break;
            waitCondition_.wait(&waitMutex_, static_cast<unsigned long>(std::clamp(std::ceil(remaining), 1.0, 100.0)));
        }
    }
    report.insert("post_wait_seconds", std::chrono::duration<double>(Clock::now() - waitStarted).count());
    if (!cancelled()) {
        report.insert("log_after_delay_seconds", std::max(0.0, std::chrono::duration<double>(Clock::now() - deadline).count()));
        emit phaseChanged(QStringLiteral("⚡ Сравниваю системные события вокруг метки…"));
        if (!cancelled()) after = collect();
    }
    const bool wasCancelled = cancelled();
    const auto difference = orion::diagnostics::diffSystemErrorReports(before, wasCancelled ? QJsonObject{} : after);
    const auto newInWindow = orion::diagnostics::filterSystemErrorReportWindow(
        difference, options.markerTimestamp, 0, options.postSeconds);
    auto recent = orion::diagnostics::filterSystemErrorReportWindow(
        orion::diagnostics::mergeSystemErrorReports(before, wasCancelled ? QJsonObject{} : after),
        options.markerTimestamp, 120.0, options.postSeconds);
    recent.insert("comparison_supported", difference.value("comparison_supported"));
    recent.insert("comparison_quality", difference.value("data_quality"));
    report.insert("status", wasCancelled ? "cancelled" : "complete");
    report.insert("finished_at", QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    report.insert("capture_elapsed_seconds", std::chrono::duration<double>(Clock::now() - marker).count());
    report.insert("log_before", before);
    report.insert("log_after", wasCancelled ? QJsonValue(QJsonValue::Null) : QJsonValue(after));
    report.insert("unfiltered_new_system_errors", difference.value("errors"));
    report.insert("new_system_errors", newInWindow.value("errors"));
    report.insert("new_system_error_correlation", newInWindow);
    report.insert("recent_system_errors", recent.value("errors"));
    report.insert("recent_system_error_correlation", recent);
    emit captureReady(report);
}

} // namespace orion::app
