#pragma once

#include "../ping_worker.h"
#include "../overlay_fps_counter.h"

#include <QWidget>

class QColor;
class QCloseEvent;
class QHideEvent;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QShowEvent;
class QTimer;
class QVariantAnimation;

namespace orion::app {

class GamerOverlay final : public QWidget {
    Q_OBJECT

public:
    explicit GamerOverlay(QString pingTarget, QWidget* parent = nullptr);
    ~GamerOverlay() override;

    void updateTelemetry(
        double cpuPercent,
        double gpuPercent,
        double ramPercent,
        double downloadBytesPerSecond,
        double uploadBytesPerSecond);
    void setPingTarget(const QString& target);
    void setMonitoringPaused(bool paused);
    void setAlarmState(bool active);
    void setCriticalMetrics(bool cpuCritical, bool gpuCritical, bool ramCritical);
    void applyTheme(
        const QString& panel,
        const QString& text,
        const QString& accent,
        const QString& secondary,
        const QString& border,
        const QString& font);
    void shutdown();

    [[nodiscard]] bool monitoringPaused() const noexcept;
    [[nodiscard]] bool alarmActive() const noexcept;
    [[nodiscard]] bool pingWorkerRunning() const;

signals:
    void closedByUser();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void applyPing(const PingTelemetry& sample);
    void syncRuntimeState();
    void rebuildStyle(const QColor& borderColor);
    void updateMetricStyle(QLabel* label, bool critical);
    [[nodiscard]] QString formatPercent(double value) const;

    QLabel* cpuLabel_ {nullptr};
    QLabel* gpuLabel_ {nullptr};
    QLabel* ramLabel_ {nullptr};
    QLabel* networkLabel_ {nullptr};
    QLabel* pingLabel_ {nullptr};
    QLabel* fpsLabel_ {nullptr};
    QTimer* renderTimer_ {nullptr};
    QTimer* keepOnTopTimer_ {nullptr};
    QVariantAnimation* pulseAnimation_ {nullptr};
    PingWorker* pingWorker_ {nullptr};
    UiFpsCounter fpsCounter_;
    QPoint dragOffset_;
    bool dragging_ {false};
    bool paused_ {false};
    bool alarmActive_ {false};
    bool cpuCritical_ {false};
    bool gpuCritical_ {false};
    bool ramCritical_ {false};
    bool shuttingDown_ {false};
    int fpsDisplayTicks_ {0};
    QString pingTarget_;
    QString panelColor_ {QStringLiteral("#141417")};
    QString textColor_ {QStringLiteral("#E8E8EC")};
    QString accentColor_ {QStringLiteral("#F5C518")};
    QString secondaryColor_ {QStringLiteral("#8A8F98")};
    QString borderColor_ {QStringLiteral("#2A2A2E")};
    QString fontFamily_ {QStringLiteral("'Consolas', monospace")};
};

} // namespace orion::app
