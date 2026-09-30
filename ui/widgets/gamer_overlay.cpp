#include "gamer_overlay.h"

#include <QAbstractAnimation>
#include <QCloseEvent>
#include <QColor>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QShortcut>
#include <QShowEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

namespace orion::app {

GamerOverlay::GamerOverlay(QString pingTarget, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , pingTarget_(std::move(pingTarget).trimmed())
{
    if (pingTarget_.isEmpty()) pingTarget_ = QStringLiteral("8.8.8.8");
    setObjectName(QStringLiteral("GamerOverlay"));
    setAttribute(Qt::WA_TranslucentBackground, false);
    setWindowOpacity(0.88);
    setFixedSize(280, 250);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral(
        "FPS — частота отрисовки интерфейса O.R.I.O.N., не FPS игры. "
        "Оверлей поверх Exclusive Fullscreen технически не гарантируется."));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(7);
    auto* titleRow = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("O.R.I.O.N."), this);
    title->setObjectName(QStringLiteral("GamerOverlayTitle"));
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    auto* closeButton = new QPushButton(QStringLiteral("×"), this);
    closeButton->setObjectName(QStringLiteral("GamerOverlayClose"));
    closeButton->setFixedSize(24, 24);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    titleRow->addWidget(closeButton);
    layout->addLayout(titleRow);

    const auto addMetric = [this, layout](const QString& name, const QString& initial) {
        auto* label = new QLabel(initial, this);
        label->setObjectName(name);
        layout->addWidget(label);
        return label;
    };
    cpuLabel_ = addMetric(QStringLiteral("GamerCpu"), QStringLiteral("CPU: —"));
    gpuLabel_ = addMetric(QStringLiteral("GamerGpu"), QStringLiteral("GPU: —"));
    ramLabel_ = addMetric(QStringLiteral("GamerRam"), QStringLiteral("RAM: —"));
    networkLabel_ = addMetric(QStringLiteral("GamerNetwork"), QStringLiteral("NET: —"));
    pingLabel_ = addMetric(QStringLiteral("GamerPing"), QStringLiteral("PING: ожидание…"));
    fpsLabel_ = addMetric(QStringLiteral("GamerFps"), QStringLiteral("FPS: — (UI)"));
    fpsLabel_->setToolTip(QStringLiteral(
        "Честная частота paintEvent этого оверлея; не измеряет кадры сторонней игры."));
    layout->addStretch(1);

    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &QWidget::close);

    pulseAnimation_ = new QVariantAnimation(this);
    pulseAnimation_->setStartValue(0.0);
    pulseAnimation_->setKeyValueAt(0.5, 1.0);
    pulseAnimation_->setEndValue(0.0);
    pulseAnimation_->setDuration(1200);
    pulseAnimation_->setLoopCount(-1);
    pulseAnimation_->setEasingCurve(QEasingCurve::InOutSine);
    connect(pulseAnimation_, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) {
            const QColor base(accentColor_);
            const QColor alarm(QStringLiteral("#FF3B30"));
            const qreal mix = value.toReal();
            rebuildStyle(QColor(
                static_cast<int>(base.red() + (alarm.red() - base.red()) * mix),
                static_cast<int>(base.green() + (alarm.green() - base.green()) * mix),
                static_cast<int>(base.blue() + (alarm.blue() - base.blue()) * mix)));
        });

    renderTimer_ = new QTimer(this);
    renderTimer_->setInterval(16);
    connect(renderTimer_, &QTimer::timeout, this, [this] {
        if (paused_ || shuttingDown_) return;
        update();
        if (++fpsDisplayTicks_ >= 30) {
            fpsDisplayTicks_ = 0;
            const QString text = QStringLiteral("FPS: %1 (UI)")
                .arg(fpsCounter_.fps(), 0, 'f', 0);
            if (fpsLabel_->text() != text) fpsLabel_->setText(text);
        }
    });

    keepOnTopTimer_ = new QTimer(this);
    keepOnTopTimer_->setInterval(3000);
    connect(keepOnTopTimer_, &QTimer::timeout, this, [this] {
        if (isVisible() && !paused_) raise();
    });

    pingWorker_ = new PingWorker(pingTarget_, 53, 1000, 2000, this);
    pingWorker_->setPaused(true);
    connect(pingWorker_, &PingWorker::sampleReady, this, &GamerOverlay::applyPing);
    pingWorker_->start();
    rebuildStyle(QColor(accentColor_));
}

