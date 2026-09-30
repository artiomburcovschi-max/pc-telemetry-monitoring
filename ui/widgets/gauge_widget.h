#pragma once

#include <QColor>
#include <QWidget>

class QPaintEvent;
class QPropertyAnimation;

namespace orion::app {

class GaugeWidget final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue)

public:
    explicit GaugeWidget(
        QString label = {},
        QString unit = QStringLiteral("%"),
        QWidget* parent = nullptr);

    [[nodiscard]] double value() const noexcept;
    [[nodiscard]] double targetValue() const noexcept;
    [[nodiscard]] bool available() const noexcept;
    void setValue(double value);
    void setTargetValue(double value, const QString& unit = {});
    void setUnavailable(bool unavailable = true);
    void setLabel(const QString& label);
    void setColors(const QColor& accent, const QColor& track, const QColor& text);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double value_ {0.0};
    double targetValue_ {0.0};
    bool available_ {false};
    QString label_;
    QString unit_ {QStringLiteral("%")};
    QColor accent_ {QStringLiteral("#00E5FF")};
    QColor track_ {QStringLiteral("#122A3A")};
    QColor textColor_ {QStringLiteral("#E4F6FF")};
    QPropertyAnimation* animation_ {nullptr};
};

} // namespace orion::app
