#pragma once

#include <QJsonObject>
#include <QThread>
#include <functional>

namespace orion::app {

class ProcessActionWorker final : public QThread {
    Q_OBJECT
public:
    using Executor = std::function<QJsonObject(quint32, quint64)>;
    explicit ProcessActionWorker(QObject* parent = nullptr);
    explicit ProcessActionWorker(Executor executor, QObject* parent = nullptr);
    bool startTermination(quint32 pid, quint64 creationIdentity, bool confirmed);
    static bool supported();
signals:
    void resultReady(const QJsonObject& result);
protected:
    void run() override;
private:
    static QJsonObject terminateNative(quint32 pid, quint64 creationIdentity);
    Executor executor_;
    quint32 pid_ {0};
    quint64 creationIdentity_ {0};
};

} // namespace orion::app
