#pragma once

#include <QDialog>

class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTimer;

namespace orion::app
{

class StartupDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit StartupDialog(QWidget* parent = nullptr);
    void setProgress(int value, const QString& stage);
    void appendLog(const QString& line);
    void setDetailsVisible(bool visible);
    void startRevealTimeout(int milliseconds = 15000);
    void dismiss();
    [[nodiscard]] int progress() const;

public slots:
    void requestSkip();
    void reject() override;

signals:
    void skipRequested();
    void abortRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void requestAbort();
    QProgressBar* progress_;
    QPlainTextEdit* log_;
    QPushButton* details_;
    QPushButton* skip_;
    QTimer* revealTimer_;
    bool dismissed_ { false };
    bool skipSent_ { false };
    bool abortSent_ { false };
};

} // namespace orion::app
