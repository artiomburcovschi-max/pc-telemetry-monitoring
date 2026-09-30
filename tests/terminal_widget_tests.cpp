#include "terminal_text.h"
#include "widgets/terminal_widget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    const auto colored = orion::app::filterTerminalOutput(
        QString::fromUtf8("plain\x1b[31m red\x1b[0m\a"));
    if (colored.clearScreen || colored.text != QStringLiteral("plain red")) {
        std::cerr << "ANSI terminal output was not cleaned deterministically.\n";
        return EXIT_FAILURE;
    }
    const auto cleared = orion::app::filterTerminalOutput(
        QString::fromUtf8("old\x1b[2J\x1b[Hprompt> "));
    if (!cleared.clearScreen || cleared.text != QStringLiteral("oldprompt> ")) {
        std::cerr << "Terminal clear-screen control was not preserved as an action.\n";
        return EXIT_FAILURE;
    }

    orion::app::TerminalWidget terminal;
    QElapsedTimer timer;
    timer.start();
    while (!terminal.shellRunning() && timer.elapsed() < 3500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(10);
    }
    if (!terminal.shellRunning()) {
        std::cerr << "The embedded system shell did not start.\n";
        return EXIT_FAILURE;
    }
    terminal.terminateShell();
    if (terminal.shellRunning()) {
        std::cerr << "The embedded system shell did not terminate within its bound.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Native terminal filtering and lifecycle tests passed.\n";
    return EXIT_SUCCESS;
}
