#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPolygonF>
#include <QWidget>
#include <deque>

namespace orion::app {

// Active observation time, not wall time. Missing values and pauses break paths.
class AppMonitorChart final : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Cpu, Memory };
    explicit AppMonitorChart(Mode mode, QWidget* parent = nullptr);
    void appendSample(const QJsonObject& sample);
    void clear();
    void setMonitoringPaused(bool paused);
    void setThemeColors(QColor panel, QColor text, QColor muted, QColor border);
    [[nodiscard]] int sampleCount() const { return static_cast<int>(points_.size()); }
    [[nodiscard]] double axisMaximum() const;
    [[nodiscard]] QRectF plotArea() const;
    [[nodiscard]] QVector<QPolygonF> lineSegments(int series = 0) const;
    [[nodiscard]] QString hoverText() const;
    [[nodiscard]] int hoverIndex() const { return hoverIndex_; }
    QSize sizeHint() const override { return {560, 230}; }
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    struct Point { double elapsed, first, second; bool breakBefore; };
    [[nodiscard]] QPointF position(const Point& point, double value, double maximum) const;
    [[nodiscard]] QVector<QPolygonF> segments(int series, double maximum) const;
    void updateHover();
    Mode mode_;
    std::deque<Point> points_;
    bool paused_ = false, breakPending_ = false, mouseInside_ = false;
    int hoverIndex_ = -1;
    QPointF mouse_;
    QColor panel_{"#16202e"}, text_{"#e6edf3"}, muted_{"#9caec4"}, border_{"#334155"};
};

} // namespace orion::app
