#include "orion/storage/logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <cstdlib>
#include <exception>
#include <memory>

namespace orion::storage {
namespace {

QMutex logMutex;
std::unique_ptr<QFile> logFile;
QString logPath;

[[nodiscard]] QString levelName(const QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARNING");
    case QtCriticalMsg:
        return QStringLiteral("CRITICAL");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

void messageHandler(
    const QtMsgType type,
    const QMessageLogContext&,
    const QString& message)
{
    QMutexLocker locker(&logMutex);
    if (logFile == nullptr || !logFile->isOpen()) {
        return;
    }
    const auto line = QStringLiteral("%1 [%2] %3\n")
                          .arg(
                              QDateTime::currentDateTime().toString(
                                  QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                              levelName(type),
                              message)
                          .toUtf8();
    logFile->write(line);
    logFile->flush();
}

} // namespace

bool initializeFileLogging(const QString& path, QString* error)
{
    shutdownFileLogging();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось создать каталог журнала");
        }
        return false;
    }
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (error != nullptr) {
            *error = file->errorString();
        }
        return false;
    }
    {
        QMutexLocker locker(&logMutex);
        logPath = path;
        logFile = std::move(file);
    }
    qInstallMessageHandler(messageHandler);
    return true;
}

void installCrashHandler()
{
    std::set_terminate([] {
        messageHandler(
            QtFatalMsg,
            QMessageLogContext {},
            QStringLiteral("Необработанное C++ исключение: std::terminate"));
        std::abort();
    });
}

void shutdownFileLogging()
{
    qInstallMessageHandler(nullptr);
    QMutexLocker locker(&logMutex);
    if (logFile != nullptr) {
        logFile->flush();
        logFile->close();
        logFile.reset();
    }
    logPath.clear();
}

QString activeLogFile()
{
    QMutexLocker locker(&logMutex);
    return logPath;
}

} // namespace orion::storage
