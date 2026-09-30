#pragma once

#include <QString>

namespace orion::app {

struct TerminalTextResult {
    QString text;
    bool clearScreen {false};
};

[[nodiscard]] TerminalTextResult filterTerminalOutput(const QString& input);

} // namespace orion::app
