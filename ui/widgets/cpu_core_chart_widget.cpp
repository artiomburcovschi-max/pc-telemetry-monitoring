#include "cpu_core_chart_widget.h"

#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace orion::app {
namespace {

constexpr auto profileMode = "profile";
constexpr auto barsMode = "bars";
constexpr auto historyMode = "history";

[[nodiscard]] double missingValue()
{
    return std::numeric_limits<double>::quiet_NaN();
}

[[nodiscard]] bool validLoad(const double value)
{
    return std::isfinite(value);
}

[[nodiscard]] QPointF chartPoint(
    const QRectF& plot,
    const double xFraction,
    const double load)
{
    return {plot.left() + std::clamp(xFraction, 0.0, 1.0) * plot.width(),
        plot.bottom() - std::clamp(load, 0.0, 100.0) / 100.0 * plot.height()};
}

void drawFiniteLine(
    QPainter& painter,
    const QVector<QPointF>& points,
    const QPen& pen)
{
    if (points.isEmpty()) return;
    painter.setPen(pen);
    if (points.size() == 1) {
        painter.drawEllipse(points.front(), 2.5, 2.5);
        return;
    }
    QPainterPath path(points.front());
    for (qsizetype index = 1; index < points.size(); ++index) {
        path.lineTo(points[index]);
    }
    painter.drawPath(path);
}

} // namespace