GamerOverlay::~GamerOverlay()
{
    shutdown();
}

QString GamerOverlay::formatPercent(const double value) const
{
    return std::isfinite(value) && value >= 0.0
        ? QStringLiteral("%1%").arg(value, 0, 'f', 0)
        : QStringLiteral("н/д");
}

void GamerOverlay::updateTelemetry(
    const double cpuPercent,
    const double gpuPercent,
    const double ramPercent,
    const double downloadBytesPerSecond,
    const double uploadBytesPerSecond)
{
    if (paused_ || shuttingDown_) return;
    cpuLabel_->setText(QStringLiteral("CPU: %1").arg(formatPercent(cpuPercent)));
    gpuLabel_->setText(QStringLiteral("GPU: %1").arg(formatPercent(gpuPercent)));
    ramLabel_->setText(QStringLiteral("RAM: %1").arg(formatPercent(ramPercent)));
    const auto rate = [](const double bytes) {
        return std::isfinite(bytes) && bytes >= 0.0
            ? QString::number(bytes / (1024.0 * 1024.0), 'f', 1)
            : QStringLiteral("н/д");
    };
    networkLabel_->setText(QStringLiteral("NET: ↓%1  ↑%2 МБ/с")
        .arg(rate(downloadBytesPerSecond), rate(uploadBytesPerSecond)));
}

void GamerOverlay::setPingTarget(const QString& suppliedTarget)
{
    QString target = suppliedTarget.trimmed();
    if (target.isEmpty()) target = QStringLiteral("8.8.8.8");
    pingTarget_ = target;
    pingLabel_->setText(QStringLiteral("PING: ожидание… (%1)").arg(pingTarget_));
    if (pingWorker_ != nullptr) pingWorker_->setTarget(pingTarget_);
}

void GamerOverlay::applyPing(const PingTelemetry& sample)
{
    if (paused_ || shuttingDown_ || !isVisible()) return;
    pingLabel_->setText(sample.usable()
        ? QStringLiteral("PING: %1 мс (%2)").arg(sample.latencyMs, 0, 'f', 0).arg(sample.target)
        : QStringLiteral("PING: н/д (нет соединения)"));
}

void GamerOverlay::setMonitoringPaused(const bool paused)
{
    if (paused_ == paused) {
        syncRuntimeState();
        return;
    }
    paused_ = paused;
    if (paused_) {
        pulseAnimation_->stop();
        fpsLabel_->setText(QStringLiteral("FPS: пауза"));
        pingLabel_->setText(QStringLiteral("PING: пауза"));
    } else {
        fpsCounter_.reset();
        fpsDisplayTicks_ = 0;
        pingLabel_->setText(QStringLiteral("PING: ожидание… (%1)").arg(pingTarget_));
    }
    syncRuntimeState();
}

void GamerOverlay::setAlarmState(const bool active)
{
    alarmActive_ = active;
    if (alarmActive_ && isVisible() && !paused_ && !shuttingDown_) {
        if (pulseAnimation_->state() != QAbstractAnimation::Running) {
            pulseAnimation_->start();
        }
    } else {
        pulseAnimation_->stop();
        rebuildStyle(QColor(accentColor_));
    }
}

void GamerOverlay::updateMetricStyle(QLabel* label, const bool critical)
{
    label->setStyleSheet(QStringLiteral(
        "font-size: 14px; font-weight: 700; color: %1; background: transparent; border: none;")
        .arg(critical ? QStringLiteral("#FF3B30") : textColor_));
}

