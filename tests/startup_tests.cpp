#include "startup_dialog.h"
#include "startup_sequence.h"

#include <QApplication>
#include <QEventLoop>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <cstdlib>
#include <iostream>

namespace
{
void drain()
{
    for (int i = 0; i < 8; ++i)
        QApplication::processEvents();
}
void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    using Sequence = orion::app::StartupSequence;
    using Phase = Sequence::Phase;
    Sequence sequence;
    QVector<Phase> requested;
    int lastProgress = 0;
    int finishes = 0;
    bool warnings = false;
    QObject::connect(
        &sequence, &Sequence::phaseRequested, &app, [&](Phase phase) { requested.append(phase); });
    QObject::connect(&sequence, &Sequence::progressChanged, &app,
        [&](int value, const QString&)
        {
            require(value >= lastProgress && value <= 100, "Progress regressed or exceeded completion");
            lastProgress = value;
        });
    QObject::connect(&sequence, &Sequence::finished, &app,
        [&](bool warning)
        {
            ++finishes;
            warnings = warning;
        });
    sequence.start(true);
    sequence.start(true);
    require(requested == QVector<Phase> { Phase::Telemetry }, "Startup started duplicate collectors");
    sequence.completePhase(Phase::Hardware);
    require(sequence.progress() == 0, "Out-of-order report advanced startup");
    sequence.completePhase(Phase::Telemetry);
    drain();
    require(sequence.phase() == Phase::Hardware && sequence.progress() == 5, "Telemetry gate broken");
    sequence.hardwareProgress(15, 15, QStringLiteral("готовы секции"));
    require(sequence.progress() == 64 && finishes == 0, "Hardware checkpoints claimed final report");
    sequence.hardwareProgress(1, 15, QStringLiteral("старый шаг"));
    require(sequence.progress() == 64, "Old hardware progress regressed");
    sequence.setPaused(true);
    sequence.completePhase(Phase::Hardware, true, QStringLiteral("частичный отчёт"));
    drain();
    require(sequence.phase() == Phase::Hardware && requested.size() == 2, "Pause launched another collector");
    sequence.setPaused(false);
    drain();
    require(sequence.phase() == Phase::Autostart, "Resume failed to advance completed phase");
    sequence.completePhase(Phase::Autostart);
    drain();
    sequence.completePhase(Phase::Diagnostics);
    drain();
    require(sequence.phase() == Phase::PublicIp && sequence.progress() == 95 && finishes == 0,
        "Startup completed before IP result");
    sequence.completePhase(Phase::PublicIp, true, QStringLiteral("нет соединения"));
    drain();
    sequence.completePhase(Phase::PublicIp);
    require(!sequence.isActive() && sequence.progress() == 100 && finishes == 1 && warnings,
        "Warning completion or idempotence failed");

    Sequence offline;
    bool offlineDone = false;
    QObject::connect(&offline, &Sequence::phaseRequested, &app,
        [&](Phase phase)
        {
            require(phase != Phase::PublicIp, "Verification mode made an Internet request");
            offline.completePhase(phase);
        });
    QObject::connect(&offline, &Sequence::finished, &app, [&](bool) { offlineDone = true; });
    offline.start(false);
    drain();
    require(offlineDone && offline.progress() == 100, "Offline startup did not complete");

    Sequence cancelled;
    int cancelledRequests = 0;
    QObject::connect(&cancelled, &Sequence::phaseRequested, &app, [&](Phase) { ++cancelledRequests; });
    cancelled.start(true);
    cancelled.completePhase(Phase::Telemetry);
    cancelled.cancel();
    drain();
    require(!cancelled.isActive() && cancelledRequests == 1 && cancelled.progress() != 100,
        "Cancellation allowed queued work or fabricated success");
    Sequence held;
    int heldRequests = 0;
    QObject::connect(&held, &Sequence::phaseRequested, &app, [&](Phase) { ++heldRequests; });
    held.setPaused(true);
    held.start(false);
    require(heldRequests == 0, "Initially paused startup launched work");
    held.setPaused(false);
    require(heldRequests == 1, "Initially paused startup did not resume");

    orion::app::StartupDialog dialog;
    int skips = 0;
    int aborts = 0;
    QObject::connect(&dialog, &orion::app::StartupDialog::skipRequested, &app, [&] { ++skips; });
    QObject::connect(&dialog, &orion::app::StartupDialog::abortRequested, &app, [&] { ++aborts; });
    dialog.show();
    drain();
    auto* bar = dialog.findChild<QProgressBar*>(QStringLiteral("StartupProgress"));
    auto* log = dialog.findChild<QPlainTextEdit*>(QStringLiteral("StartupLog"));
    require(
        dialog.size() == QSize(430, 96) && bar && log && log->isHidden(), "Compact startup layout differs");
    dialog.setProgress(40, QStringLiteral("Железо"));
    dialog.setProgress(20, QStringLiteral("Старый этап"));
    require(dialog.progress() == 40 && bar->maximum() == 100, "Dialog progress regressed");
    dialog.setDetailsVisible(true);
    for (int i = 0; i < 260; ++i)
        dialog.appendLog(QStringLiteral("Шаг %1").arg(i));
    require(dialog.size() == QSize(430, 310) && !log->isHidden() && log->blockCount() == 240,
        "Expanded startup log is not bounded");
    dialog.requestSkip();
    dialog.requestSkip();
    require(
        skips == 1 && aborts == 0 && dialog.progress() == 40, "Skip fabricated completion or aborted work");
    dialog.dismiss();
    require(aborts == 0, "Normal startup dismissal requested exit");

    orion::app::StartupDialog timeout;
    int timedSkips = 0;
    QObject::connect(&timeout, &orion::app::StartupDialog::skipRequested, &app, [&] { ++timedSkips; });
    timeout.show();
    timeout.startRevealTimeout(20);
    QEventLoop wait;
    QTimer::singleShot(60, &wait, &QEventLoop::quit);
    wait.exec();
    require(timedSkips == 1 && timeout.progress() == 0, "Deadline did not reveal without false completion");
    timeout.dismiss();
    orion::app::StartupDialog close;
    QObject::connect(&close, &orion::app::StartupDialog::abortRequested, &app, [&] { ++aborts; });
    close.show();
    close.close();
    close.reject();
    require(aborts == 1, "Window close did not abort exactly once");
    orion::app::StartupDialog escape;
    QObject::connect(&escape, &orion::app::StartupDialog::abortRequested, &app, [&] { ++aborts; });
    escape.show();
    escape.reject();
    require(aborts == 2, "Escape did not abort startup");
    std::cout << "Startup sequence, real checkpoints, pause, warnings, cancellation, skip, deadline and "
                 "dialog passed.\n";
    return EXIT_SUCCESS;
}