class CpuCorePlotCanvas final : public QWidget {
public:
    explicit CpuCorePlotCanvas(CpuCoreChartWidget* owner)
        : QWidget(owner)
        , owner_(owner)
    {
        setObjectName(QStringLiteral("CpuCorePlot"));
        setMinimumHeight(235);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setToolTip(QStringLiteral(
            "Общая шкала 0–100%. Пропуск измерения разрывает линию и не заменяется нулём."));
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return {720, 250};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), owner_->background_);

        painter.setFont(QFont(painter.font().family(), 8));
        const bool history = owner_->currentMode() == QString::fromLatin1(historyMode);
        int legendRows = 1;
        qreal legendWidth = 0;
        for (int core : owner_->selectedHistoryCores()) {
            const qreal entry = painter.fontMetrics().horizontalAdvance(QStringLiteral("CPU %1").arg(core)) + 24.0;
            if (legendWidth > 0 && legendWidth + entry > width() - 62.0) {
                ++legendRows;
                legendWidth = 0;
            }
            legendWidth += entry;
        }
        const qreal topMargin = history ? std::max(48.0, legendRows * 17.0 + 14.0) : 14.0;
        const QRectF plot(48.0, topMargin,
            std::max(1.0, width() - 62.0),
            std::max(1.0, height() - topMargin - 32.0));

        painter.setPen(QPen(owner_->border_, 1.0));
        painter.drawRect(plot);
        painter.setFont(QFont(painter.font().family(), 8));
        for (int value = 0; value <= 100; value += 25) {
            const qreal y = chartPoint(plot, 0.0, value).y();
            painter.setPen(QPen(owner_->border_, 1.0, Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(owner_->muted_);
            painter.drawText(QRectF(0.0, y - 9.0, 42.0, 18.0),
                Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1%").arg(value));
        }

        if (owner_->history_.isEmpty()) {
            painter.setPen(owner_->muted_);
            painter.drawText(plot, Qt::AlignCenter,
                QStringLiteral("Нет данных по логическим процессорам"));
            return;
        }

        if (owner_->currentMode() == QString::fromLatin1(barsMode)) {
            drawBars(painter, plot);
        } else if (history) {
            drawHistory(painter, plot);
        } else {
            drawProfile(painter, plot);
        }
    }

private:
    void drawCoreTicks(QPainter& painter, const QRectF& plot) const
    {
        const int count = owner_->history_.size();
        const int step = count <= 16 ? 1 : count <= 32 ? 2 : count <= 64 ? 4 : 8;
        painter.setPen(owner_->muted_);
        for (int index = 0; index < count; index += step) {
            const double fraction = count <= 1 ? 0.5
                : static_cast<double>(index) / static_cast<double>(count - 1);
            const qreal x = chartPoint(plot, fraction, 0.0).x();
            painter.drawText(QRectF(x - 17.0, plot.bottom() + 4.0, 34.0, 18.0),
                Qt::AlignHCenter | Qt::AlignTop, QString::number(index + 1));
        }
        if (count > 1 && (count - 1) % step != 0) {
            painter.drawText(QRectF(plot.right() - 17.0, plot.bottom() + 4.0, 34.0, 18.0),
                Qt::AlignHCenter | Qt::AlignTop, QString::number(count));
        }
    }

    void drawProfile(QPainter& painter, const QRectF& plot) const
    {
        drawCoreTicks(painter, plot);
        const int count = owner_->history_.size();
        QVector<QPointF> segment;
        const QPen linePen(owner_->accent_, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        const auto flush = [&] {
            if (segment.size() > 1) {
                QPainterPath fillPath(segment.front());
                for (qsizetype index = 1; index < segment.size(); ++index) {
                    fillPath.lineTo(segment[index]);
                }
                fillPath.lineTo(segment.back().x(), plot.bottom());
                fillPath.lineTo(segment.front().x(), plot.bottom());
                fillPath.closeSubpath();
                QColor fill(owner_->accent_);
                fill.setAlpha(42);
                painter.fillPath(fillPath, fill);
            }
            drawFiniteLine(painter, segment, linePen);
            segment.clear();
        };
        for (int index = 0; index < count; ++index) {
            const double value = index < owner_->currentLoads_.size()
                ? owner_->currentLoads_[index] : missingValue();
            if (!validLoad(value)) {
                flush();
                continue;
            }
            const double fraction = count <= 1 ? 0.5
                : static_cast<double>(index) / static_cast<double>(count - 1);
            segment.append(chartPoint(plot, fraction, value));
        }
        flush();

        painter.setPen(QPen(owner_->accent_, 1.4));
        painter.setBrush(owner_->background_);
        for (int index = 0; index < count; ++index) {
            const double value = index < owner_->currentLoads_.size()
                ? owner_->currentLoads_[index] : missingValue();
            if (!validLoad(value)) continue;
            const double fraction = count <= 1 ? 0.5
                : static_cast<double>(index) / static_cast<double>(count - 1);
            painter.drawEllipse(chartPoint(plot, fraction, value), 3.0, 3.0);
        }
    }

    void drawBars(QPainter& painter, const QRectF& plot) const
    {
        const int count = owner_->history_.size();
        const qreal slot = plot.width() / static_cast<qreal>(std::max(1, count));
        const qreal barWidth = std::max(1.0, slot * 0.72);
        for (int index = 0; index < count; ++index) {
            const double value = index < owner_->currentLoads_.size()
                ? owner_->currentLoads_[index] : missingValue();
            if (!validLoad(value)) continue;
            const qreal height = value / 100.0 * plot.height();
            const QRectF bar(plot.left() + index * slot + (slot - barWidth) / 2.0,
                plot.bottom() - height, barWidth, height);
            const QColor color = value >= 95.0 ? QColor(QStringLiteral("#FF3B30"))
                : value >= 76.0 ? QColor(QStringLiteral("#F4C542"))
                                : QColor(QStringLiteral("#2ECC71"));
            QColor fill(color);
            fill.setAlpha(190);
            painter.setPen(QPen(color, 1.0));
            painter.setBrush(fill);
            painter.drawRect(bar);
        }

        painter.setPen(owner_->muted_);
        const int tickStep = count <= 16 ? 1 : count <= 32 ? 2 : count <= 64 ? 4 : 8;
        for (int index = 0; index < count; index += tickStep) {
            const qreal x = plot.left() + (index + 0.5) * slot;
            painter.drawText(QRectF(x - 17.0, plot.bottom() + 4.0, 34.0, 18.0),
                Qt::AlignHCenter | Qt::AlignTop, QString::number(index + 1));
        }
    }

    void drawHistory(QPainter& painter, const QRectF& plot) const
    {
        const QVector<int> cores = owner_->selectedHistoryCores();
        qreal legendX = plot.left();
        qreal legendY = 6.0;
        for (int position = 0; position < cores.size(); ++position) {
            const QColor color = owner_->lineColor(position);
            const QString label = QStringLiteral("CPU %1").arg(cores[position]);
            const qreal width = painter.fontMetrics().horizontalAdvance(label) + 24.0;
            if (legendX + width > plot.right()) {
                legendX = plot.left();
                legendY += 17.0;
            }
            painter.setPen(QPen(color, 2.0));
            painter.drawLine(QPointF(legendX, legendY + 7.0),
                QPointF(legendX + 12.0, legendY + 7.0));
            painter.setPen(owner_->text_);
            painter.drawText(QRectF(legendX + 16.0, legendY, width - 16.0, 15.0),
                Qt::AlignLeft | Qt::AlignVCenter, label);
            legendX += width;
        }

        for (int position = 0; position < cores.size(); ++position) {
            const int coreIndex = cores[position] - 1;
            if (coreIndex < 0 || coreIndex >= owner_->history_.size()) continue;
            const auto& values = owner_->history_[coreIndex];
            QVector<QPointF> segment;
            const QPen linePen(owner_->lineColor(position), 1.8,
                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            const auto flush = [&] {
                drawFiniteLine(painter, segment, linePen);
                segment.clear();
            };
            for (int index = 0; index < values.size(); ++index) {
                if (!validLoad(values[index])) {
                    flush();
                    continue;
                }
                const int secondsAgo = values.size() - 1 - index;
                const double fraction = 1.0
                    - static_cast<double>(secondsAgo)
                        / static_cast<double>(CpuCoreChartWidget::historyPointLimit - 1);
                segment.append(chartPoint(plot, fraction, values[index]));
            }
            flush();
        }

        painter.setPen(owner_->muted_);
        painter.drawText(QRectF(plot.left() - 12.0, plot.bottom() + 4.0, 70.0, 18.0),
            Qt::AlignLeft | Qt::AlignTop, QStringLiteral("−59 сек"));
        painter.drawText(QRectF(plot.center().x() - 25.0, plot.bottom() + 4.0, 50.0, 18.0),
            Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("−30"));
        painter.drawText(QRectF(plot.right() - 55.0, plot.bottom() + 4.0, 55.0, 18.0),
            Qt::AlignRight | Qt::AlignTop, QStringLiteral("сейчас"));
    }

    CpuCoreChartWidget* owner_ {nullptr};
};

CpuCoreChartWidget::CpuCoreChartWidget(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("CpuCoreChart"));
    setMinimumHeight(285);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);
    auto* controls = new QGridLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(7);

    auto* modeLabel = new QLabel(QStringLiteral("Вид:"), this);
    modeCombo_ = new QComboBox(this);
    modeCombo_->setObjectName(QStringLiteral("CpuCoreChartMode"));
    modeCombo_->addItem(QStringLiteral("Линия по ядрам"), QString::fromLatin1(profileMode));
    modeCombo_->addItem(QStringLiteral("Гистограмма"), QString::fromLatin1(barsMode));
    modeCombo_->addItem(QStringLiteral("История 60 секунд"), QString::fromLatin1(historyMode));
    modeCombo_->setToolTip(QStringLiteral(
        "Линия и гистограмма показывают текущую загрузку всех логических CPU. "
        "История показывает до восьми движущихся линий одновременно."));
    groupLabel_ = new QLabel(QStringLiteral("Группа:"), this);
    groupCombo_ = new QComboBox(this);
    groupCombo_->setObjectName(QStringLiteral("CpuCoreChartGroup"));
    groupCombo_->setMinimumWidth(120);
    summaryLabel_ = new QLabel(QStringLiteral("Данные по ядрам пока недоступны"), this);
    summaryLabel_->setObjectName(QStringLiteral("CpuCoreChartSummary"));
    summaryLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    summaryLabel_->setWordWrap(true);

    controls->addWidget(modeLabel, 0, 0);
    controls->addWidget(modeCombo_, 0, 1);
    controls->addWidget(groupLabel_, 1, 0);
    controls->addWidget(groupCombo_, 1, 1);
    controls->setColumnStretch(2, 1);
    root->addLayout(controls);
    root->addWidget(summaryLabel_);
    plot_ = new CpuCorePlotCanvas(this);
    root->addWidget(plot_, 1);

    connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        syncControls();
        plot_->update();
        emit modeChanged(currentMode());
    });
    connect(groupCombo_, &QComboBox::currentIndexChanged, plot_,
        qOverload<>(&QWidget::update));
    rebuildGroups();
    syncControls();
}