void GamerOverlay::setCriticalMetrics(
    const bool cpuCritical,
    const bool gpuCritical,
    const bool ramCritical)
{
    cpuCritical_ = cpuCritical;
    gpuCritical_ = gpuCritical;
    ramCritical_ = ramCritical;
    updateMetricStyle(cpuLabel_, cpuCritical_);
    updateMetricStyle(gpuLabel_, gpuCritical_);
    updateMetricStyle(ramLabel_, ramCritical_);
}

void GamerOverlay::applyTheme(
    const QString& panel,
    const QString& text,
    const QString& accent,
    const QString& secondary,
    const QString& border,
    const QString& font)
{
    panelColor_ = panel;
    textColor_ = text;
    accentColor_ = accent;
    secondaryColor_ = secondary;
    borderColor_ = border;
    fontFamily_ = font;
    rebuildStyle(QColor(accentColor_));
    setCriticalMetrics(cpuCritical_, gpuCritical_, ramCritical_);
}

void GamerOverlay::rebuildStyle(const QColor& borderColor)
{
    setStyleSheet(QStringLiteral(R"(
        QWidget#GamerOverlay {
            background: %1;
            color: %2;
            border: 2px solid %3;
            border-radius: 10px;
            font-family: %4;
        }
        QLabel#GamerOverlayTitle {
            color: %2;
            background: transparent;
            border: none;
            font-size: 17px;
            font-weight: 800;
        }
        QLabel { color: %2; background: transparent; border: none; }
        QPushButton#GamerOverlayClose {
            background: transparent;
            color: %2;
            border: 1px solid %5;
            border-radius: 4px;
            padding: 0px;
            font-weight: 800;
        }
        QPushButton#GamerOverlayClose:hover { background: %6; color: %1; }
    )").arg(panelColor_, textColor_, borderColor.name(), fontFamily_,
             borderColor_, secondaryColor_));
}

void GamerOverlay::syncRuntimeState()
{
    const bool active = isVisible() && !paused_ && !shuttingDown_;
    if (pingWorker_ != nullptr) pingWorker_->setPaused(!active);
    if (active) {
        if (!renderTimer_->isActive()) renderTimer_->start();
        if (!keepOnTopTimer_->isActive()) keepOnTopTimer_->start();
        if (alarmActive_ && pulseAnimation_->state() != QAbstractAnimation::Running) {
            pulseAnimation_->start();
        }
    } else {
        renderTimer_->stop();
        keepOnTopTimer_->stop();
        pulseAnimation_->stop();
    }
}

void GamerOverlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    fpsCounter_.reset();
    fpsDisplayTicks_ = 0;
    syncRuntimeState();
}

void GamerOverlay::hideEvent(QHideEvent* event)
{
    renderTimer_->stop();
    keepOnTopTimer_->stop();
    pulseAnimation_->stop();
    if (pingWorker_ != nullptr) pingWorker_->setPaused(true);
    QWidget::hideEvent(event);
}

void GamerOverlay::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    if (paused_ || shuttingDown_) return;
    (void)fpsCounter_.tick();
}

void GamerOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void GamerOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_ && event->buttons().testFlag(Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - dragOffset_);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void GamerOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    dragging_ = false;
    QWidget::mouseReleaseEvent(event);
}

void GamerOverlay::closeEvent(QCloseEvent* event)
{
    const bool userClose = !shuttingDown_;
    shutdown();
    event->accept();
    if (userClose) emit closedByUser();
}

void GamerOverlay::shutdown()
{
    if (shuttingDown_) return;
    shuttingDown_ = true;
    renderTimer_->stop();
    keepOnTopTimer_->stop();
    pulseAnimation_->stop();
    if (pingWorker_ != nullptr) {
        pingWorker_->stop();
        if (!pingWorker_->wait(2000)) {
            qWarning("Gamer overlay ping worker did not stop within two seconds");
        }
    }
}

bool GamerOverlay::monitoringPaused() const noexcept
{
    return paused_;
}

bool GamerOverlay::alarmActive() const noexcept
{
    return alarmActive_;
}

bool GamerOverlay::pingWorkerRunning() const
{
    return pingWorker_ != nullptr && pingWorker_->isRunning();
}

} // namespace orion::app
