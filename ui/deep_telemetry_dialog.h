#pragma once

#include "orion/core/session_peaks.h"

#include <QDialog>
#include <QJsonObject>
#include <QThread>
#include <QVector>
#include <functional>
#include <optional>

class QLabel;
class QPushButton;
class QTextEdit;

namespace orion::app {

class PerCoreBarChart;

class BootLogWorker final : public QThread {
    Q_OBJECT
public:
    explicit BootLogWorker(QString path);
signals:
    void resultReady(const QString& text, const QString& description);
protected:
    void run() override;
private:
    QString path_;
};

class SystemErrorsWorker final : public QThread {
    Q_OBJECT

public:
    using Collector = std::function<QJsonObject()>;
    explicit SystemErrorsWorker(QObject* parent = nullptr, Collector collector = {});

signals:
    void resultReady(const QJsonObject& result);

protected:
    void run() override;
private:
    Collector collector_;
};

class DeepTelemetryDialog final : public QDialog {
    Q_OBJECT

public:
    explicit DeepTelemetryDialog(
        QWidget* parent = nullptr,
        bool refreshErrorsOnOpen = true,
        SystemErrorsWorker::Collector errorsCollector = {});
    ~DeepTelemetryDialog() override;

    void applyTelemetry(
        const QVector<double>& coreLoads,
        const QVector<double>& coreFrequenciesMhz,
        double averageFrequencyMhz,
        double uptimeSeconds,
        const orion::core::SessionPeaks& peaks);
    void setMonitoringPaused(bool paused);
    [[nodiscard]] QJsonObject exportPayload() const;

private slots:
    void toggleLocalPause();
    void refreshSystemErrors();
    void applySystemErrors(const QJsonObject& result);
    void clearSystemErrors();
    void showBootLog();
    void exportLogs();

private:
    void updatePausePresentation();
    void updateErrorsPresentation();

    PerCoreBarChart* frequencyChart_ {nullptr};
    PerCoreBarChart* loadChart_ {nullptr};
    QLabel* frequencyQualityLabel_ {nullptr};
    QLabel* uptimeLabel_ {nullptr};
    QLabel* peaksLabel_ {nullptr};
    QLabel* statusLabel_ {nullptr};
    QLabel* errorsQualityLabel_ {nullptr};
    QPushButton* pauseButton_ {nullptr};
    QPushButton* refreshButton_ {nullptr};
    QTextEdit* errorsOutput_ {nullptr};
    SystemErrorsWorker* errorsWorker_ {nullptr};
    QJsonObject lastSnapshot_;
    QJsonObject lastErrorsReport_;
    std::optional<QJsonObject> pendingErrorsReport_;
    SystemErrorsWorker::Collector errorsCollector_;
    quint64 errorsGeneration_ {0};
    bool localPaused_ {false};
    bool globalPaused_ {false};
};

} // namespace orion::app
