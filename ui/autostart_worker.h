#pragma once

#include "telemetry_types.h"

#include <QThread>

namespace orion::app {

class AutostartWorker final : public QThread {
    Q_OBJECT

public:
    explicit AutostartWorker(QObject* parent = nullptr);
    void scan();
    void stop();

signals:
    void scanStarted();
    void entriesReady(const QVector<AutostartTelemetry>& entries, const QString& source);

protected:
    void run() override;
};

} // namespace orion::app
