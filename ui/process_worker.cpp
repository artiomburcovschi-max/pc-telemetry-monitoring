#include "process_worker.h"

#include "orion/platform/process_collector.h"

#include <QString>

namespace orion::app {
namespace {

constexpr double bytesPerMiB = 1024.0 * 1024.0;

} // namespace

ProcessWorker::ProcessWorker(QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<QVector<ProcessTelemetry>>();
}

void ProcessWorker::setActive(const bool active)
{
    active_.store(active, std::memory_order_release);
    if (active) {
        refreshRequested_.store(true, std::memory_order_release);
    }
    waitCondition_.wakeAll();
}

bool ProcessWorker::isActive() const noexcept
{
    return active_.load(std::memory_order_acquire);
}

void ProcessWorker::refresh()
{
    refreshRequested_.store(true, std::memory_order_release);
    waitCondition_.wakeAll();
}

void ProcessWorker::stop()
{
    requestInterruption();
    waitCondition_.wakeAll();
}

void ProcessWorker::run()
{
    auto collector = orion::platform::makeProcessCollector();
    if (!collector) {
        return;
    }
    while (!isInterruptionRequested()) {
        if (!isActive()) {
            waitMutex_.lock();
            while (!isActive() && !isInterruptionRequested()) {
                waitCondition_.wait(&waitMutex_, 250);
            }
            waitMutex_.unlock();
            continue;
        }
        refreshRequested_.store(false, std::memory_order_release);
        const auto snapshot = collector->sample();
        if (isInterruptionRequested() || !isActive()) {
            continue;
        }
        QVector<ProcessTelemetry> processes;
        processes.reserve(static_cast<qsizetype>(snapshot.processes.size()));
        for (const auto& process : snapshot.processes) {
            const auto ioBytes = process.readBytes.has_value() && process.writeBytes.has_value()
                ? std::optional {*process.readBytes + *process.writeBytes}
                : std::nullopt;
            processes.append({
                process.pid,
                process.parentPid,
                QString::fromUtf8(process.name),
                process.threadCount,
                process.cpuPercent.value_or(-1.0),
                process.workingSetBytes.has_value()
                    ? static_cast<double>(*process.workingSetBytes) / bytesPerMiB : -1.0,
                process.privateBytes.has_value()
                    ? static_cast<double>(*process.privateBytes) / bytesPerMiB : -1.0,
                ioBytes.has_value() ? static_cast<double>(*ioBytes) / bytesPerMiB : -1.0,
                process.creationIdentity,
            });
        }
        emit processesReady(
            processes,
            static_cast<int>(snapshot.logicalProcessorCount),
            QString::fromUtf8(snapshot.source));

        for (int elapsed = 0;
             elapsed < 1400 && !isInterruptionRequested() && isActive()
             && !refreshRequested_.load(std::memory_order_acquire);
             elapsed += 50) {
            waitMutex_.lock();
            waitCondition_.wait(&waitMutex_, 50);
            waitMutex_.unlock();
        }
    }
}

} // namespace orion::app
