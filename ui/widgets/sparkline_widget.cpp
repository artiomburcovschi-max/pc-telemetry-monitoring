#include "sparkline_widget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace orion::app {

SparklineWidget::SparklineWidget(
    const double minimum,
    const double maximum,
    const bool automaticRange,
    QWidget* parent)
    : QWidget(parent)
    , minimum_(minimum)
    , maximum_(maximum)
    , automaticRange_(automaticRange)
{
    setMinimumHeight(30);
    setMaximumHeight(40);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void SparklineWidget::pushValue(const double value)
{
    if (!std::isfinite(value)) return;
    values_.append(value);
    constexpr qsizetype maximumPoints = 60;
    if (values_.size() > maximumPoints) {
        values_.remove(0, values_.size() - maximumPoints);
    }
    update();
}

void SparklineWidget::clear()
{
    values_.clear();
    update();
}

void SparklineWidget::setAccentColor(const QColor& color)
{
    accent_ = color;
    update();
}

QSize SparklineWidget::sizeHint() const
{
    return {220, 40};
}

void SparklineWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (values_.isEmpty()) {
        return;
    }

    double low = minimum_;
    double high = maximum_;
    if (automaticRange_) {
        const auto [minimumIt, maximumIt] = std::minmax_element(values_.cbegin(), values_.cend());
        low = std::min(0.0, *minimumIt);
        high = std::max(*maximumIt, low + 1.0);
    }
    const double range = std::max(high - low, 1.0);
    const double step = values_.size() > 1
        ? static_cast<double>(width() - 2) / static_cast<double>(values_.size() - 1)
        : 0.0;

    QPainterPath path;
    for (qsizetype index = 0; index < values_.size(); ++index) {
        const double normalized = std::clamp((values_[index] - low) / range, 0.0, 1.0);
        const QPointF point(
            1.0 + static_cast<double>(index) * step,
            1.0 + (1.0 - normalized) * static_cast<double>(height() - 3));
        if (index == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
    }
    painter.setPen(QPen(accent_, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
}

} // namespace orion::app
