#include "process_action_worker.h"

#include <QCoreApplication>
#include <bit>
#include <exception>
#include <memory>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <QFile>
#include <QThread>
#include <cerrno>
#include <csignal>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace orion::app {
namespace {
QJsonObject result(const QString& code, const QString& message)
{
    return {{"code", code}, {"message", message},
            {"ok", code == "terminated" || code == "already_exited"}};
}
}

ProcessActionWorker::ProcessActionWorker(QObject* parent)
    : ProcessActionWorker(terminateNative, parent) {}

ProcessActionWorker::ProcessActionWorker(Executor executor, QObject* parent)
    : QThread(parent), executor_(std::move(executor))
{
    qRegisterMetaType<QJsonObject>();
}

bool ProcessActionWorker::supported()
{
    return true;
}

bool ProcessActionWorker::startTermination(quint32 pid, quint64 creationIdentity, bool confirmed)
{
    if (!confirmed || isRunning() || !executor_ || pid <= 4 || creationIdentity == 0
        || pid == static_cast<quint32>(QCoreApplication::applicationPid()))
        return false;
    pid_ = pid;
    creationIdentity_ = creationIdentity;
    start();
    return true;
}

void ProcessActionWorker::run()
{
    QJsonObject report;
    try {
        report = executor_(pid_, creationIdentity_);
    } catch (const std::exception& error) {
        report = result("error", QString::fromUtf8(error.what()));
    } catch (...) {
        report = result("error", QStringLiteral("Не удалось завершить процесс."));
    }
    report.insert("pid", static_cast<qint64>(pid_));
    emit resultReady(report);
}

QJsonObject ProcessActionWorker::terminateNative(quint32 pid, quint64 creationIdentity)
{
#ifdef Q_OS_WIN
    const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                                      FALSE, pid);
    if (!handle) {
        const DWORD error = GetLastError();
        if (error == ERROR_INVALID_PARAMETER)
            return result("already_exited", QStringLiteral("Процесс уже завершился."));
        if (error == ERROR_ACCESS_DENIED)
            return result("access_denied", QStringLiteral("Windows отказала в доступе к процессу. Возможно, недостаточно прав."));
        return result("error", QStringLiteral("Не удалось открыть процесс. Код Windows: %1.").arg(error));
    }
    const auto closeHandle = [](void* value) { CloseHandle(value); };
    std::unique_ptr<void, decltype(closeHandle)> owned(handle, closeHandle);
    if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0)
        return result("already_exited", QStringLiteral("Процесс уже завершился."));
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(handle, &created, &exited, &kernel, &user))
        return result("identity_unavailable", QStringLiteral("Не удалось подтвердить выбранный процесс. Обновите список."));
    const quint64 identity = (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    if (identity != creationIdentity)
        return result("identity_changed", QStringLiteral("Выбранный процесс уже сменился. Обновите список и выберите его заново."));
    BOOL critical = FALSE;
    using CriticalQuery = BOOL (WINAPI*)(HANDLE, PBOOL);
    const auto queryCritical = std::bit_cast<CriticalQuery>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsProcessCritical"));
    if (!queryCritical || !queryCritical(handle, &critical))
        return result("identity_unavailable", QStringLiteral("Не удалось проверить системный статус процесса. Завершение отменено."));
    if (critical)
        return result("protected", QStringLiteral("Это критический системный процесс. Его завершение запрещено."));
    // Keep the same verified handle throughout: a reused PID cannot target another process.
    if (!TerminateProcess(handle, 1)) {
        const DWORD error = GetLastError();
        if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0)
            return result("already_exited", QStringLiteral("Процесс уже завершился."));
        return result("error", QStringLiteral("Windows не смогла завершить процесс. Код: %1.").arg(error));
    }
    if (WaitForSingleObject(handle, 3000) != WAIT_OBJECT_0)
        return result("pending", QStringLiteral("Запрос завершения отправлен; процесс ещё не подтвердил выход. Обновите список."));
    return result("terminated", QStringLiteral("Выбранный процесс завершён."));
#else
    // Linux stand-in for the Windows handle: identity is /proc/<pid>/stat field 22
    // (start time in clock ticks since boot). It is re-read immediately before the
    // signal too, so a PID recycled between the check and the send still cannot be
    // targeted — mirroring the "keep the same verified handle" guarantee above.
    const auto readStart = [](quint32 processId) -> quint64 {
        QFile file(QStringLiteral("/proc/%1/stat").arg(processId));
        if (!file.open(QIODevice::ReadOnly)) return 0;
        const QByteArray line = file.readAll();
        const auto close = line.lastIndexOf(')');
        if (close < 0) return 0;
        const auto fields = line.mid(close + 1).simplified().split(' ');
        if (fields.size() <= 19 || fields.at(0).isEmpty() || fields.at(0) == "Z"
            || fields.at(0) == "X")
            return 0; // zombie/dead counts the same as "gone"
        bool ok = false;
        const quint64 value = fields.at(19).toULongLong(&ok);
        return ok ? value : 0;
    };
    const quint64 currentIdentity = readStart(pid);
    if (currentIdentity == 0)
        return result("already_exited", QStringLiteral("Процесс уже завершился."));
    if (currentIdentity != creationIdentity)
        return result("identity_changed", QStringLiteral("Выбранный процесс уже сменился. Обновите список и выберите его заново."));
    bool sent = false;
    int failure = 0;
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    const long fd = ::syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0);
    if (fd >= 0) {
        if (readStart(pid) == creationIdentity) {
            sent = ::syscall(SYS_pidfd_send_signal, static_cast<int>(fd), SIGKILL, nullptr, 0) == 0;
            failure = errno;
        }
        ::close(static_cast<int>(fd));
    } else
#endif
    {
        if (readStart(pid) == creationIdentity) {
            sent = ::kill(static_cast<pid_t>(pid), SIGKILL) == 0;
            failure = errno;
        }
    }
    if (!sent) {
        if (readStart(pid) != creationIdentity)
            return result("already_exited", QStringLiteral("Процесс уже завершился."));
        if (failure == EPERM)
            return result("access_denied", QStringLiteral("Недостаточно прав для завершения процесса."));
        return result("error", QStringLiteral("Не удалось завершить процесс. Код ошибки: %1.").arg(failure));
    }
    // Give it a brief moment to actually exit, mirroring the WaitForSingleObject step above.
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (readStart(pid) != creationIdentity)
            return result("terminated", QStringLiteral("Выбранный процесс завершён."));
        QThread::msleep(100);
    }
    return result("pending", QStringLiteral("Запрос завершения отправлен; процесс ещё не подтвердил выход. Обновите список."));
#endif
}

} // namespace orion::app
