#pragma once

#include "orion/core/network_counters.h"

#include <QMetaType>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace orion::app {

struct DiskTelemetry {
    QString name;
    QString mountPoint;
    QString fileSystem;
    QString storageType;
    double totalGiB {0.0};
    double usedGiB {0.0};
    double freeGiB {0.0};
    double usedPercent {0.0};
    double readMiBPerSecond {-1.0};
    double writeMiBPerSecond {-1.0};
    double totalReadGiB {-1.0};
    double totalWrittenGiB {-1.0};
    double busyPercent {-1.0};
    double readLatencyMs {-1.0};
    double writeLatencyMs {-1.0};
};

struct RuntimeTelemetry {
    QJsonObject appMonitorSystemEnvelope;
    QJsonObject incidentSystemEnvelope;
    orion::core::NetworkCounterSample network;
    double ramAvailablePercent {-1.0};
    double swapUsedPercent {-1.0};
    double commitUsedPercent {-1.0};
    double commitUsedMiB {-1.0};
    double commitLimitMiB {-1.0};
    double pagefileUsedPercent {-1.0};
    double pagesInputPerSecond {-1.0};
    double pageReadsPerSecond {-1.0};
    double pagesPerSecond {-1.0};
    double pagesOutputPerSecond {-1.0};
    double pageWritesPerSecond {-1.0};
    double systemContextSwitchesPerSecond {-1.0};
    double diskBusyPercent {-1.0};
    double diskReadLatencyMs {-1.0};
    double diskWriteLatencyMs {-1.0};
    QString pagingActivity {QStringLiteral("unknown")};
    QString hardFaultActivity {QStringLiteral("unknown")};
    QString memoryPressure {QStringLiteral("unknown")};
    QString pagingInterpretation {QStringLiteral("counter_data_unavailable")};
    QString pagingRateQuality {QStringLiteral("unsupported")};
};

struct TemperatureTelemetry {
    QString component;
    QString label;
    double valueC {0.0};
    double highC {-1.0};
    double criticalC {-1.0};
    QString source;
};

struct FanTelemetry {
    QString component;
    QString label;
    double rpm {-1.0};
    double percent {-1.0};
    QString source;
};

struct ProcessTelemetry {
    quint32 pid {0};
    quint32 parentPid {0};
    QString name;
    quint32 threadCount {0};
    double cpuPercent {-1.0};
    double workingSetMiB {-1.0};
    double privateMiB {-1.0};
    double ioMiB {-1.0};
    quint64 creationIdentity {0};
};

struct AutostartTelemetry {
    QString name;
    QString command;
    QString source;
    int enabled {-1};
    QString category;
};

} // namespace orion::app

Q_DECLARE_METATYPE(orion::app::DiskTelemetry)
Q_DECLARE_METATYPE(QVector<orion::app::DiskTelemetry>)
Q_DECLARE_METATYPE(orion::app::RuntimeTelemetry)
Q_DECLARE_METATYPE(orion::app::TemperatureTelemetry)
Q_DECLARE_METATYPE(QVector<orion::app::TemperatureTelemetry>)
Q_DECLARE_METATYPE(orion::app::FanTelemetry)
Q_DECLARE_METATYPE(QVector<orion::app::FanTelemetry>)
Q_DECLARE_METATYPE(orion::app::ProcessTelemetry)
Q_DECLARE_METATYPE(QVector<orion::app::ProcessTelemetry>)
Q_DECLARE_METATYPE(orion::app::AutostartTelemetry)
Q_DECLARE_METATYPE(QVector<orion::app::AutostartTelemetry>)
