#include <QCoreApplication>
#include <QTimer>
#include <QProcess>

#ifndef Q_OS_WIN
#include <csignal>
#endif

// Modes (ORION_APP_FIXTURE_MODE):
//   quick-exit      exits with code 37 after ~120 ms
//   tree            starts one child (same executable, "--child"); ignores SIGTERM on
//                   non-Windows so only the force-terminate path can end it, matching a
//                   headless Windows process that never answers a window-close request
//   tree-graceful   same tree, but SIGTERM is honoured (exercises the cooperative path)
//   ignore-term     single process that ignores SIGTERM on non-Windows: stays alive through
//                   the grace period like a headless Windows process ignoring a close request
int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const auto mode = qEnvironmentVariable("ORION_APP_FIXTURE_MODE");
    const bool isChild = application.arguments().contains("--child");
#ifndef Q_OS_WIN
    if (mode == "tree" || mode == "ignore-term") std::signal(SIGTERM, SIG_IGN);
#endif
    if (mode == "quick-exit") {
        QTimer::singleShot(120, &application, [&] { application.exit(37); });
    } else if ((mode == "tree" || mode == "tree-graceful") && !isChild) {
#ifdef Q_OS_WIN
        QProcess::startDetached(application.applicationFilePath(), {"--child"});
#else
        // QProcess::startDetached() double-forks on Unix, which reparents the child to
        // init and hides it from parent/child tracking. Keep a genuine child process.
        auto* child = new QProcess(&application);
        child->setProgram(application.applicationFilePath());
        child->setArguments({QStringLiteral("--child")});
        child->start();
#endif
    }
    QTimer::singleShot(60000, &application, &QCoreApplication::quit);
    return application.exec();
}
