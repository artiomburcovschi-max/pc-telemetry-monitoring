#include "metric_card.h"

#include "gauge_widget.h"
#include "sparkline_widget.h"

#include <QColor>
#include <QLabel>
#include <QHBoxLayout>
#include <QStyle>
#include <QResizeEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace orion::app {

MetricCard::MetricCard(
    const QString& title,
    const bool automaticHistoryRange,
    QWidget* parent)
    : QFrame(parent)
    , percentageMetric_(!automaticHistoryRange)
{
    setObjectName(QStringLiteral("MetricCard"));
    setFrameShape(QFrame::NoFrame);

    setMinimumHeight(145);
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    stripe_ = new QFrame(this);
    stripe_->setObjectName(QStringLiteral("MetricStatusStripe"));
    stripe_->setFixedWidth(4);
    setStatusColor(QColor(QStringLiteral("#2ECC71")));
    outer->addWidget(stripe_);

    contentLayout_ = new QVBoxLayout;
    contentLayout_->setContentsMargins(12, 10, 14, 10);
    contentLayout_->setSpacing(5);

    auto* titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("MetricCardTitle"));
    titleLabel->setVisible(!title.isEmpty());
    valueLabel_ = new QLabel(QStringLiteral("н/д"), this);
    valueLabel_->setObjectName(QStringLiteral("MetricCardValue"));
    valueLabel_->setWordWrap(true);
    valueLabel_->setTextFormat(Qt::PlainText);
    detailLabel_ = new QLabel(this);
    detailLabel_->setObjectName(QStringLiteral("MetricCardDetail"));
    detailLabel_->setWordWrap(true);
    detailLabel_->setTextFormat(Qt::PlainText);
    denseSummaryLabel_ = new QLabel(this);
    denseSummaryLabel_->setObjectName(QStringLiteral("DenseMetricSummary"));
    denseSummaryLabel_->setTextFormat(Qt::RichText);
    denseSummaryLabel_->setWordWrap(true);
    denseSummaryLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    denseSummaryLabel_->hide();
    sparkline_ = new SparklineWidget(0.0, 100.0, automaticHistoryRange, this);
    sparkline_->setObjectName(QStringLiteral("MetricSparkline"));
    gauge_ = new GaugeWidget(title, QStringLiteral("%"), this);
    gauge_->hide();

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(10);
    body->addWidget(gauge_, 0, Qt::AlignCenter);
    auto* values = new QVBoxLayout;
    values->setContentsMargins(0, 0, 0, 0);
    values->setSpacing(5);
    values->addWidget(valueLabel_);
    values->addWidget(detailLabel_);
    values->addWidget(denseSummaryLabel_, 1);
    values->addWidget(sparkline_, 1);
    body->addLayout(values, 1);

    auto* heading = new QHBoxLayout;
    heading->setContentsMargins(0, 0, 0, 0);
    heading->addWidget(titleLabel);
    heading->addStretch(1);
    badge_ = new QPushButton(this);
    badge_->setObjectName(QStringLiteral("CardBadge"));
    badge_->setToolTip(QStringLiteral("Открыть подробности во вкладке «Детали»"));
    badge_->setCursor(Qt::PointingHandCursor);
    badge_->hide();
    connect(badge_, &QPushButton::clicked, this, &MetricCard::badgeClicked);
    heading->addWidget(badge_);
    contentLayout_->addLayout(heading);
    contentLayout_->addLayout(body, 1);
    outer->addLayout(contentLayout_, 1);
    setProperty("presentationMode", QStringLiteral("standard"));
}

void MetricCard::setPercent(const double value)
{
    if (value < 0.0) {
        setUnavailable();
        return;
    }
    currentValue_ = QStringLiteral("%1%").arg(value, 0, 'f', 1);
    valueLabel_->setText(currentValue_);
    gauge_->setTargetValue(value);
    detailLabel_->setVisible(presentationMode_ != PresentationMode::Dense
        && !detailLabel_->text().isEmpty());
    refreshDenseSummary();
    pushHistory(value);
    setStatusForPercent(value);
}

void MetricCard::setTextValue(const QString& value, const QString& detail)
{
    currentValue_ = value;
    currentDetail_ = detail;
    valueLabel_->setText(value);
    detailLabel_->setText(detail);
    detailLabel_->setVisible(presentationMode_ != PresentationMode::Dense
        && !detail.isEmpty());
    refreshDenseSummary();
}

void MetricCard::setDetailText(const QString& detail)
{
    currentDetail_ = detail;
    detailLabel_->setText(detail);
    detailLabel_->setVisible(presentationMode_ != PresentationMode::Dense
        && !detail.isEmpty());
    refreshDenseSummary();
}

