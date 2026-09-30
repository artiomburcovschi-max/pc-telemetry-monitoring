#include "orion/storage/app_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace orion::storage {

AppPaths::AppPaths(QString dataRoot)
{
    if (dataRoot.isEmpty()) {
        dataRoot = qEnvironmentVariable("ORION_DATA_DIR").trimmed();
    }
    if (dataRoot.isEmpty()) {
        dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (dataRoot.isEmpty()) {
        dataRoot = QDir::home().filePath(QStringLiteral(".orion"));
    }
    dataRoot_ = QDir::cleanPath(dataRoot);
}

const QString& AppPaths::dataRoot() const noexcept
{
    return dataRoot_;
}

QString AppPaths::settingsFile() const
{
    return dataFile(QStringLiteral("user_settings.json"));
}

QString AppPaths::logsDirectory() const
{
    return QDir(dataRoot_).filePath(QStringLiteral("logs"));
}

QString AppPaths::logFile() const
{
    return QDir(logsDirectory()).filePath(QStringLiteral("orion.log"));
}

QString AppPaths::reportsDirectory() const
{
    return QDir(dataRoot_).filePath(QStringLiteral("reports"));
}

QString AppPaths::dataFile(const QString& fileName) const
{
    return QDir(dataRoot_).filePath(fileName);
}

bool AppPaths::ensureCreated(QString* error) const
{
    QDir directory;
    for (const auto& path : {dataRoot_, logsDirectory(), reportsDirectory()}) {
        if (!directory.mkpath(path)) {
            if (error != nullptr) {
                *error = QStringLiteral("Не удалось создать каталог данных: %1").arg(path);
            }
            return false;
        }
    }
    return true;
}

bool AppPaths::migrateLegacyFile(
    const QString& legacyDirectory,
    const QString& fileName,
    QString* error) const
{
    const auto destination = dataFile(fileName);
    if (QFileInfo::exists(destination)) {
        return true;
    }
    const auto source = QDir(legacyDirectory).filePath(fileName);
    if (!QFileInfo::exists(source)) {
        return true;
    }
    if (!ensureCreated(error)) {
        return false;
    }
    if (!QFile::copy(source, destination)) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось перенести %1 в %2")
                         .arg(source, destination);
        }
        return false;
    }
    return true;
}

} // namespace orion::storage