void CpuCoreChartWidget::updateCores(const QVector<double>& loads)
{
    const int knownCount = std::max(history_.size(), loads.size());
    if (knownCount == 0) {
        currentLoads_.clear();
        updateSummary();
        plot_->update();
        return;
    }

    sampleCount_ = std::min(historyPointLimit, sampleCount_ + 1);
    const int previousCount = history_.size();
    history_.resize(knownCount);
    for (int index = previousCount; index < knownCount; ++index) {
        history_[index].fill(missingValue(), std::max(0, sampleCount_ - 1));
    }

    currentLoads_.fill(missingValue(), knownCount);
    for (int index = 0; index < knownCount; ++index) {
        double value = missingValue();
        if (index < loads.size() && validLoad(loads[index])) {
            value = std::clamp(loads[index], 0.0, 100.0);
            currentLoads_[index] = value;
        }
        history_[index].append(value);
        if (history_[index].size() > historyPointLimit) {
            history_[index].remove(0, history_[index].size() - historyPointLimit);
        }
    }
    if (previousCount != knownCount) rebuildGroups();
    updateSummary();
    plot_->update();
}

void CpuCoreChartWidget::setMode(const QString& mode)
{
    int index = modeCombo_->findData(mode);
    if (index < 0) index = modeCombo_->findData(QString::fromLatin1(profileMode));
    modeCombo_->setCurrentIndex(index);
    syncControls();
    plot_->update();
}

