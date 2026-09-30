#include "app_monitor_chart.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>

namespace orion::app {
namespace {
double measurement(const QJsonValue& value)
{
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= 0
        ? value.toDouble() : std::numeric_limits<double>::quiet_NaN();
}
QString number(double value)
{
    return std::isfinite(value) ? QString::number(value, 'f', 1) : QStringLiteral("н/д");
}
}

AppMonitorChart::AppMonitorChart(Mode mode, QWidget* parent) : QWidget(parent), mode_(mode)
{
    setMinimumSize(260, 210);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip(QStringLiteral("Время активного наблюдения. Разрывы — пауза или недоступные данные. CPU дерева может превышать 100%. История — до 6000 замеров."));
}

void AppMonitorChart::appendSample(const QJsonObject& sample)
{
    if (paused_) return;
    const double elapsed = measurement(sample.value("elapsed"));
    if (!std::isfinite(elapsed) || (!points_.empty() && elapsed <= points_.back().elapsed)) return;
    const double interval = measurement(sample.value("sample_interval"));
    const bool gap = !points_.empty() && elapsed - points_.back().elapsed
        > 2.5 * (std::isfinite(interval) && interval > 0 ? interval : 1.0);
    points_.push_back({elapsed, measurement(sample.value(mode_ == Mode::Cpu ? "cpu_percent" : "ram_mb")),
        measurement(sample.value("private_mb")), breakPending_ || gap});
    if (points_.size() > 6000) points_.pop_front();
    breakPending_ = false;
    updateHover();
    update();
}

void AppMonitorChart::clear()
{
    points_.clear();
    breakPending_ = false;
    hoverIndex_ = -1;
    update();
}

void AppMonitorChart::setMonitoringPaused(bool paused)
{
    if (paused != paused_) breakPending_ = true;
    paused_ = paused;
    update();
}

void AppMonitorChart::setThemeColors(QColor panel, QColor text, QColor muted, QColor border)
{
    panel_ = panel; text_ = text; muted_ = muted; border_ = border;
    update();
}

double AppMonitorChart::axisMaximum() const
{
    double peak = mode_ == Mode::Cpu ? 100.0 : 1.0;
    for (const auto& point : points_) {
        if (std::isfinite(point.first)) peak = std::max(peak, point.first);
        if (mode_ == Mode::Memory && std::isfinite(point.second)) peak = std::max(peak, point.second);
    }
    if (mode_ == Mode::Cpu && peak <= 100) return 100;
    const double padded = peak * 1.1;
    if (!std::isfinite(padded)) return peak;
    if (mode_ == Mode::Cpu) return std::ceil(padded / 25.0) * 25.0;
    const double scale = std::pow(10.0, std::floor(std::log10(padded)));
    const double fraction = padded / scale;
    const double result = (fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10) * scale;
    return std::isfinite(result) ? result : peak;
}

QRectF AppMonitorChart::plotArea() const
{
    return QRectF(62, 57, std::max(1, width() - 82), std::max(1, height() - 94));
}

QPointF AppMonitorChart::position(const Point& point, double value, double maximum) const
{
    const auto area = plotArea();
    const double span = std::max(1.0, points_.back().elapsed - points_.front().elapsed);
    return {area.left() + (point.elapsed - points_.front().elapsed) / span * area.width(),
        area.bottom() - value / maximum * area.height()};
}

QVector<QPolygonF> AppMonitorChart::segments(int series, double maximum) const
{
    QVector<QPolygonF> result;
    QPolygonF current;
    for (const auto& point : points_) {
        const double value = series == 0 ? point.first : point.second;
        if (point.breakBefore || !std::isfinite(value)) {
            if (!current.isEmpty()) result.append(current);
            current.clear();
        }
        if (std::isfinite(value)) current.append(position(point, value, maximum));
    }
    if (!current.isEmpty()) result.append(current);
    return result;
}

QVector<QPolygonF> AppMonitorChart::lineSegments(int series) const
{
    if (series < 0 || series > (mode_ == Mode::Cpu ? 0 : 1)) return {};
    return segments(series, axisMaximum());
}

void AppMonitorChart::updateHover()
{
    hoverIndex_ = -1;
    if (!mouseInside_ || points_.empty() || !plotArea().contains(mouse_)) return;
    const double elapsed = points_.front().elapsed + (mouse_.x() - plotArea().left()) / plotArea().width()
        * std::max(1.0, points_.back().elapsed - points_.front().elapsed);
    auto it = std::lower_bound(points_.begin(), points_.end(), elapsed,
        [](const Point& point, double time) { return point.elapsed < time; });
    if (it == points_.end()) --it;
    else if (it != points_.begin() && elapsed - (it - 1)->elapsed <= it->elapsed - elapsed) --it;
    hoverIndex_ = static_cast<int>(it - points_.begin());
}

QString AppMonitorChart::hoverText() const
{
    if (hoverIndex_ < 0 || hoverIndex_ >= sampleCount()) return {};
    const auto& point = points_[hoverIndex_];
    const QString time = QStringLiteral("%1 с").arg(number(point.elapsed));
    return mode_ == Mode::Cpu ? QStringLiteral("%1 · CPU %2% ").arg(time, number(point.first))
        : QStringLiteral("%1\nWS %2 МБ\nPrivate %3 МБ").arg(time, number(point.first), number(point.second));
}

void AppMonitorChart::mouseMoveEvent(QMouseEvent* event)
{
    mouse_ = event->position(); mouseInside_ = true; updateHover(); update();
}
void AppMonitorChart::leaveEvent(QEvent*) { mouseInside_ = false; hoverIndex_ = -1; update(); }
void AppMonitorChart::hideEvent(QHideEvent*) { mouseInside_ = false; hoverIndex_ = -1; }

void AppMonitorChart::paintEvent(QPaintEvent*)
{
    updateHover();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(panel_); painter.setPen(border_);
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
    auto font = painter.font(); font.setPixelSize(12); font.setBold(true); painter.setFont(font);
    painter.setPen(text_);
    painter.drawText(QRectF(14, 9, width() - 28, 22), Qt::AlignLeft | Qt::AlignVCenter,
        mode_ == Mode::Cpu ? QStringLiteral("CPU дерева, %") : QStringLiteral("Память дерева, МБ"));
    font.setBold(false); font.setPixelSize(11); painter.setFont(font);
    const QColor firstColor(mode_ == Mode::Cpu ? "#3498db" : "#e67e22"), secondColor("#9b59b6");
    painter.setPen(firstColor);
    painter.drawText(14, 44, mode_ == Mode::Cpu ? QStringLiteral("● CPU · сумма процессов") : QStringLiteral("● Working set"));
    if (mode_ == Mode::Memory) { painter.setPen(secondColor); painter.drawText(150, 44, QStringLiteral("● Private")); }
    const auto area = plotArea();
    const double maximum = axisMaximum();
    for (int tick = 0; tick <= 4; ++tick) {
        const double y = area.bottom() - tick * area.height() / 4;
        painter.setPen(QPen(border_, 1, Qt::DotLine)); painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.setPen(muted_);
        painter.drawText(QRectF(3, y - 8, 52, 16), Qt::AlignRight | Qt::AlignVCenter,
            QString::number(maximum * tick / 4, 'g', 4));
        const double elapsed = points_.empty() ? tick * 15.0
            : points_.front().elapsed + tick * std::max(1.0, points_.back().elapsed - points_.front().elapsed) / 4;
        const int timeDigits = !points_.empty() && points_.back().elapsed - points_.front().elapsed < 4.0 ? 1 : 0;
        painter.drawText(QRectF(area.left() + tick * area.width() / 4 - 33, area.bottom() + 7, 66, 18),
            Qt::AlignCenter, QStringLiteral("%1 с").arg(elapsed, 0, 'f', timeDigits));
    }
    painter.save(); painter.setClipRect(area.adjusted(-2, -2, 2, 2));
    // Linear work over at most 6000 points; never rescan the history per vertex.
    for (int series = 0; series < (mode_ == Mode::Cpu ? 1 : 2); ++series) {
        painter.setPen(QPen(series == 0 ? firstColor : secondColor, 1.7));
        for (const auto& segment : segments(series, maximum)) {
            if (segment.size() == 1) painter.drawEllipse(segment.front(), 2, 2);
            else painter.drawPolyline(segment);
        }
    }
    if (hoverIndex_ >= 0) {
        const auto& point = points_[hoverIndex_];
        const double x = position(point, 0, maximum).x();
        painter.setPen(QPen(muted_, 1, Qt::DashLine));
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        painter.drawLine(QPointF(area.left(), mouse_.y()), QPointF(area.right(), mouse_.y()));
    }
    painter.restore();
    if (points_.empty()) {
        painter.setPen(muted_); painter.drawText(area, Qt::AlignCenter, QStringLiteral("Ожидание замеров"));
    }
    if (hoverIndex_ >= 0) {
        const auto message = hoverText();
        const auto bounds = painter.fontMetrics().boundingRect(QRect(0, 0, 1000, 1000), Qt::AlignLeft, message);
        const double boxWidth = std::min(width() - 8.0, bounds.width() + 20.0);
        const double boxHeight = bounds.height() + 14.0;
        QRectF box(std::clamp(mouse_.x() + 14, 4.0, width() - boxWidth - 4),
            std::clamp(mouse_.y() + 14, 4.0, height() - boxHeight - 4), boxWidth, boxHeight);
        painter.setBrush(panel_); painter.setPen(border_); painter.drawRoundedRect(box, 5, 5);
        painter.setPen(text_); painter.drawText(box.adjusted(10, 7, -10, -7), Qt::AlignLeft, message);
    }
}
} // namespace orion::app
