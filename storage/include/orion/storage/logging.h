#pragma once

#include <QString>

namespace orion::storage {

[[nodiscard]] bool initializeFileLogging(
    const QString& path,
    QString* error = nullptr);
void installCrashHandler();
void shutdownFileLogging();
[[nodiscard]] QString activeLogFile();

} // namespace orion::storage
