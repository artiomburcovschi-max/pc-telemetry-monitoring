#include "startup_sequence.h"

#include <QTimer>
#include <array>

namespace orion::app
{
namespace
{
    const std::array<int, 5> endpoints { 5, 65, 75, 95, 100 };
    QString title(StartupSequence::Phase phase)
    {
        switch (phase)
        {
        case StartupSequence::Phase::Telemetry:
            return QStringLiteral("Первый замер телеметрии");
        case StartupSequence::Phase::Hardware:
            return QStringLiteral("Характеристики компьютера");
        case StartupSequence::Phase::Autostart:
            return QStringLiteral("Автозагрузка");
        case StartupSequence::Phase::Diagnostics:
            return QStringLiteral("SMART и журнал ошибок");
        case StartupSequence::Phase::PublicIp:
            return QStringLiteral("Публичный IP и провайдер");
        case StartupSequence::Phase::Complete:
            return QStringLiteral("Подготовка завершена");
        }
        return {};
    }
}

StartupSequence::StartupSequence(QObject* parent)
    : QObject(parent)
{
}

void StartupSequence::start(bool includeOnlineLookup)
{
    if (started_ || cancelled_)
        return;
    started_ = true;
    includeOnline_ = includeOnlineLookup;
    requestCurrent();
}

void StartupSequence::publish(int value, const QString& stage)
{
    progress_ = qBound(progress_, value, 100);
    emit progressChanged(progress_, stage);
}

void StartupSequence::requestCurrent()
{
    if (!isActive() || paused_ || completed_)
        return;
    publish(progress_, title(phase_) + QStringLiteral("…"));
    emit logLine(QStringLiteral("Начат этап: %1").arg(title(phase_)));
    if (phase_ == Phase::PublicIp && !includeOnline_)
    {
        completePhase(phase_, false, QStringLiteral("пропущено: проверочный запуск без публичного IP"));
        return;
    }
    emit phaseRequested(phase_);
}

void StartupSequence::completePhase(Phase phase, bool warning, const QString& detail)
{
    if (!isActive() || phase != phase_ || completed_)
        return;
    completed_ = true;
    warnings_ |= warning;
    const QString result
        = QStringLiteral("%1: %2%3")
              .arg(title(phase_),
                  warning ? QStringLiteral("предупреждение") : QStringLiteral("этап завершён"),
                  detail.isEmpty() ? QString() : QStringLiteral(" · %1").arg(detail));
    emit logLine(result);
    publish(endpoints.at(static_cast<size_t>(phase)), result);
    scheduleNext();
}

void StartupSequence::scheduleNext()
{
    if (!isActive() || paused_ || !completed_ || queued_)
        return;
    queued_ = true;
    QTimer::singleShot(0, this,
        [this]
        {
            queued_ = false;
            if (!isActive() || paused_ || !completed_)
                return;
            phase_ = static_cast<Phase>(static_cast<int>(phase_) + 1);
            completed_ = false;
            if (phase_ == Phase::Complete)
            {
                emit logLine(warnings_ ? QStringLiteral("Подготовка завершена с предупреждениями")
                                       : QStringLiteral("Подготовка завершена"));
                emit finished(warnings_);
            }
            else
                requestCurrent();
        });
}

void StartupSequence::hardwareProgress(int completed, int total, const QString& label)
{
    if (!isActive() || phase_ != Phase::Hardware || completed_ || paused_ || total <= 0)
        return;
    const int value = 5 + qRound(59.0 * qBound(0, completed, total) / total);
    const QString stage = QStringLiteral("Железо %1/%2 · %3").arg(completed).arg(total).arg(label);
    // Collector checkpoints cannot claim that the final report has been delivered.
    if (value >= progress_)
    {
        publish(value, stage);
        emit logLine(stage);
    }
}

void StartupSequence::setPaused(bool paused)
{
    if (paused_ == paused)
        return;
    paused_ = paused;
    if (!isActive())
        return;
    if (paused)
    {
        emit logLine(QStringLiteral("Стартовый сбор приостановлен"));
    }
    else if (completed_)
        scheduleNext();
    else
        requestCurrent();
}

void StartupSequence::cancel()
{
    if (cancelled_ || phase_ == Phase::Complete)
        return;
    cancelled_ = true;
    emit logLine(QStringLiteral("Стартовая подготовка отменена"));
}

} // namespace orion::app