void CpuCoreChartWidget::setHistoryPage(const int page)
{
    if (groupCombo_->count() == 0) return;
    groupCombo_->setCurrentIndex(std::clamp(page, 0, groupCombo_->count() - 1));
}

void CpuCoreChartWidget::setThemeColors(
    const QColor& background,
    const QColor& panel,
    const QColor& text,
    const QColor& muted,
    const QColor& accent,
    const QColor& border)
{
    background_ = background.isValid() ? background : background_;
    panel_ = panel.isValid() ? panel : panel_;
    text_ = text.isValid() ? text : text_;
    muted_ = muted.isValid() ? muted : muted_;
    accent_ = accent.isValid() ? accent : accent_;
    border_ = border.isValid() ? border : border_;
    summaryLabel_->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
        .arg(muted_.name()));
    plot_->update();
}

QString CpuCoreChartWidget::currentMode() const
{
    const QString mode = modeCombo_->currentData().toString();
    if (mode == QString::fromLatin1(profileMode)
        || mode == QString::fromLatin1(barsMode)
        || mode == QString::fromLatin1(historyMode)) {
        return mode;
    }
    return QString::fromLatin1(profileMode);
}

int CpuCoreChartWidget::sampleCount() const noexcept
{
    return sampleCount_;
}

int CpuCoreChartWidget::coreCount() const noexcept
{
    return history_.size();
}

int CpuCoreChartWidget::historyPageCount() const noexcept
{
    return (history_.size() + historyLinesPerPage - 1) / historyLinesPerPage;
}

QVector<double> CpuCoreChartWidget::currentLoads() const
{
    return currentLoads_;
}

QVector<double> CpuCoreChartWidget::historyForCore(const int coreNumber) const
{
    const int index = coreNumber - 1;
    return index >= 0 && index < history_.size() ? history_[index] : QVector<double> {};
}

QVector<int> CpuCoreChartWidget::selectedHistoryCores() const
{
    QVector<int> cores;
    if (history_.isEmpty()) return cores;
    int start = groupCombo_->currentData().toInt() - 1;
    if (start < 0 || start >= history_.size()) start = 0;
    const int end = std::min(start + historyLinesPerPage,
        static_cast<int>(history_.size()));
    for (int index = start; index < end; ++index) cores.append(index + 1);
    return cores;
}

void CpuCoreChartWidget::rebuildGroups()
{
    const int selectedStart = groupCombo_->currentData().toInt();
    groupCombo_->blockSignals(true);
    groupCombo_->clear();
    int selectedIndex = 0;
    for (int start = 0; start < history_.size(); start += historyLinesPerPage) {
        const int end = std::min(start + historyLinesPerPage,
            static_cast<int>(history_.size()));
        const QString label = end == start + 1
            ? QStringLiteral("CPU %1").arg(start + 1)
            : QStringLiteral("CPU %1–%2").arg(start + 1).arg(end);
        groupCombo_->addItem(label, start + 1);
        if (selectedStart == start + 1) selectedIndex = groupCombo_->count() - 1;
    }
    if (groupCombo_->count() > 0) groupCombo_->setCurrentIndex(selectedIndex);
    groupCombo_->blockSignals(false);
}

void CpuCoreChartWidget::syncControls()
{
    const bool history = currentMode() == QString::fromLatin1(historyMode);
    groupLabel_->setVisible(history);
    groupCombo_->setVisible(history);
}

void CpuCoreChartWidget::updateSummary()
{
    double total = 0.0;
    double maximum = -1.0;
    int maximumCore = -1;
    int validCount = 0;
    for (int index = 0; index < currentLoads_.size(); ++index) {
        if (!validLoad(currentLoads_[index])) continue;
        total += currentLoads_[index];
        ++validCount;
        if (currentLoads_[index] > maximum) {
            maximum = currentLoads_[index];
            maximumCore = index + 1;
        }
    }
    if (validCount == 0) {
        summaryLabel_->setText(history_.isEmpty()
            ? QStringLiteral("Данные по ядрам пока недоступны")
            : QStringLiteral("Последний замер недоступен · история сохранена"));
        return;
    }
    summaryLabel_->setText(QStringLiteral("Среднее %1% · максимум CPU %2: %3%")
        .arg(total / validCount, 0, 'f', 0)
        .arg(maximumCore)
        .arg(maximum, 0, 'f', 0));
}

QColor CpuCoreChartWidget::lineColor(const int position) const
{
    qreal hue = accent_.hsvHueF();
    if (hue < 0.0) hue = 0.08;
    return QColor::fromHsvF(std::fmod(hue + position * 0.127, 1.0), 0.72, 0.92, 1.0);
}

} // namespace orion::app