void MetricCard::setUnavailable(const QString& detail)
{
    currentValue_ = QStringLiteral("н/д");
    currentDetail_ = detail.isEmpty()
                              ? QStringLiteral("Источник временно недоступен")
                              : detail;
    valueLabel_->setText(currentValue_);
    detailLabel_->setText(currentDetail_);
    detailLabel_->setVisible(presentationMode_ != PresentationMode::Dense);
    refreshDenseSummary();
    sparkline_->clear();
    gauge_->setUnavailable();
    setStatusColor(QColor(QStringLiteral("#657080")));
}

void MetricCard::setStatusForPercent(
    const double value,
    const double warning,
    const double critical)
{
    if (value >= critical) {
        setStatusColor(QColor(QStringLiteral("#FF3B30")));
    } else if (value >= warning) {
        setStatusColor(QColor(QStringLiteral("#F4C542")));
    } else {
        setStatusColor(QColor(QStringLiteral("#2ECC71")));
    }
}

void MetricCard::setStatusColor(const QColor& color)
{
    stripe_->setStyleSheet(QStringLiteral(
        "background-color: %1; border: none; border-top-left-radius: 12px; "
        "border-bottom-left-radius: 12px;")
                                  .arg(color.name()));
}

void MetricCard::pushHistory(const double value)
{
    sparkline_->pushValue(value);
}

void MetricCard::setGaugeLabel(const QString& label)
{
    gauge_->setLabel(label);
}

void MetricCard::setBadge(const QString& text)
{
    badge_->setText(text);
    badge_->setVisible(!text.isEmpty());
    updateMinimumHeight();
}

void MetricCard::setPresentationMode(const PresentationMode requestedMode)
{
    presentationMode_ = requestedMode;
    const bool gaugeMode = requestedMode == PresentationMode::Gauge && percentageMetric_;
    const bool denseMode = requestedMode == PresentationMode::Dense;
    gauge_->setVisible(gaugeMode);
    valueLabel_->setVisible(!gaugeMode && !denseMode);
    detailLabel_->setVisible(!denseMode && !currentDetail_.isEmpty());
    denseSummaryLabel_->setVisible(denseMode);
    sparkline_->setVisible(!denseMode);
    setMinimumHeight(gaugeMode ? 170 : denseMode ? 88 : 145);
    contentLayout_->setContentsMargins(
        denseMode ? 8 : 12,
        denseMode ? 5 : 10,
        denseMode ? 10 : 14,
        denseMode ? 5 : 10);
    contentLayout_->setSpacing(denseMode ? 1 : 5);
    const QString name = gaugeMode ? QStringLiteral("gauge")
        : denseMode ? QStringLiteral("dense") : QStringLiteral("standard");
    setProperty("presentationMode", name);
    style()->unpolish(this);
    style()->polish(this);
    refreshDenseSummary();
    updateGeometry();
}

void MetricCard::refreshDenseSummary()
{
    const QString escapedValue = currentValue_.toHtmlEscaped();
    const QString escapedDetail = currentDetail_.toHtmlEscaped()
        .replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
    denseSummaryLabel_->setText(QStringLiteral(
        "<span style='font-size:20pt; font-weight:700'>%1</span>%2")
        .arg(escapedValue, escapedDetail.isEmpty()
            ? QString() : QStringLiteral("<br/><span style='font-size:10pt'>%1</span>")
                              .arg(escapedDetail)));
    updateMinimumHeight();
}

void MetricCard::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    updateMinimumHeight();
}

void MetricCard::updateMinimumHeight()
{
    if (layout() == nullptr) return;
    const bool dense = presentationMode_ == PresentationMode::Dense;
    const bool gauge = presentationMode_ == PresentationMode::Gauge && percentageMetric_;
    const int base = dense ? 88 : gauge ? 170 : 145;
    // QDockWidget does not propagate height-for-width from wrapped labels itself.
    // Recompute the minimum after a resize or new text, leaving native docking intact.
    const int required = qMax(base, layout()->totalHeightForWidth(width()));
    if (minimumHeight() != required) setMinimumHeight(required);
}

void MetricCard::setThemeColors(
    const QColor& accent,
    const QColor& track,
    const QColor& text)
{
    gauge_->setColors(accent, track, text);
}

MetricCard::PresentationMode MetricCard::presentationMode() const noexcept
{
    return presentationMode_;
}

bool MetricCard::gaugeVisible() const
{
    return !gauge_->isHidden();
}

bool MetricCard::sparklineVisible() const
{
    return !sparkline_->isHidden();
}

} // namespace orion::app
