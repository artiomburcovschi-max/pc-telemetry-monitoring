#include "terminal_widget.h"

#include "../terminal_text.h"

#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextCursor>
#include <QVBoxLayout>

namespace orion::app {

TerminalOutput::TerminalOutput(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("TerminalOutput"));
    setReadOnly(false);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setUndoRedoEnabled(false);
}

void TerminalOutput::lockInputStart()
{
    moveCursor(QTextCursor::End);
    inputStart_ = textCursor().position();
}

QString TerminalOutput::currentInput() const
{
    QTextCursor cursor(document());
    cursor.setPosition(inputStart_);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    return cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
}

void TerminalOutput::replaceCurrentInput(const QString& text)
{
    QTextCursor cursor(document());
    cursor.setPosition(inputStart_);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.insertText(text);
    setTextCursor(cursor);
    moveCursor(QTextCursor::End);
}

void TerminalOutput::navigateHistory(const int direction)
{
    if (history_.isEmpty()) return;
    historyIndex_ = qBound(0, historyIndex_ + direction, history_.size());
    replaceCurrentInput(historyIndex_ == history_.size()
        ? QString() : history_.at(historyIndex_));
}

void TerminalOutput::keyPressEvent(QKeyEvent* event)
{
    auto cursor = textCursor();
    const bool navigationKey = event->key() == Qt::Key_Left
        || event->key() == Qt::Key_Right || event->key() == Qt::Key_Up
        || event->key() == Qt::Key_Down;
    if (cursor.position() < inputStart_ && !navigationKey) {
        moveCursor(QTextCursor::End);
        cursor = textCursor();
    }
    if (event->key() == Qt::Key_L && event->modifiers().testFlag(Qt::ControlModifier)) {
        clear();
        inputStart_ = 0;
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QString line = currentInput();
        history_.append(line);
        historyIndex_ = history_.size();
        moveCursor(QTextCursor::End);
        insertPlainText(QStringLiteral("\n"));
        emit lineSubmitted(line);
        inputStart_ = textCursor().position();
        return;
    }
    if (event->key() == Qt::Key_Backspace && cursor.position() <= inputStart_) {
        return;
    }
    if (event->key() == Qt::Key_Up) {
        navigateHistory(-1);
        return;
    }
    if (event->key() == Qt::Key_Down) {
        navigateHistory(1);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void TerminalOutput::appendProcessOutput(const QString& text)
{
    const auto filtered = filterTerminalOutput(text);
    if (filtered.clearScreen) {
        clear();
        inputStart_ = 0;
    }
    moveCursor(QTextCursor::End);
    insertPlainText(filtered.text);
    moveCursor(QTextCursor::End);
    inputStart_ = textCursor().position();
}

TerminalWidget::TerminalWidget(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("TerminalWidget"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    shellName_ = QStringLiteral("PowerShell");
#ifndef Q_OS_WIN
    shellName_ = QStringLiteral("bash");
#endif
    title_ = new QLabel(QStringLiteral("TERMINAL — %1").arg(shellName_), this);
    title_->setObjectName(QStringLiteral("Header"));
    layout->addWidget(title_);
    output_ = new TerminalOutput(this);
    layout->addWidget(output_, 1);
    refreshTheme(QStringLiteral("#00FF41"), QStringLiteral("'Consolas', monospace"));
    printBanner();
    connect(output_, &TerminalOutput::lineSubmitted, this, &TerminalWidget::sendCommand);
    startShell();
}

TerminalWidget::~TerminalWidget()
{
    terminateShell();
}

void TerminalWidget::refreshTheme(const QString& textColor, const QString& fontFamily)
{
    output_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#TerminalOutput { background: #000000; color: %1; "
        "font-family: %2; font-size: 13px; border: 1px solid #303038; padding: 6px; }")
        .arg(textColor, fontFamily));
}

void TerminalWidget::printBanner(const bool themeSwitch)
{
    const QString banner = QStringLiteral(
        "   ___  ____  ___ ___  _   _\n"
        "  / _ \\|  _ \\|_ _/ _ \\| \\ | |\n"
        " | | | | |_) || | | | |  \\| |\n"
        " | |_| |  _ < | | |_| | |\\  |\n"
        "  \\___/|_| \\_\\___\\___/|_| \\_|\n");
    output_->setPlainText(banner + (themeSwitch
        ? QStringLiteral("\nТема применена — сессия оболочки продолжается.\n")
        : QStringLiteral("\nСистемная оболочка запускается…\n")));
#ifdef Q_OS_WIN
    output_->appendPlainText(QStringLiteral(
        "\nПолезные команды:\n"
        "  Get-Process          — список процессов\n"
        "  Get-ComputerInfo     — информация о системе\n"
        "  Test-Connection host — проверка доступности\n"
        "  Get-NetTCPConnection — сетевые подключения\n"
        "  Get-PSDrive          — диски\n"
        "  cls                  — очистить экран\n"));
#else
    output_->appendPlainText(QStringLiteral(
        "\nПолезные команды: ps aux, df -h, ping <host>, clear\n"));
#endif
    output_->lockInputStart();
}

void TerminalWidget::startShell()
{
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    auto environment = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
    QString executable = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    QStringList arguments;
    if (!executable.isEmpty()) {
        arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
            QStringLiteral("-NoExit"), QStringLiteral("-Command"),
            QStringLiteral("[Console]::InputEncoding=[Text.UTF8Encoding]::new($false); "
                           "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); "
                           "$OutputEncoding=[Text.UTF8Encoding]::new($false)")};
    } else {
        executable = QStandardPaths::findExecutable(QStringLiteral("cmd.exe"));
        shellName_ = QStringLiteral("cmd.exe");
        title_->setText(QStringLiteral("TERMINAL — %1").arg(shellName_));
    }
#else
    const QString executable = QStringLiteral("/bin/bash");
    const QStringList arguments {QStringLiteral("-i")};
    environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm"));
    environment.insert(QStringLiteral("PS1"), QStringLiteral("\\u@\\h:\\w$ "));
#endif
    process_->setProcessEnvironment(environment);
    process_->setWorkingDirectory(QDir::homePath());
    connect(process_, &QProcess::readyReadStandardOutput, this, &TerminalWidget::readOutput);
    connect(process_, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            output_->appendProcessOutput(QStringLiteral("\n[ошибка: не удалось запустить оболочку]\n"));
        }
    });
    process_->start(executable, arguments);
    if (!process_->waitForStarted(3000)) {
        output_->appendProcessOutput(QStringLiteral("\n[ошибка: оболочка недоступна]\n"));
    }
}

void TerminalWidget::readOutput()
{
    if (process_ == nullptr) return;
    const QByteArray bytes = process_->readAllStandardOutput();
    output_->appendProcessOutput(QString::fromUtf8(bytes));
}

void TerminalWidget::sendCommand(const QString& line)
{
    if (shellRunning()) {
        process_->write(line.toUtf8() + '\n');
    }
}

bool TerminalWidget::shellRunning() const
{
    return process_ != nullptr && process_->state() == QProcess::Running;
}

void TerminalWidget::terminateShell()
{
    if (process_ == nullptr || process_->state() == QProcess::NotRunning) return;
    process_->write("exit\n");
    process_->closeWriteChannel();
    if (!process_->waitForFinished(700)) {
        process_->terminate();
        if (!process_->waitForFinished(900)) {
            process_->kill();
            process_->waitForFinished(1000);
        }
    }
}

} // namespace orion::app
