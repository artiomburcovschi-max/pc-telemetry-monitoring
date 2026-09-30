#include "network_traffic_chart.h"

#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace orion::app {
namespace {
double checkedRate(double value)
{
    return std::isfinite(value) && value >= 0 ? value : std::numeric_limits<double>::quiet_NaN();
}

QString rateText(double value)
{
    if (!std::isfinite(value)) return QStringLiteral("н/д");
    const bool large = value >= 1024.0 * 1024.0;
    return QStringLiteral("%1 %2").arg(value / (large ? 1048576.0 : 1024.0), 0, 'f', 3)
        .arg(large ? QStringLiteral("МиБ/с") : QStringLiteral("КиБ/с"));
}
} // namespace

NetworkTrafficChart::NetworkTrafficChart(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("NetworkTrafficChart"));
    setMinimumSize(260, 260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("История приёма и передачи сетевого трафика"));
    setToolTip(QStringLiteral("Суммарный трафик компьютера, не тест скорости Интернета. "
        "Наведите курсор на линию для просмотра замера. Пропуски и пауза разрывают линию. "
        "1 КиБ = 1024 байта; 1 МиБ = 1048576 байт."));
}

void NetworkTrafficChart::appendSample(qint64 elapsedMs, double download, double upload)
{
    // Reject queued paused deliveries and duplicate/out-of-order timestamps.
    if (paused_ || elapsedMs < 0 || (!samples_.isEmpty() && elapsedMs <= samples_.back().elapsedMs)) return;
    const bool gap = breakPending_ || (!samples_.isEmpty()
        && elapsedMs - samples_.back().elapsedMs > 2500);
    samples_.append({elapsedMs, checkedRate(download), checkedRate(upload), gap});
    breakPending_ = false;
    while (!samples_.isEmpty() && (elapsedMs - samples_.front().elapsedMs > historyDurationMs
        || samples_.size() > historyPointLimit)) samples_.removeFirst();
    updateHover();
    update();
}

void NetworkTrafficChart::setMonitoringPaused(bool paused)
{
    if (paused && !paused_) breakPending_ = true;
    paused_ = paused;
    update();
}

void NetworkTrafficChart::setThemeColors(const QColor& background, const QColor& panel,
    const QColor& text, const QColor& muted, const QColor& accent, const QColor& border, bool quantum)
{
    background_ = background;
    panel_ = panel;
    text_ = text;
    muted_ = muted;
    accent_ = accent;
    border_ = border;
    quantum_ = quantum;
    download_ = quantum ? accent : QColor(QStringLiteral("#2ECC71"));
    update();
}

QRectF NetworkTrafficChart::plotArea() const
{
    return QRectF(62, 76, std::max(1, width() - 78), std::max(1, height() - 120));
}

double NetworkTrafficChart::unitDivisor() const
{
    for (const auto& sample : samples_) {
        if (sample.downloadBytesPerSecond >= 1048576.0 || sample.uploadBytesPerSecond >= 1048576.0)
            return 1048576.0;
    }
    return 1024.0;
}

double NetworkTrafficChart::axisMaximum() const
{
    double high = 0;
    const double divisor = unitDivisor();
    for (const auto& sample : samples_) {
        for (double value : {sample.downloadBytesPerSecond, sample.uploadBytesPerSecond}) {
            if (std::isfinite(value)) high = std::max(high, value / divisor);
        }
    }
    high = std::max(1.0, high * 1.1);
    const double power = std::pow(10.0, std::floor(std::log10(high)));
    const double fraction = high / power;
    return (fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10) * power;
}

QPointF NetworkTrafficChart::samplePosition(int index, bool download) const
{
    const auto area = plotArea();
    const auto& sample = samples_.at(index);
    const double age = static_cast<double>(samples_.back().elapsedMs - sample.elapsedMs);
    const double value = download ? sample.downloadBytesPerSecond : sample.uploadBytesPerSecond;
    return {area.right() - age / historyDurationMs * area.width(),
        area.bottom() - value / unitDivisor() / axisMaximum() * area.height()};
}

