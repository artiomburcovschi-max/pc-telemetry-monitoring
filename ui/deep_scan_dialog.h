#pragma once

#include <QDialog>
#include <QJsonObject>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QCloseEvent;

namespace orion::app
{
class DeepScanWorker;

class DeepScanDialog final : public QDialog
{
    Q_OBJECT
  public:
    explicit DeepScanDialog(DeepScanWorker* worker, QWidget* parent = nullptr);
    void reject() override;

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void renderReport(const QJsonObject& report);
    void cancelAndClose();
    DeepScanWorker* worker_;
    QLabel* status_;
    QLabel* verdict_;
    QPlainTextEdit* log_;
    QProgressBar* progress_;
    QPushButton* copy_;
    QPushButton* stop_;
    bool closePending_{false};
    QString finalText_;
};
} // namespace orion::app
