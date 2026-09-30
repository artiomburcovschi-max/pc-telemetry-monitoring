#pragma once

#include <QColor>
#include <QFrame>
#include <QString>
#include <QVector>

class QComboBox;
class QLabel;

namespace orion::app {

class CpuCorePlotCanvas;

class CpuCoreChartWidget final : public QFrame {
    Q_OBJECT

public:
    static constexpr int historyPointLimit = 60;
    static constexpr int historyLinesPerPage = 8;

    explicit CpuCoreChartWidget(QWidget* parent = nullptr);

    void updateCores(const QVector<double>& loads);
    void setMode(const QString& mode);
    void setHistoryPage(int page);
    void setThemeColors(
        const QColor& background,
        const QColor& panel,
        const QColor& text,
        const QColor& muted,
        const QColor& accent,
        const QColor& border);

    [[nodiscard]] QString currentMode() const;
    [[nodiscard]] int sampleCount() const noexcept;
    [[nodiscard]] int coreCount() const noexcept;
    [[nodiscard]] int historyPageCount() const noexcept;
    [[nodiscard]] QVector<double> currentLoads() const;
    [[nodiscard]] QVector<double> historyForCore(int coreNumber) const;
    [[nodiscard]] QVector<int> selectedHistoryCores() const;

signals:
    void modeChanged(const QString& mode);

private:
    void rebuildGroups();
    void syncControls();
    void updateSummary();
    [[nodiscard]] QColor lineColor(int position) const;

    QComboBox* modeCombo_ {nullptr};
    QLabel* groupLabel_ {nullptr};
    QComboBox* groupCombo_ {nullptr};
    QLabel* summaryLabel_ {nullptr};
    CpuCorePlotCanvas* plot_ {nullptr};
    QVector<QVector<double>> history_;
    QVector<double> currentLoads_;
    int sampleCount_ {0};
    QColor background_ {QStringLiteral("#0A0A0C")};
    QColor panel_ {QStringLiteral("#141417")};
    QColor text_ {QStringLiteral("#E8E8EC")};
    QColor muted_ {QStringLiteral("#6E6E76")};
    QColor accent_ {QStringLiteral("#F5C518")};
    QColor border_ {QStringLiteral("#2A2A2E")};

    friend class CpuCorePlotCanvas;
};

} // namespace orion::app
