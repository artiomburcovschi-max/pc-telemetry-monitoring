#include "gauge_widget.h"

#include <QEasingCurve>
#include <QFont>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPropertyAnimation>

#include <algorithm>
#include <utility>

namespace orion::app {

GaugeWidget::GaugeWidget(QString label, QString unit, QWidget* parent)
    : QWidget(parent)
    , label_(std::move(label))
    , unit_(std::move(unit))
{
    setObjectName(QStringLiteral("MetricGauge"));
    setMinimumSize(120, 120);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    animation_ = new QPropertyAnimation(this, "value", this);
    animation_->setDuration(400);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
}

double GaugeWidget::value() const noexcept
{
    return value_;
}

double GaugeWidget::targetValue() const noexcept
{
    return targetValue_;
}

bool GaugeWidget::available() const noexcept
{
    return available_;
}

void GaugeWidget::setValue(const double value)
{
    value_ = std::clamp(value, 0.0, 100.0);
    update();
}

void GaugeWidget::setTargetValue(const double value, const QString& unit)
{
    if (value < 0.0) {
        setUnavailable();
        return;
    }
    if (!unit.isEmpty()) unit_ = unit;
    available_ = true;
    targetValue_ = std::clamp(value, 0.0, 100.0);
    animation_->stop();
    animation_->setStartValue(value_);
    animation_->setEndValue(targetValue_);
    animation_->start();
}

void GaugeWidget::setUnavailable(const bool unavailable)
{
    available_ = !unavailable;
    if (unavailable) {
        animation_->stop();
        targetValue_ = 0.0;
        value_ = 0.0;
    }
    update();
}

void GaugeWidget::setLabel(const QString& label)
{
    label_ = label;
    update();
}

void GaugeWidget::setColors(
    const QColor& accent,
    const QColor& track,
    const QColor& text)
{
    accent_ = accent;
    track_ = track;
    textColor_ = text;
    update();
}

QSize GaugeWidget::sizeHint() const
{
    return {132, 132};
}

void GaugeWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int side = std::min(width(), height());
    const QRectF arcRect(
        (width() - side) / 2.0 + 11.0,
        (height() - side) / 2.0 + 11.0,
        side - 22.0,
        side - 22.0);
    constexpr int startAngle = 225 * 16;
    constexpr int totalSpan = -270 * 16;

    painter.setPen(QPen(track_, 8.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(arcRect, startAngle, totalSpan);

    if (available_) {
        QColor glow = accent_;
        glow.setAlpha(70);
        painter.setPen(QPen(glow, 13.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arcRect, startAngle,
            static_cast<int>(totalSpan * value_ / 100.0));
        painter.setPen(QPen(accent_, 8.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arcRect, startAngle,
            static_cast<int>(totalSpan * value_ / 100.0));
    }

    painter.setPen(textColor_);
    QFont valueFont(font());
    valueFont.setPointSize(std::max(10, side / 8));
    valueFont.setBold(true);
    painter.setFont(valueFont);
    painter.drawText(arcRect, Qt::AlignCenter,
        available_ ? QStringLiteral("%1%2").arg(value_, 0, 'f', 0).arg(unit_)
                   : QStringLiteral("н/д"));

    QFont labelFont(font());
    labelFont.setPointSize(std::max(8, side / 15));
    labelFont.setBold(false);
    painter.setFont(labelFont);
    const QRectF labelRect(arcRect.x(), arcRect.bottom() - 8.0,
        arcRect.width(), 22.0);
    painter.drawText(labelRect, Qt::AlignCenter, label_);
}

} // namespace orion::app
