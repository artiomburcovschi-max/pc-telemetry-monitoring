#include "full_scan_dialog.h"

#include "full_scan_worker.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace orion::app
{

FullScanDialog::FullScanDialog(FullScanWorker* worker, QWidget* parent)
    : QDialog(parent), worker_(worker)
{
    setObjectName(QStringLiteral("FullScanDialog"));
    setWindowTitle(QStringLiteral("Полная проверка"));
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(680, 520);
    setMinimumSize(540, 440);
    if (parent)
        setStyleSheet(parent->styleSheet());
    auto* layout = new QVBoxLayout(this);
    status_ = new QLabel(QStringLiteral("Локальные проверки и нагрузка, затем антивирус, "
                                        "скорость интернета и публичный IP."), this);
    status_->setObjectName(QStringLiteral("FullScanStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("FullScanProgress"));
    progress_->setRange(0, 100);
    layout->addWidget(progress_);
    verdict_ = new QLabel(this);
    verdict_->setObjectName(QStringLiteral("FullScanVerdict"));
    verdict_->setTextFormat(Qt::PlainText);
    verdict_->setWordWrap(true);
    verdict_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    verdict_->hide();
    layout->addWidget(verdict_);
    log_ = new QPlainTextEdit(this);
    log_->setObjectName(QStringLiteral("FullScanLog"));
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2500);
    layout->addWidget(log_, 1);
    auto* buttons = new QHBoxLayout;
    copy_ = new QPushButton(QStringLiteral("📋 Скопировать отчёт"), this);
    copy_->setObjectName(QStringLiteral("FullScanCopy"));
    copy_->setEnabled(false);
    stop_ = new QPushButton(QStringLiteral("Остановить"), this);
    stop_->setObjectName(QStringLiteral("FullScanStop"));
    auto* close = new QPushButton(QStringLiteral("Закрыть"), this);
    close->setObjectName(QStringLiteral("FullScanClose"));
    buttons->addWidget(copy_);
    buttons->addStretch();
    buttons->addWidget(stop_);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(copy_, &QPushButton::clicked, this, [this]
            {
                if (!finalText_.isEmpty())
                    QApplication::clipboard()->setText(finalText_);
            });
    connect(stop_, &QPushButton::clicked, this, [this]
            {
                worker_->requestStop();
                stop_->setEnabled(false);
                status_->setText(QStringLiteral("Останавливаем; текущий локальный или сетевой запрос завершается…"));
            });
    connect(close, &QPushButton::clicked, this, &FullScanDialog::close);
    connect(worker_, &FullScanWorker::progressChanged, this,
            [this](const int value, const QString& text)
            {
                progress_->setValue(value);
                if (stop_->isEnabled())
                    status_->setText(text);
            });
    connect(worker_, &FullScanWorker::logLine, log_, &QPlainTextEdit::appendPlainText);
    connect(worker_, &FullScanWorker::reportReady, this, &FullScanDialog::renderReport);
    connect(worker_, &QThread::finished, this, [this]
            {
                stop_->setEnabled(false);
                if (closePending_)
                    this->close();
            });
}

void FullScanDialog::renderReport(const QJsonObject& report)
{
    finalText_ = FullScanWorker::reportText(report);
    copy_->setEnabled(true);
    stop_->setEnabled(false);
    const QString state = report.value(QStringLiteral("scan")).toObject().value(QStringLiteral("state")).toString();
    const bool complete = state == QStringLiteral("complete");
    status_->setText(complete ? QStringLiteral("Готово — отчёт можно скопировать.")
                     : state == QStringLiteral("cancelled")
                         ? QStringLiteral("Проверка остановлена. Сохранён частичный отчёт.")
                         : QStringLiteral("Проверка неполная. Причина указана в отчёте."));
    QStringList lines;
    lines << (complete ? report.value(QStringLiteral("risk_assessment")).toObject()
                             .value(QStringLiteral("headline")).toString(QStringLiteral("Проверка завершена"))
                       : QStringLiteral("НЕПОЛНАЯ ПРОВЕРКА — это не заключение об исправности ПК"));
    lines << QStringLiteral("ПК: %1 · интернет: %2")
                 .arg(report.value(QStringLiteral("pc_rating")).toObject()
                              .value(QStringLiteral("label")).toString(QStringLiteral("н/д")),
                      report.value(QStringLiteral("internet_rating")).toObject()
                              .value(QStringLiteral("label")).toString(QStringLiteral("н/д")));
    const auto findings = report.value(QStringLiteral("verdict_findings")).toArray();
    bool critical = false;
    for (const auto& value : findings)
        critical |= value.toObject().value(QStringLiteral("severity")) == QStringLiteral("critical");
    verdict_->setProperty("critical", critical);
    if (critical)
        verdict_->setStyleSheet(QStringLiteral("border-left: 4px solid #FF3B30; padding: 8px;"));
    for (int i = 0; i < qMin(3, static_cast<int>(findings.size())); ++i)
        lines << QStringLiteral("• %1").arg(findings.at(i).toObject().value(QStringLiteral("title")).toString());
    lines << report.value(QStringLiteral("coverage")).toObject().value(QStringLiteral("message")).toString();
    verdict_->setText(lines.join(QLatin1Char('\n')));
    verdict_->show();
    log_->appendPlainText(QStringLiteral("\n=== ИТОГОВЫЙ ОТЧЁТ ===\n") + finalText_);
}

void FullScanDialog::cancelAndClose()
{
    closePending_ = true;
    worker_->requestStop();
    stop_->setEnabled(false);
    status_->setText(QStringLiteral("Останавливаем проверку перед закрытием…"));
}

void FullScanDialog::reject()
{
    if (worker_->isRunning())
        cancelAndClose();
    else
        QDialog::reject();
}

void FullScanDialog::closeEvent(QCloseEvent* event)
{
    if (worker_->isRunning())
    {
        cancelAndClose();
        event->ignore();
    }
    else
        QDialog::closeEvent(event);
}

} // namespace orion::app