QVector<QPolygonF> NetworkTrafficChart::lineSegments(bool download) const
{
    QVector<QPolygonF> segments;
    QPolygonF current;
    for (int index = 0; index < sampleCount(); ++index) {
        const auto& sample = samples_.at(index);
        const double value = download ? sample.downloadBytesPerSecond : sample.uploadBytesPerSecond;
        if ((!std::isfinite(value) || sample.breakBefore) && !current.isEmpty()) {
            segments.append(current);
            current.clear();
        }
        if (std::isfinite(value)) current.append(samplePosition(index, download));
    }
    if (!current.isEmpty()) segments.append(current);
    return segments;
}

QString NetworkTrafficChart::hoverText() const
{
    if (hoveredIndex_ < 0 || hoveredIndex_ >= sampleCount()) return {};
    const auto& sample = samples_.at(hoveredIndex_);
    return QStringLiteral("%1 с до последнего замера\nПриём: %2\nПередача: %3")
        .arg((samples_.back().elapsedMs - sample.elapsedMs) / 1000.0, 0, 'f', 1)
        .arg(rateText(sample.downloadBytesPerSecond), rateText(sample.uploadBytesPerSecond));
}

void NetworkTrafficChart::updateHover()
{
    hoveredIndex_ = -1;
    const auto area = plotArea();
    if (!pointerInside_ || samples_.isEmpty() || !area.contains(pointer_)) return;
    const double target = static_cast<double>(samples_.back().elapsedMs)
        - (area.right() - pointer_.x()) / area.width() * historyDurationMs;
    double distance = 1500.0;
    for (int index = 0; index < sampleCount(); ++index) {
        const double candidate = std::abs(static_cast<double>(samples_.at(index).elapsedMs) - target);
        if (candidate <= distance) { hoveredIndex_ = index; distance = candidate; }
    }
}

void NetworkTrafficChart::mouseMoveEvent(QMouseEvent* event)
{
    pointerInside_ = true;
    pointer_ = event->position();
    updateHover();
    update();
}

void NetworkTrafficChart::leaveEvent(QEvent* event)
{
    pointerInside_ = false;
    updateHover();
    update();
    QWidget::leaveEvent(event);
}

void NetworkTrafficChart::hideEvent(QHideEvent* event)
{
    pointerInside_ = false;
    updateHover();
    QWidget::hideEvent(event);
}

void NetworkTrafficChart::resizeEvent(QResizeEvent* event)
{
    updateHover();
    QWidget::resizeEvent(event);
}

