#pragma once

#include <QString>

namespace orion::storage {

class AppPaths final {
public:
    explicit AppPaths(QString dataRoot = {});

    [[nodiscard]] const QString& dataRoot() const noexcept;
    [[nodiscard]] QString settingsFile() const;
    [[nodiscard]] QString logsDirectory() const;
    [[nodiscard]] QString logFile() const;
    [[nodiscard]] QString reportsDirectory() const;
    [[nodiscard]] QString dataFile(const QString& fileName) const;

    [[nodiscard]] bool ensureCreated(QString* error = nullptr) const;
    [[nodiscard]] bool migrateLegacyFile(
        const QString& legacyDirectory,
        const QString& fileName,
        QString* error = nullptr) const;

private:
    QString dataRoot_;
};

} // namespace orion::storage
