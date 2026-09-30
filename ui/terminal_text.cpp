#include "terminal_text.h"

#include <QRegularExpression>

namespace orion::app {

TerminalTextResult filterTerminalOutput(const QString& input)
{
    static const QRegularExpression clearPattern(
        QStringLiteral("\\x1B\\[[23]?J|\\x1B\\[H|\\x1Bc"));
    static const QRegularExpression ansiPattern(
        QStringLiteral(
            "\\x1B\\[[0-?]*[ -/]*[@-~]|\\x1B\\][^\\x07]*(?:\\x07|\\x1B\\\\)|[\\x07\\x08]"));

    TerminalTextResult result;
    result.clearScreen = input.contains(clearPattern);
    result.text = input;
    result.text.remove(clearPattern);
    result.text.remove(ansiPattern);
    return result;
}

} // namespace orion::app
