#include "process_action_worker.h"
#include "orion/platform/process_collector.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}
bool collect(orion::app::ProcessActionWorker& worker)
{
    const bool finished = worker.wait(5000);
    QCoreApplication::processEvents();
    return finished;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--fixture-child"))) {
        QTimer::singleShot(15000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    using orion::app::ProcessActionWorker;
    bool passed = true;
    int calls = 0;
    QJsonObject report;
    ProcessActionWorker fixture([&](quint32 pid, quint64 identity) {
        ++calls;
        QThread::msleep(30);
        return QJsonObject{{"code", "access_denied"}, {"message", "fixture"},
            {"ok", false}, {"identity", static_cast<qint64>(identity)}, {"fixture_pid", static_cast<qint64>(pid)}};
    });
    QObject::connect(&fixture, &ProcessActionWorker::resultReady, &app,
        [&](const QJsonObject& value) { report = value; });
    passed &= check(!fixture.startTermination(12345, 123, false), "Unconfirmed action was accepted.");
    passed &= check(!fixture.startTermination(0, 123, true)
        && !fixture.startTermination(4, 123, true), "Reserved PID was accepted.");
    passed &= check(!fixture.startTermination(12345, 0, true), "Missing identity was accepted.");
    passed &= check(!fixture.startTermination(static_cast<quint32>(app.applicationPid()), 123, true),
        "Self termination was accepted.");
    passed &= check(calls == 0, "Rejected action invoked executor.");
    passed &= check(fixture.startTermination(12345, 123, true), "Confirmed fixture was rejected.");
    passed &= check(!fixture.startTermination(12346, 124, true), "Overlapping action was accepted.");
    passed &= check(collect(fixture) && calls == 1 && report.value("code") == "access_denied"
        && report.value("pid").toInt() == 12345 && report.value("identity").toInt() == 123
        && !report.value("ok").toBool(), "Action identity/error result was lost.");
    ProcessActionWorker throwing([](quint32, quint64) -> QJsonObject {
        throw std::runtime_error("fixture exception");
    });
    QObject::connect(&throwing, &ProcessActionWorker::resultReady, &app,
        [&](const QJsonObject& value) { report = value; });
    throwing.startTermination(12345, 123, true);
    passed &= check(collect(throwing) && report.value("code") == "error"
        && report.value("message") == "fixture exception", "Executor exception escaped.");

#ifdef Q_OS_WIN
    passed &= check(ProcessActionWorker::supported(), "Windows action was reported unsupported.");
    // The only native target is this test's own bounded-lifetime child, never a user process.
    QProcess child;
    child.start(app.applicationFilePath(), {QStringLiteral("--fixture-child")});
    if (!check(child.waitForStarted(3000), "Test child could not start.")) return EXIT_FAILURE;
    const auto childPid = static_cast<quint32>(child.processId());
    quint64 identity = 0;
    auto collector = orion::platform::makeProcessCollector();
    for (const auto& process : collector->sample().processes)
        if (process.pid == childPid) identity = process.creationIdentity;
    passed &= check(identity != 0, "Collector did not provide child creation identity.");
    ProcessActionWorker native;
    QObject::connect(&native, &ProcessActionWorker::resultReady, &app,
        [&](const QJsonObject& value) { report = value; });
    if (identity != 0) {
        native.startTermination(childPid, identity + 1, true);
        passed &= check(collect(native) && report.value("code") == "identity_changed",
            "Stale identity was not rejected.");
        passed &= check(child.state() == QProcess::Running, "Identity mismatch affected the child.");
        native.startTermination(childPid, identity, true);
        passed &= check(collect(native) && report.value("code") == "terminated",
            "Verified test-child termination failed.");
        passed &= check(child.state() == QProcess::NotRunning || child.waitForFinished(3000),
            "Test child did not exit.");
    }
    if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(3000); }
#else
    passed &= check(ProcessActionWorker::supported(), "Linux action was reported unsupported.");
    QProcess child;
    child.start(app.applicationFilePath(), {QStringLiteral("--fixture-child")});
    if (!check(child.waitForStarted(3000), "Test child could not start.")) return EXIT_FAILURE;
    const auto childPid = static_cast<quint32>(child.processId());
    quint64 identity = 0;
    auto collector = orion::platform::makeProcessCollector();
    for (const auto& process : collector->sample().processes)
        if (process.pid == childPid) identity = process.creationIdentity;
    passed &= check(identity != 0, "Collector did not provide child creation identity.");
    ProcessActionWorker native;
    QObject::connect(&native, &ProcessActionWorker::resultReady, &app,
        [&](const QJsonObject& value) { report = value; });
    if (identity != 0) {
        native.startTermination(childPid, identity + 1, true);
        passed &= check(collect(native) && report.value("code") == "identity_changed",
            "Stale identity was not rejected.");
        passed &= check(child.state() == QProcess::Running, "Identity mismatch affected the child.");
        native.startTermination(childPid, identity, true);
        passed &= check(collect(native) && report.value("code") == "terminated",
            "Verified test-child termination failed.");
        passed &= check(child.state() == QProcess::NotRunning || child.waitForFinished(3000),
            "Test child did not exit.");
    }
    if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(3000); }
    // An unrelated process outside this test's own child must never be reachable.
    QProcess unrelated;
    unrelated.start(QStringLiteral("sleep"), {QStringLiteral("30")});
    if (check(unrelated.waitForStarted(3000), "Unrelated process could not start.")) {
        quint64 unrelatedIdentity = 0;
        for (const auto& process : collector->sample().processes)
            if (process.pid == static_cast<quint32>(unrelated.processId()))
                unrelatedIdentity = process.creationIdentity;
        if (unrelatedIdentity != 0) {
            ProcessActionWorker guard;
            QObject::connect(&guard, &ProcessActionWorker::resultReady, &app,
                [&](const QJsonObject& value) { report = value; });
            guard.startTermination(static_cast<quint32>(unrelated.processId()),
                unrelatedIdentity + 1, true);
            passed &= check(collect(guard) && report.value("code") == "identity_changed"
                && unrelated.state() == QProcess::Running,
                "A stale-identity call reached an unrelated process.");
        }
        unrelated.kill();
        unrelated.waitForFinished(3000);
    }
#endif
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
