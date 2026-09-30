#pragma once

#include "../telemetry_types.h"
#include "orion/core/session_peaks.h"
#include "orion/core/status_level.h"

#include <QColor>
#include <QFrame>
#include <QMap>
#include <QScrollArea>

class QLabel;
class QVBoxLayout;

namespace orion::app
{

class CpuCoreChartWidget;

// A direct counterpart of the Python DetailCard: stable rows, subtitle and status stripe.
class DetailCard final : public QFrame
{
    Q_OBJECT
public:
    explicit DetailCard(const QString& title, const QString& key, bool stripe, QWidget* parent);
    void setSubtitle(const QString& text);
    QLabel* setField(const QString& key, const QString& caption, const QString& value);
    [[nodiscard]] QString fieldValue(const QString& key) const;
    void addContent(QWidget* widget);
    void setStatus(orion::core::StatusLevel status);
    void setThemeColors(const QColor& accent, const QColor& muted);

private:
    void refreshStripe();
    QLabel* subtitle_;
    QFrame* stripe_;
    QVBoxLayout* content_;
    QVBoxLayout* fields_;
    QMap<QString, QLabel*> values_;
    orion::core::StatusLevel status_ { orion::core::StatusLevel::Unknown };
    QColor accent_ { QStringLiteral("#F5C518") };
    QColor muted_ { QStringLiteral("#6E6E76") };
};

struct DetailsSnapshot
{
    QString operatingSystem;
    QString cpuName;
    QString gpuName;
    double cpuPercent { -1.0 };
    double cpuFrequencyMhz { -1.0 };
    double cpuTemperatureC { -1.0 };
    int logicalCpus { 0 };
    double gpuPercent { -1.0 };
    double gpuTemperatureC { -1.0 };
    double gpuMemoryTotalGiB { -1.0 };
    double gpuMemoryUsedGiB { -1.0 };
    double gpuMemoryPercent { -1.0 };
    double ramPercent { -1.0 };
    double ramTotalGiB { -1.0 };
    QVector<DiskTelemetry> disks;
    QVector<TemperatureTelemetry> temperatures;
    QVector<FanTelemetry> fans;
};

class DetailsPanel final : public QScrollArea
{
    Q_OBJECT
public:
    explicit DetailsPanel(QWidget* parent = nullptr);
    void updateSnapshot(const DetailsSnapshot& sample, const orion::core::SessionPeaks& peaks);
    void setThemeColors(const QColor& accent, const QColor& muted);
    [[nodiscard]] CpuCoreChartWidget* coreChart() const noexcept { return coreChart_; }

private:
    void updateDisks(const QVector<DiskTelemetry>& disks);
    void updateSensors(const DetailsSnapshot& sample);
    DetailCard* cpu_;
    DetailCard* gpu_;
    DetailCard* os_;
    DetailCard* ram_;
    DetailCard* sensors_;
    CpuCoreChartWidget* coreChart_;
    QWidget* diskContainer_;
    QVBoxLayout* diskLayout_;
    DetailCard* emptyDisks_;
    QMap<QString, DetailCard*> disks_;
    QWidget* sensorRows_;
    QVBoxLayout* sensorLayout_;
    QMap<QString, QLabel*> sensorValues_;
    QColor accent_ { QStringLiteral("#F5C518") };
    QColor muted_ { QStringLiteral("#6E6E76") };
};

} // namespace orion::app
