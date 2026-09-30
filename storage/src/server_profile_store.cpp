#include "orion/storage/server_profile_store.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <atomic>
#include <utility>

namespace orion::storage {
namespace {

std::atomic<quint64> connectionCounter {0};

template<typename Operation>
bool withDatabase(const QString& path, QString* error, Operation&& operation)
{
    const QString connectionName = QStringLiteral("orion_server_profiles_%1")
        .arg(++connectionCounter);
    bool success = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        if (!database.open()) {
            if (error != nullptr) {
                *error = database.lastError().text();
            }
        } else {
            success = std::forward<Operation>(operation)(database);
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return success;
}

void setQueryError(const QSqlQuery& query, QString* error)
{
    if (error != nullptr) {
        *error = query.lastError().text();
    }
}

} // namespace

ServerProfileStore::ServerProfileStore(QString databasePath)
    : databasePath_(QDir::cleanPath(std::move(databasePath)))
{
}

const QString& ServerProfileStore::databasePath() const noexcept
{
    return databasePath_;
}

bool ServerProfileStore::ensureSchema(QString* error) const
{
    const QFileInfo info(databasePath_);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось создать каталог профилей: %1")
                .arg(info.absolutePath());
        }
        return false;
    }
    return withDatabase(databasePath_, error, [error](QSqlDatabase& database) {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS profiles (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                kind TEXT NOT NULL,
                label TEXT NOT NULL,
                host TEXT NOT NULL,
                port INTEGER NOT NULL,
                login TEXT,
                secret TEXT,
                db_type TEXT DEFAULT ''
            )
        )"))) {
            setQueryError(query, error);
            return false;
        }
        return true;
    });
}

QVector<ServerProfile> ServerProfileStore::listProfiles(
    const QString& kind,
    QString* error) const
{
    QVector<ServerProfile> profiles;
    if (!ensureSchema(error)) {
        return profiles;
    }
    const bool ok = withDatabase(databasePath_, error,
        [&profiles, &kind, error](QSqlDatabase& database) {
            QSqlQuery query(database);
            if (kind.isEmpty()) {
                query.prepare(QStringLiteral(
                    "SELECT id, kind, label, host, port, login, secret, db_type "
                    "FROM profiles ORDER BY kind, label COLLATE NOCASE"));
            } else {
                query.prepare(QStringLiteral(
                    "SELECT id, kind, label, host, port, login, secret, db_type "
                    "FROM profiles WHERE kind = ? ORDER BY label COLLATE NOCASE"));
                query.addBindValue(kind);
            }
            if (!query.exec()) {
                setQueryError(query, error);
                return false;
            }
            while (query.next()) {
                profiles.append(ServerProfile {
                    query.value(0).toLongLong(),
                    query.value(1).toString(),
                    query.value(2).toString(),
                    query.value(3).toString(),
                    query.value(4).toInt(),
                    query.value(5).toString(),
                    query.value(6).toString(),
                    query.value(7).toString(),
                });
            }
            return true;
        });
    if (!ok) {
        profiles.clear();
    }
    return profiles;
}

bool ServerProfileStore::saveProfile(ServerProfile& profile, QString* error) const
{
    if (profile.kind != QStringLiteral("server")
        && profile.kind != QStringLiteral("database")) {
        if (error != nullptr) *error = QStringLiteral("Неизвестный тип профиля");
        return false;
    }
    if (profile.label.trimmed().isEmpty() || profile.host.trimmed().isEmpty()
        || profile.port < 1 || profile.port > 65535) {
        if (error != nullptr) *error = QStringLiteral("Проверьте название, хост и порт профиля");
        return false;
    }
    if (!ensureSchema(error)) {
        return false;
    }
    return withDatabase(databasePath_, error, [&profile, error](QSqlDatabase& database) {
        QSqlQuery query(database);
        if (profile.id < 0) {
            query.prepare(QStringLiteral(
                "INSERT INTO profiles (kind, label, host, port, login, secret, db_type) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)"));
        } else {
            query.prepare(QStringLiteral(
                "UPDATE profiles SET kind=?, label=?, host=?, port=?, login=?, secret=?, db_type=? "
                "WHERE id=?"));
        }
        query.addBindValue(profile.kind);
        query.addBindValue(profile.label.trimmed());
        query.addBindValue(profile.host.trimmed());
        query.addBindValue(profile.port);
        query.addBindValue(profile.login.trimmed());
        query.addBindValue(profile.secret);
        query.addBindValue(profile.databaseType);
        if (profile.id >= 0) {
            query.addBindValue(profile.id);
        }
        if (!query.exec()) {
            setQueryError(query, error);
            return false;
        }
        if (profile.id < 0) {
            profile.id = query.lastInsertId().toLongLong();
        }
        return true;
    });
}

bool ServerProfileStore::deleteProfile(const qint64 profileId, QString* error) const
{
    if (!ensureSchema(error)) {
        return false;
    }
    return withDatabase(databasePath_, error, [profileId, error](QSqlDatabase& database) {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("DELETE FROM profiles WHERE id = ?"));
        query.addBindValue(profileId);
        if (!query.exec()) {
            setQueryError(query, error);
            return false;
        }
        return true;
    });
}

} // namespace orion::storage
