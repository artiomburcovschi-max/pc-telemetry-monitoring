#pragma once

#include <QObject>
#include <QString>

namespace orion::app
{

// Coordinates existing collectors. It performs no I/O and owns no worker thread.
class StartupSequence final : public QObject
{
    Q_OBJECT
public:
    enum class Phase
    {
        Telemetry,
        Hardware,
        Autostart,
        Diagnostics,
        PublicIp,
        Complete
    };
    Q_ENUM(Phase)
    explicit StartupSequence(QObject* parent = nullptr);
    void start(bool includeOnlineLookup);
    void completePhase(Phase phase, bool warning = false, const QString& detail = {});
    void hardwareProgress(int completed, int total, const QString& label);
    void setPaused(bool paused);
    void cancel();
    [[nodiscard]] bool isActive() const { return started_ && !cancelled_ && phase_ != Phase::Complete; }
    [[nodiscard]] Phase phase() const { return phase_; }
    [[nodiscard]] int progress() const { return progress_; }

signals:
    void phaseRequested(orion::app::StartupSequence::Phase phase);
    void progressChanged(int percent, const QString& stage);
    void logLine(const QString& line);
    void finished(bool withWarnings);

private:
    void scheduleNext();
    void requestCurrent();
    void publish(int value, const QString& stage);
    Phase phase_ { Phase::Telemetry };
    bool started_ { false };
    bool cancelled_ { false };
    bool paused_ { false };
    bool includeOnline_ { true };
    bool completed_ { false };
    bool queued_ { false };
    bool warnings_ { false };
    int progress_ { 0 };
};

} // namespace orion::app