void NetworkTrafficChart::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), panel_);
    painter.setPen(border_);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    QFont small = font();
    small.setPixelSize(11);
    painter.setFont(small);
    const auto area = plotArea();
    painter.setPen(text_);
    const QString unit = unitDivisor() == 1024.0 ? QStringLiteral("КиБ/с") : QStringLiteral("МиБ/с");
    painter.drawText(QRectF(12, 8, width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("Трафик · %1%2").arg(unit, paused_ ? QStringLiteral(" · ПАУЗА") : QString()));
    painter.setPen(muted_);
    painter.drawText(QRectF(12, 29, width() - 24, 17), Qt::AlignLeft | Qt::AlignVCenter,
        painter.fontMetrics().elidedText(QStringLiteral("Все адаптеры · наведите курсор на график"),
            Qt::ElideRight, width() - 24));
    painter.setPen(download_);
    painter.drawLine(QPointF(12, 58), QPointF(28, 58));
    painter.drawText(QRectF(34, 49, 80, 18), Qt::AlignVCenter, QStringLiteral("Приём"));
    painter.setPen(upload_);
    painter.drawLine(QPointF(116, 58), QPointF(132, 58));
    painter.drawText(QRectF(138, 49, 85, 18), Qt::AlignVCenter, QStringLiteral("Передача"));
    painter.fillRect(area, background_);

    const int divisions = quantum_ ? 8 : 4;
    const double maximum = axisMaximum();
    for (int division = 0; division <= divisions; ++division) {
        const double fraction = static_cast<double>(division) / divisions;
        const double y = area.bottom() - fraction * area.height();
        const double x = area.left() + fraction * area.width();
        painter.setPen(QPen(border_, 1, Qt::DotLine));
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        if (quantum_ && division % 2 != 0) continue;
        painter.setPen(muted_);
        const double tick = maximum * fraction;
        const int decimals = std::floor(tick) == tick ? 0 : std::floor(tick * 10) == tick * 10 ? 1 : 2;
        const QString yLabel = maximum >= 10000 ? QString::number(tick, 'g', 2)
            : QString::number(tick, 'f', decimals);
        painter.drawText(QRectF(1, y - 8, 54, 16), Qt::AlignRight | Qt::AlignVCenter, yLabel);
        painter.drawText(QRectF(x - 16, area.bottom() + 4, 32, 16), Qt::AlignCenter,
            QString::number(qRound((1 - fraction) * 60)));
    }
    painter.setPen(muted_);
    painter.drawText(QRectF(area.left(), area.bottom() + 22, area.width(), 17), Qt::AlignCenter,
        QStringLiteral("секунд до последнего замера"));

    painter.save();
    painter.setClipRect(area.adjusted(-3, -3, 3, 3));
    for (bool download : {true, false}) {
        const QColor color = download ? download_ : upload_;
        painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(color);
        for (const auto& segment : lineSegments(download)) {
            if (segment.size() == 1) painter.drawEllipse(segment.front(), 2.2, 2.2);
            else painter.drawPolyline(segment);
        }
    }
    painter.restore();
    if (samples_.isEmpty()) {
        painter.setPen(text_);
        painter.drawText(area, Qt::AlignCenter, QStringLiteral("Ожидание первого замера…"));
    } else if (lineSegments(true).isEmpty() && lineSegments(false).isEmpty()) {
        painter.setPen(text_);
        painter.drawText(area, Qt::AlignCenter, QStringLiteral("Трафик недоступен"));
    }
    if (hoveredIndex_ < 0) return;
    const double x = samplePosition(hoveredIndex_, true).x();
    painter.setPen(QPen(quantum_ ? accent_ : muted_, 1, Qt::DashLine));
    painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    for (bool download : {true, false}) {
        const auto point = samplePosition(hoveredIndex_, download);
        if (!std::isfinite(point.y())) continue;
        painter.setPen(panel_);
        painter.setBrush(download ? download_ : upload_);
        painter.drawEllipse(point, 4, 4);
    }
    // In-widget tooltip is kept within the canvas, including at both edges.
    const QStringList lines = hoverText().split(u'\n');
    int textWidth = 0;
    for (const auto& line : lines) textWidth = std::max(textWidth, painter.fontMetrics().horizontalAdvance(line));
    const double boxWidth = std::min(width() - 16, textWidth + 20);
    const double boxX = std::clamp(x + 12, 8.0, width() - boxWidth - 8.0);
    const QRectF box(boxX, area.top() + 8, boxWidth, 65);
    painter.setPen(border_);
    painter.setBrush(panel_);
    painter.drawRoundedRect(box, 5, 5);
    for (int index = 0; index < lines.size(); ++index) {
        painter.setPen(index == 0 ? text_ : index == 1 ? download_ : upload_);
        painter.drawText(box.adjusted(8, 5 + index * 18, -8, 0), Qt::AlignLeft | Qt::AlignTop,
            painter.fontMetrics().elidedText(lines.at(index), Qt::ElideRight, static_cast<int>(boxWidth) - 16));
    }
}

} // namespace orion::app
