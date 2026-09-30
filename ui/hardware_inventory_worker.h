#pragma once

#include <QJsonObject>
#include <QMutex>
#include <QThread>
#include <functional>

namespace orion::app {

class HardwareInventoryWorker final : public QThread {
    Q_OBJECT

public:
    explicit HardwareInventoryWorker(QObject* parent = nullptr);

    void scan(const QJsonObject& seed);
    void stop();
    // Synchronous entry for a caller already running outside the GUI thread.
    [[nodiscard]] QJsonObject collectReport(const QJsonObject& seed,
        const std::function<bool()>& externalCancellation = {});

    [[nodiscard]] static QString memoryTypeName(int smbiosType);
    [[nodiscard]] static QString normaliseFirmwareDate(const QString& value);
    [[nodiscard]] static bool isNpuDeviceName(const QString& name);
    [[nodiscard]] static QJsonObject ratePc(const QJsonObject& report);

signals:
    void scanStarted();
    void progressChanged(int completed, int total, const QString& label);
    void reportReady(const QJsonObject& report);

protected:
    void run() override;

private:

    QMutex mutex_;
    QJsonObject seed_;
};

} // namespace orion::app
