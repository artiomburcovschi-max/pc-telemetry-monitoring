#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

namespace orion::app {

class SparklineWidget final : public QWidget {
public:
    explicit SparklineWidget(
        double minimum = 0.0,
        double maximum = 100.0,
        bool automaticRange = false,
        QWidget* parent = nullptr);

    void pushValue(double value);
    void clear();
    void setAccentColor(const QColor& color);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<double> values_;
    double minimum_ {0.0};
    double maximum_ {100.0};
    bool automaticRange_ {false};
    QColor accent_ {QStringLiteral("#FF8A00")};
};

} // namespace orion::app
