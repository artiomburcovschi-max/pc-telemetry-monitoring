#pragma once

#include <QJsonObject>
#include <QThread>
#include <QWaitCondition>
#include <QMutex>

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>

namespace orion::app {

struct IncidentOptions {
    QString incidentId;
    QString markerTimestamp;
    double markerMonotonic {0.0};
    double preSeconds {60.0};
    double postSeconds {15.0};
    // Separate from the session-relative JSON marker; never mix clock domains.
    std::optional<std::chrono::steady_clock::time_point> markerSteady;
};

class IncidentWorker final : public QThread {
    Q_OBJECT

public:
    using LogCollector = std::function<QJsonObject()>;
    explicit IncidentWorker(QObject* parent = nullptr, LogCollector collector = {});
    void capture(const IncidentOptions& options);
    void cancel();

signals:
    void phaseChanged(const QString& phase);
    void captureReady(const QJsonObject& partial);

protected:
    void run() override;

private:
    QMutex mutex_;
    QMutex waitMutex_;
    QWaitCondition waitCondition_;
    IncidentOptions options_;
    LogCollector collector_;
    std::chrono::steady_clock::time_point captureEntered_;
    std::atomic_bool cancelled_ {false};
};

} // namespace orion::app
