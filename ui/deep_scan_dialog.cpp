#include "deep_scan_dialog.h"
#include "deep_scan_worker.h"

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

DeepScanDialog::DeepScanDialog(DeepScanWorker* worker, QWidget* parent) : QDialog(parent), worker_(worker)
{
    setObjectName(QStringLiteral("DeepScanDialog"));
    setWindowTitle(QStringLiteral("Глубокая диагностика"));
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(640, 480);
    setMinimumSize(520, 420);
    if (parent)
        setStyleSheet(parent->styleSheet());
    auto* layout = new QVBoxLayout(this);
    status_ = new QLabel(QStringLiteral("Локальная проверка: характеристики, SMART, "
                                        "журнал, автозагрузка и нагрузка."),
                         this);
    status_->setObjectName(QStringLiteral("DeepScanStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("DeepScanProgress"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    layout->addWidget(progress_);
    verdict_ = new QLabel(this);
    verdict_->setObjectName(QStringLiteral("DeepScanVerdict"));
    verdict_->setTextFormat(Qt::PlainText);
    verdict_->setWordWrap(true);
    verdict_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    verdict_->hide();
    layout->addWidget(verdict_);
    log_ = new QPlainTextEdit(this);
    log_->setObjectName(QStringLiteral("DeepScanLog"));
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    layout->addWidget(log_, 1);
    auto* buttons = new QHBoxLayout;
    copy_ = new QPushButton(QStringLiteral("📋 Скопировать отчёт"), this);
    copy_->setObjectName(QStringLiteral("DeepScanCopy"));
    copy_->setEnabled(false);
    stop_ = new QPushButton(QStringLiteral("Остановить"), this);
    stop_->setObjectName(QStringLiteral("DeepScanStop"));
    auto* close = new QPushButton(QStringLiteral("Закрыть"), this);
    close->setObjectName(QStringLiteral("DeepScanClose"));
    buttons->addWidget(copy_);
    buttons->addStretch();
    buttons->addWidget(stop_);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(copy_, &QPushButton::clicked, this,
            [this]
            {
                if (!finalText_.isEmpty())
                    QApplication::clipboard()->setText(finalText_);
            });
    connect(stop_, &QPushButton::clicked, this,
            [this]
            {
                worker_->requestStop();
                stop_->setEnabled(false);
                status_->setText(
                    QStringLiteral("Останавливаем проверку; ожидаем завершения текущего сборщика…"));
            });
    connect(close, &QPushButton::clicked, this, &DeepScanDialog::close);
    connect(worker_, &DeepScanWorker::progressChanged, this,
            [this](int value, const QString& text)
            {
                progress_->setValue(value);
                if (stop_->isEnabled())
                    status_->setText(text);
            });
    connect(worker_, &DeepScanWorker::logLine, log_, &QPlainTextEdit::appendPlainText);
    connect(worker_, &DeepScanWorker::reportReady, this, &DeepScanDialog::renderReport);
    connect(worker_, &QThread::finished, this,
            [this]
            {
                stop_->setEnabled(false);
                if (closePending_)
                    this->close();
            });
}

void DeepScanDialog::renderReport(const QJsonObject& report)
{
    finalText_ = DeepScanWorker::reportText(report);
    copy_->setEnabled(true);
    stop_->setEnabled(false);
    const auto state = report.value("scan").toObject().value("state").toString();
    const bool complete = state == QStringLiteral("complete");
    status_->setText(complete ? QStringLiteral("Готово — отчёт можно скопировать.")
                     : state == QStringLiteral("cancelled")
                         ? QStringLiteral("Проверка остановлена. Сохранён частичный отчёт.")
                         : QStringLiteral("Проверка неполная. Причина указана в отчёте."));
    QStringList lines;
    lines << (complete ? report.value("risk_assessment").toObject().value("headline").toString()
                       : QStringLiteral("НЕПОЛНАЯ ПРОВЕРКА — это не заключение об исправности ПК"));
    const auto findings = report.value("verdict_findings").toArray();
    bool critical = false;
    for (const auto& value : findings)
        critical |= value.toObject().value("severity") == "critical";
    verdict_->setProperty("critical", critical);
    if (critical)
        verdict_->setStyleSheet(QStringLiteral("border-left: 4px solid #FF3B30; padding: 8px;"));
    for (int i = 0; i < qMin(3, static_cast<int>(findings.size())); ++i)
    {
        const auto finding = findings.at(i).toObject();
        lines << QStringLiteral("• %1").arg(finding.value("title").toString());
    }
    lines << report.value("coverage").toObject().value("message").toString();
    verdict_->setText(lines.join(QLatin1Char('\n')));
    verdict_->show();
    log_->appendPlainText(QStringLiteral("\n=== ИТОГОВЫЙ ОТЧЁТ ===\n") + finalText_);
}

void DeepScanDialog::cancelAndClose()
{
    closePending_ = true;
    worker_->requestStop();
    stop_->setEnabled(false);
    status_->setText(QStringLiteral("Останавливаем проверку перед закрытием…"));
}

void DeepScanDialog::reject()
{
    if (worker_->isRunning())
        cancelAndClose();
    else
        QDialog::reject();
}

void DeepScanDialog::closeEvent(QCloseEvent* event)
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
