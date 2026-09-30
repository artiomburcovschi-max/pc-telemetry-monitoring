#pragma once

#include <QString>
#include <QVector>

namespace orion::storage {

struct ServerProfile {
    qint64 id {-1};
    QString kind;
    QString label;
    QString host;
    int port {0};
    QString login;
    QString secret;
    QString databaseType;
};

class ServerProfileStore final {
public:
    explicit ServerProfileStore(QString databasePath);

    [[nodiscard]] const QString& databasePath() const noexcept;
    [[nodiscard]] bool ensureSchema(QString* error = nullptr) const;
    [[nodiscard]] QVector<ServerProfile> listProfiles(
        const QString& kind = {},
        QString* error = nullptr) const;
    [[nodiscard]] bool saveProfile(ServerProfile& profile, QString* error = nullptr) const;
    [[nodiscard]] bool deleteProfile(qint64 profileId, QString* error = nullptr) const;

private:
    QString databasePath_;
};

} // namespace orion::storage
