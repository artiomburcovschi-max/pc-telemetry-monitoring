#pragma once

#include <QColor>
#include <QPointF>
#include <QPolygonF>
#include <QVector>
#include <QWidget>

namespace orion::app {

struct NetworkTrafficSample {
    qint64 elapsedMs;
    double downloadBytesPerSecond;
    double uploadBytesPerSecond;
    bool breakBefore;
};

class NetworkTrafficChart final : public QWidget {
    Q_OBJECT
public:
    static constexpr qint64 historyDurationMs = 60000;
    static constexpr int historyPointLimit = 120;
    explicit NetworkTrafficChart(QWidget* parent = nullptr);

    void appendSample(qint64 elapsedMs, double downloadBytesPerSecond, double uploadBytesPerSecond);
    void setMonitoringPaused(bool paused);
    void setThemeColors(const QColor& background, const QColor& panel, const QColor& text,
        const QColor& muted, const QColor& accent, const QColor& border, bool quantum);

    [[nodiscard]] const QVector<NetworkTrafficSample>& samples() const { return samples_; }
    [[nodiscard]] int sampleCount() const { return static_cast<int>(samples_.size()); }
    [[nodiscard]] bool isMonitoringPaused() const { return paused_; }
    [[nodiscard]] int hoveredSampleIndex() const { return hoveredIndex_; }
    [[nodiscard]] QString hoverText() const;
    [[nodiscard]] QRectF plotArea() const;
    [[nodiscard]] QVector<QPolygonF> lineSegments(bool download) const;
    [[nodiscard]] QSize sizeHint() const override { return {390, 280}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateHover();
    [[nodiscard]] double unitDivisor() const;
    [[nodiscard]] double axisMaximum() const;
    [[nodiscard]] QPointF samplePosition(int index, bool download) const;
    QVector<NetworkTrafficSample> samples_;
    bool paused_ {false};
    bool breakPending_ {false};
    bool pointerInside_ {false};
    bool quantum_ {false};
    QPointF pointer_;
    int hoveredIndex_ {-1};
    QColor background_ {QStringLiteral("#0A0A0C")};
    QColor panel_ {QStringLiteral("#141417")};
    QColor text_ {QStringLiteral("#E8E8EC")};
    QColor muted_ {QStringLiteral("#8E8E96")};
    QColor accent_ {QStringLiteral("#F5C518")};
    QColor border_ {QStringLiteral("#2A2A2E")};
    QColor download_ {QStringLiteral("#2ECC71")};
    QColor upload_ {QStringLiteral("#E74C3C")};
};

} // namespace orion::app
