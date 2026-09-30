#include "autostart_worker.h"

#include "orion/core/autostart_entry.h"
#include "orion/platform/autostart_collector.h"

namespace orion::app {
namespace {

[[nodiscard]] QString localizedSource(const std::string& source)
{
    const QString value = QString::fromUtf8(source);
    if (value.startsWith(QStringLiteral("Registry: "))) {
        return QStringLiteral("Реестр: ") + value.sliced(10);
    }
    if (value == QStringLiteral("Startup (user folder)")) {
        return QStringLiteral("Автозагрузка (папка пользователя)");
    }
    if (value == QStringLiteral("Startup (common folder)")) {
        return QStringLiteral("Автозагрузка (общая папка)");
    }
    if (value == QStringLiteral("Startup (system desktop component)")) {
        return QStringLiteral("Автозагрузка (системный компонент рабочего стола)");
    }
    if (value == QStringLiteral("Windows Registry Run/RunOnce + Startup folders")) {
        return QStringLiteral("реестр Windows Run/RunOnce и папки Startup");
    }
    if (value == QStringLiteral("XDG autostart + systemd --user")) {
        return QStringLiteral("XDG autostart и systemd --user");
    }
    return value;
}

} // namespace

AutostartWorker::AutostartWorker(QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<QVector<AutostartTelemetry>>();
}

void AutostartWorker::scan()
{
    if (!isRunning()) {
        start();
    }
}

void AutostartWorker::stop()
{
    requestInterruption();
}

void AutostartWorker::run()
{
    emit scanStarted();
    auto collector = orion::platform::makeAutostartCollector();
    if (!collector || isInterruptionRequested()) {
        return;
    }
    const auto snapshot = collector->scan();
    if (isInterruptionRequested()) {
        return;
    }
    QVector<AutostartTelemetry> entries;
    entries.reserve(static_cast<qsizetype>(snapshot.entries.size()));
    for (const auto& entry : snapshot.entries) {
        entries.append({
            QString::fromUtf8(entry.name),
            QString::fromUtf8(entry.command),
            localizedSource(entry.source),
            entry.enabled.has_value() ? (*entry.enabled ? 1 : 0) : -1,
            QString::fromLatin1(orion::core::toString(entry.category)),
        });
    }
    emit entriesReady(entries, localizedSource(snapshot.source));
}

} // namespace orion::app
