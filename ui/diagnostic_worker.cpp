#include "diagnostic_worker.h"

#include "orion/diagnostics/report_contract.h"
#include "orion/diagnostics/system_diagnostics_collector.h"
#include "orion/platform/autostart_collector.h"

#include <QJsonArray>
#include <QMutexLocker>

namespace orion::app {

DiagnosticWorker::DiagnosticWorker(QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<QJsonObject>();
}

void DiagnosticWorker::scan(const QJsonObject& snapshot)
{
    if (isRunning()) {
        return;
    }
    {
        const QMutexLocker locker(&mutex_);
        snapshot_ = snapshot;
    }
    start();
}

void DiagnosticWorker::stop()
{
    requestInterruption();
}

void DiagnosticWorker::run()
{
    emit scanStarted();
    QJsonObject snapshot;
    {
        const QMutexLocker locker(&mutex_);
        snapshot = snapshot_;
    }
    if (!snapshot.value(QStringLiteral("autostart_collected")).toBool()
        && snapshot.value(QStringLiteral("autostart_entries")).toArray().isEmpty()) {
        QJsonArray entries;
        if (auto collector = orion::platform::makeAutostartCollector()) {
            const auto result = collector->scan();
            for (const auto& entry : result.entries) {
                QJsonObject object {
                    {QStringLiteral("name"), QString::fromUtf8(entry.name)},
                    {QStringLiteral("command"), QString::fromUtf8(entry.command)},
                    {QStringLiteral("source"), QString::fromUtf8(entry.source)},
                    {QStringLiteral("category"), QString::fromLatin1(
                         orion::core::toString(entry.category))},
                };
                object.insert(QStringLiteral("enabled"), entry.enabled.has_value()
                    ? QJsonValue {*entry.enabled} : QJsonValue {QJsonValue::Null});
                entries.append(object);
            }
        }
        snapshot.insert(QStringLiteral("autostart_entries"), entries);
    }
    auto diagnostics = snapshot.value(QStringLiteral("diagnostics")).toObject();
    if (!diagnostics.contains(QStringLiteral("smart"))) {
        diagnostics.insert(
            QStringLiteral("smart"),
            orion::diagnostics::collectSmartReport());
    }
    if (isInterruptionRequested()) {
        return;
    }
    if (!diagnostics.contains(QStringLiteral("log_errors"))) {
        diagnostics.insert(
            QStringLiteral("log_errors"),
            orion::diagnostics::collectSystemErrorReport(50));
    }
    snapshot.insert(QStringLiteral("diagnostics"), diagnostics);
    if (isInterruptionRequested()) {
        return;
    }
    const auto report = orion::diagnostics::buildReport(snapshot);
    if (!isInterruptionRequested()) {
        emit reportReady(report);
    }
}

} // namespace orion::app
