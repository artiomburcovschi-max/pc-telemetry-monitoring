#pragma once

#include <QFrame>

class QLabel;
class QFrame;
class QColor;
class QVBoxLayout;
class QPushButton;

namespace orion::app {

class GaugeWidget;

class MetricCard final : public QFrame {
    Q_OBJECT

public:
    enum class PresentationMode {
        Standard,
        Gauge,
        Dense,
    };

    explicit MetricCard(
        const QString& title,
        bool automaticHistoryRange = false,
        QWidget* parent = nullptr);

    void setPercent(double value);
    void setTextValue(const QString& value, const QString& detail = {});
    void setDetailText(const QString& detail);
    void setUnavailable(const QString& detail = {});
    void setStatusForPercent(double value, double warning = 80.0, double critical = 95.0);
    void setStatusColor(const QColor& color);
    void pushHistory(double value);
    void setGaugeLabel(const QString& label);
    void setBadge(const QString& text);
    void setPresentationMode(PresentationMode mode);
    void setThemeColors(const QColor& accent, const QColor& track, const QColor& text);

    [[nodiscard]] PresentationMode presentationMode() const noexcept;
    [[nodiscard]] bool gaugeVisible() const;
    [[nodiscard]] bool sparklineVisible() const;

signals:
    void badgeClicked();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void refreshDenseSummary();
    void updateMinimumHeight();

    QFrame* stripe_ {nullptr};
    QLabel* valueLabel_ {nullptr};
    QLabel* detailLabel_ {nullptr};
    QLabel* denseSummaryLabel_ {nullptr};
    QPushButton* badge_ {nullptr};
    class SparklineWidget* sparkline_ {nullptr};
    GaugeWidget* gauge_ {nullptr};
    QVBoxLayout* contentLayout_ {nullptr};
    bool percentageMetric_ {false};
    PresentationMode presentationMode_ {PresentationMode::Standard};
    QString currentValue_ {QStringLiteral("н/д")};
    QString currentDetail_;
};

} // namespace orion::app
