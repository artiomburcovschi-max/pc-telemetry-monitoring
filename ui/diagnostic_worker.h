#pragma once

#include <QJsonObject>
#include <QMutex>
#include <QThread>

namespace orion::app {

class DiagnosticWorker final : public QThread {
    Q_OBJECT

public:
    explicit DiagnosticWorker(QObject* parent = nullptr);
    void scan(const QJsonObject& snapshot);
    void stop();

signals:
    void scanStarted();
    void reportReady(const QJsonObject& report);

protected:
    void run() override;

private:
    QMutex mutex_;
    QJsonObject snapshot_;
};

} // namespace orion::app
