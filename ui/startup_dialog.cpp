#include "startup_dialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyle>
#include <QStyleFactory>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace orion::app
{

StartupDialog::StartupDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("StartupDialog"));
    setWindowTitle(QStringLiteral("O.R.I.O.N. — запуск"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
    setSizeGripEnabled(false);
#ifdef Q_OS_WIN
    setFont(QFont(QStringLiteral("Segoe UI"), 9));
#endif
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 11);
    layout->setSpacing(8);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("StartupProgress"));
    progress_->setAccessibleName(QStringLiteral("Ход запуска O.R.I.O.N."));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(20);
#ifdef Q_OS_WIN
    // Match Python's standard Windows control, not the app's decorative theme.
    auto* native = QStyleFactory::create(QStringLiteral("windowsvista"));
    if (native == nullptr) native = QStyleFactory::create(QStringLiteral("windows"));
    if (native != nullptr) {
        native->setParent(this);
        setStyle(native);
        progress_->setStyle(native);
    }
#endif
    layout->addWidget(progress_);
    auto* actions = new QHBoxLayout;
    details_ = new QPushButton(QStringLiteral("Подробнее"), this);
    details_->setObjectName(QStringLiteral("StartupDetails"));
    details_->setCheckable(true);
    details_->setAutoDefault(false);
    skip_ = new QPushButton(QStringLiteral("Пропустить"), this);
    skip_->setObjectName(QStringLiteral("StartupSkip"));
    skip_->setAutoDefault(false);
    skip_->setMinimumWidth(104);
    skip_->setToolTip(QStringLiteral("Открыть окно сейчас. Начатый сбор продолжится в фоне."));
    actions->addWidget(details_);
    actions->addStretch();
    actions->addWidget(skip_);
    layout->addLayout(actions);
    log_ = new QPlainTextEdit(this);
    log_->setObjectName(QStringLiteral("StartupLog"));
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(240);
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->hide();
    layout->addWidget(log_, 1);
    connect(details_, &QPushButton::toggled, this, &StartupDialog::setDetailsVisible);
    connect(skip_, &QPushButton::clicked, this, &StartupDialog::requestSkip);
    revealTimer_ = new QTimer(this);
    revealTimer_->setSingleShot(true);
    connect(revealTimer_, &QTimer::timeout, this,
        [this]
        {
            appendLog(QStringLiteral(
                "Подготовка занимает больше времени. Открываю окно; сбор продолжится в фоне."));
            requestSkip();
        });
    setFixedSize(430, 96);
}

int StartupDialog::progress() const { return progress_->value(); }

void StartupDialog::setProgress(int value, const QString& stage)
{
    if (dismissed_ || abortSent_)
        return;
    progress_->setValue(qBound(progress_->value(), value, 100));
    progress_->setToolTip(stage);
    progress_->setAccessibleDescription(stage);
}

void StartupDialog::appendLog(const QString& line)
{
    const QString clean = line.trimmed().left(2000);
    if (!clean.isEmpty())
        log_->appendPlainText(
            QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), clean));
    log_->horizontalScrollBar()->setValue(0);
}

void StartupDialog::setDetailsVisible(bool visible)
{
    if (details_->isChecked() != visible)
        details_->setChecked(visible);
    details_->setText(visible ? QStringLiteral("Скрыть") : QStringLiteral("Подробнее"));
    log_->setVisible(visible);
    setFixedSize(430, visible ? 310 : 96);
}

void StartupDialog::startRevealTimeout(int milliseconds)
{
    if (!dismissed_ && !skipSent_ && !abortSent_)
        revealTimer_->start(qBound(1, milliseconds, 15000));
}

void StartupDialog::requestSkip()
{
    if (dismissed_ || skipSent_ || abortSent_)
        return;
    skipSent_ = true;
    revealTimer_->stop();
    skip_->setEnabled(false);
    skip_->setText(QStringLiteral("Открываю…"));
    emit skipRequested();
}

void StartupDialog::dismiss()
{
    dismissed_ = true;
    revealTimer_->stop();
    close();
}

void StartupDialog::requestAbort()
{
    if (dismissed_ || abortSent_)
        return;
    abortSent_ = true;
    revealTimer_->stop();
    emit abortRequested();
}

void StartupDialog::reject()
{
    requestAbort();
    QDialog::reject();
}

void StartupDialog::closeEvent(QCloseEvent* event)
{
    requestAbort();
    event->accept();
}

void StartupDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (screen() != nullptr)
    {
        auto frame = frameGeometry();
        frame.moveCenter(screen()->availableGeometry().center());
        move(frame.topLeft());
    }
}

} // namespace orion::app
