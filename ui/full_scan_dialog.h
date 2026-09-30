#pragma once

#include <QDialog>
#include <QJsonObject>

class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace orion::app
{
class FullScanWorker;

class FullScanDialog final : public QDialog
{
    Q_OBJECT
  public:
    explicit FullScanDialog(FullScanWorker* worker, QWidget* parent = nullptr);
    void reject() override;

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void renderReport(const QJsonObject& report);
    void cancelAndClose();

    FullScanWorker* worker_;
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

