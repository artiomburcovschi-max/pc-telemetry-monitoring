#pragma once

#include "orion/storage/server_profile_store.h"

#include <QFrame>
#include <QHash>

class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace orion::app {

class ServerStatusChecker;

class ServerMonitorWidget final : public QFrame {
    Q_OBJECT

public:
    explicit ServerMonitorWidget(QString databasePath, QWidget* parent = nullptr);
    ~ServerMonitorWidget() override;

    void setGlobalPaused(bool paused);
    void shutdown();

private:
    struct ProfileForm {
        QLineEdit* label {nullptr};
        QLineEdit* host {nullptr};
        QSpinBox* port {nullptr};
        QLineEdit* login {nullptr};
        QLineEdit* secret {nullptr};
        QComboBox* databaseType {nullptr};
        QPushButton* save {nullptr};
    };

    [[nodiscard]] QWidget* createProfileForm(
        const QString& kind,
        const QString& title,
        ProfileForm& form);
    void saveProfile(const QString& kind, ProfileForm& form);
    void reloadProfiles();
    [[nodiscard]] qint64 selectedProfileId() const;
    void checkProfile(qint64 profileId);
    void applyCheckResult(qint64 profileId, bool online, double latencyMs, const QString& message);
    void checkSelected();
    void checkAll();
    void deleteSelected();
    void finishCheck(qint64 profileId, ServerStatusChecker* checker);

    orion::storage::ServerProfileStore store_;
    ProfileForm serverForm_;
    ProfileForm databaseForm_;
    QTableWidget* table_ {nullptr};
    QPushButton* checkSelectedButton_ {nullptr};
    QPushButton* checkAllButton_ {nullptr};
    QPushButton* deleteButton_ {nullptr};
    QHash<qint64, orion::storage::ServerProfile> profiles_;
    QHash<qint64, int> rows_;
    QHash<qint64, ServerStatusChecker*> activeChecks_;
    bool paused_ {false};
    bool shuttingDown_ {false};
};

} // namespace orion::app
