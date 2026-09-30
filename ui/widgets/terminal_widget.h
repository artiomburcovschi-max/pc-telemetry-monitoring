#pragma once

#include <QFrame>
#include <QPlainTextEdit>

class QKeyEvent;
class QLabel;
class QProcess;

namespace orion::app {

class TerminalOutput final : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit TerminalOutput(QWidget* parent = nullptr);

    void lockInputStart();
    void appendProcessOutput(const QString& text);
    [[nodiscard]] QString currentInput() const;

signals:
    void lineSubmitted(const QString& line);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void navigateHistory(int direction);
    void replaceCurrentInput(const QString& text);

    int inputStart_ {0};
    QStringList history_;
    int historyIndex_ {0};
};

class TerminalWidget final : public QFrame {
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget* parent = nullptr);
    ~TerminalWidget() override;

    void terminateShell();
    void refreshTheme(const QString& textColor, const QString& fontFamily);
    [[nodiscard]] bool shellRunning() const;

private:
    void printBanner(bool themeSwitch = false);
    void startShell();
    void readOutput();
    void sendCommand(const QString& line);

    QLabel* title_ {nullptr};
    TerminalOutput* output_ {nullptr};
    QProcess* process_ {nullptr};
    QString shellName_;
};

} // namespace orion::app
