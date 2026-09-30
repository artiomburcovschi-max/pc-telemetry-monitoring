#include "app_process_session.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <bit>
#include <map>
#ifndef Q_OS_WIN
#include <QFile>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace orion::app {
#ifdef Q_OS_WIN
namespace {
struct CloseHandleDeleter { void operator()(void* handle) const { if (handle) CloseHandle(handle); } };
using OwnedHandle = std::unique_ptr<void, CloseHandleDeleter>;
quint64 ticks(FILETIME value) { return (quint64(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
quint64 identity(HANDLE handle)
{
    FILETIME created{}, exited{}, kernel{}, user{};
    return GetProcessTimes(handle, &created, &exited, &kernel, &user) ? ticks(created) : 0;
}
bool safeToClose(HANDLE handle)
{
    using Query = BOOL (WINAPI*)(HANDLE, PBOOL);
    const auto query = std::bit_cast<Query>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsProcessCritical"));
    BOOL critical = TRUE;
    return query && query(handle, &critical) && !critical;
}
}
#else
namespace {
// Linux process identity: /proc/<pid>/stat field 22 (start time in clock ticks since
// boot). PID + start time is unique for the lifetime of a boot, which is what the
// Windows implementation gets from the process creation time.
struct ProcStat {
    quint64 start {0};   // 0 = unknown / process gone
    char state {'?'};
};

ProcStat readProcStat(const quint32 pid)
{
    QFile file(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray line = file.readAll();
    // comm (field 2) may contain spaces and parentheses; everything after the LAST
    // ')' is plain space-separated fields starting at field 3 (state).
    const auto close = line.lastIndexOf(')');
    if (close < 0) return {};
    const auto fields = line.mid(close + 1).simplified().split(' ');
    if (fields.size() <= 19 || fields.at(0).isEmpty()) return {};
    bool ok = false;
    const quint64 value = fields.at(19).toULongLong(&ok);
    if (!ok) return {};
    return {value, fields.at(0).at(0)};
}

quint64 readStartTicks(const quint32 pid) { return readProcStat(pid).start; }

// Still the same process AND still running. A zombie (Z) or dead (X) entry has exited
// and only awaits reaping - it must not count as alive, or "did the tree exit?" could
// never become true when PID 1 does not reap (for example a container without init).
bool isRunningSame(const quint32 pid, const quint64 expectedStart)
{
    const auto stat = readProcStat(pid);
    return expectedStart != 0 && stat.start == expectedStart && stat.state != 'Z' && stat.state != 'X';
}

bool refusedTarget(const quint32 pid)
{
    return pid <= 1 || pid == static_cast<quint32>(QCoreApplication::applicationPid());
}

// Sends `signal` to `pid` only if it is still the exact process that was recorded
// (same start time). Where the kernel supports pidfds the process is pinned FIRST and
// its identity verified afterwards, so a recycled PID can never receive the signal.
// Without pidfd support (kernel < 5.3 or a seccomp filter) it falls back to
// verify-then-kill(), which leaves a very small recycling window.
bool signalIfSameProcess(const quint32 pid, const quint64 expectedStart, const int signal)
{
    if (expectedStart == 0) return false;
#if defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    const long fd = ::syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0);
    if (fd >= 0) {
        const bool same = readStartTicks(pid) == expectedStart;
        const bool sent = same && ::syscall(SYS_pidfd_send_signal, static_cast<int>(fd), signal, nullptr, 0) == 0;
        ::close(static_cast<int>(fd));
        return sent;
    }
#endif
    if (readStartTicks(pid) != expectedStart) return false;
    return ::kill(static_cast<pid_t>(pid), signal) == 0;
}
}
#endif
struct AppProcessSession::State {
    quint32 root {0};
    QStringList errors;
#ifdef Q_OS_WIN
    struct Entry { OwnedHandle handle; quint64 created; };
    std::map<quint32, Entry> entries;
#else
    // Verified members of the launched tree: pid -> start time recorded when first seen.
    std::map<quint32, quint64> entries;
    std::optional<int> rootExit;   // cached once the root child has been reaped
#endif
    void error(const QString& message) { if (!errors.contains(message)) errors.append(message); }
};
AppProcessSession::AppProcessSession() : state_(std::make_unique<State>()) {}
AppProcessSession::~AppProcessSession()
{
#ifndef Q_OS_WIN
    // The root is our child: reap it if it already exited so it does not linger as a zombie.
    // Never wait or signal here - stopping observation must not affect the application.
    if (state_ && state_->root && !state_->rootExit) {
        int status = 0;
        ::waitpid(static_cast<pid_t>(state_->root), &status, WNOHANG);
    }
#endif
}
quint32 AppProcessSession::rootPid() const { return state_->root; }
QStringList AppProcessSession::errors() const { return state_->errors; }

bool AppProcessSession::launch(const QString& executablePath)
{
    if (state_->root != 0) return false;
    const QFileInfo file(executablePath);
    if (!file.isFile()) { state_->error(QStringLiteral("Выбранный исполняемый файл не существует.")); return false; }
#ifdef Q_OS_WIN
    const auto path = QDir::toNativeSeparators(file.absoluteFilePath()).toStdWString();
    const auto directory = QDir::toNativeSeparators(file.absolutePath()).toStdWString();
    auto command = L"\"" + path + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    // Keep verification helpers/background console processes from flashing windows.
    // GUI applications retain their normal ShowWindow behavior.
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process)) {
        state_->error(QStringLiteral("Windows отклонила запуск, код %1.").arg(GetLastError()));
        return false;
    }
    CloseHandle(process.hThread);
    state_->root = process.dwProcessId;
    const auto created = identity(process.hProcess);
    state_->entries.emplace(state_->root, State::Entry{OwnedHandle(process.hProcess), created});
    if (!created) state_->error(QStringLiteral("Не удалось прочитать время запуска корневого процесса; автозакрытие заблокировано."));
#else
    // The root is started as our own child (not QProcess::startDetached, which double-forks:
    // that would reparent it to init, hide its children from parent/child tracking and make
    // its exit status unobtainable). It gets its own session so it is independent of ORION's
    // terminal and process group. Everything the child needs is prepared before fork() because
    // only async-signal-safe calls are allowed between fork() and exec in a threaded process.
    const QByteArray path = QFile::encodeName(file.absoluteFilePath());
    const QByteArray directory = QFile::encodeName(file.absolutePath());
    int execError[2];
    if (::pipe2(execError, O_CLOEXEC) != 0) {
        state_->error(QStringLiteral("Операционная система отклонила запуск.")); return false;
    }
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(execError[0]); ::close(execError[1]);
        state_->error(QStringLiteral("Операционная система отклонила запуск.")); return false;
    }
    if (child == 0) {
        ::setsid();
        if (::chdir(directory.constData()) != 0) { /* keep the inherited directory */ }
        const int null = ::open("/dev/null", O_RDWR);
        if (null >= 0) {
            ::dup2(null, 0); ::dup2(null, 1); ::dup2(null, 2);
            if (null > 2) ::close(null);
        }
        char* const argv[] = {const_cast<char*>(path.constData()), nullptr};
        ::execv(path.constData(), argv);
        const int failure = errno;
        if (::write(execError[1], &failure, sizeof(failure)) < 0) { /* nothing more to do */ }
        ::_exit(127);
    }
    ::close(execError[1]);
    int execFailure = 0;
    const ssize_t got = ::read(execError[0], &execFailure, sizeof(execFailure));
    ::close(execError[0]);
    if (got > 0) { // exec failed in the child: reap it and report a rejected launch
        int status = 0;
        ::waitpid(child, &status, 0);
        state_->error(QStringLiteral("Операционная система отклонила запуск."));
        return false;
    }
    state_->root = static_cast<quint32>(child);
    const quint64 start = readStartTicks(state_->root);
    state_->entries.emplace(state_->root, start);
    if (!start) state_->error(QStringLiteral("Не удалось прочитать время запуска корневого процесса; автозакрытие заблокировано."));
#endif
    return true;
}

void AppProcessSession::observe(const orion::core::ProcessSnapshot& snapshot)
{
#ifdef Q_OS_WIN
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& process : snapshot.processes) {
            if (state_->entries.contains(process.pid)) continue;
            const auto parent = state_->entries.find(process.parentPid);
            if (parent == state_->entries.end()) continue;
            // Creation and exit bounds disallow descendants of a recycled parent PID.
            FILETIME created{}, exited{}, kernel{}, user{};
            if (!process.creationIdentity || !parent->second.created
                || process.creationIdentity < parent->second.created
                || !GetProcessTimes(parent->second.handle.get(), &created, &exited, &kernel, &user)
                || (ticks(exited) && process.creationIdentity > ticks(exited))) {
                state_->error(QStringLiteral("Не подтверждена принадлежность PID %1 дереву.").arg(process.pid));
                continue;
            }
            OwnedHandle handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, process.pid));
            if (!handle || identity(handle.get()) != process.creationIdentity) {
                state_->error(QStringLiteral("Недоступна идентификация дочернего PID %1.").arg(process.pid));
                continue;
            }
            state_->entries.emplace(process.pid, State::Entry{std::move(handle), process.creationIdentity});
            changed = true;
        }
    }
#else
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& process : snapshot.processes) {
            if (state_->entries.contains(process.pid)) continue;
            const auto parent = state_->entries.find(process.parentPid);
            if (parent == state_->entries.end()) continue;
            // The recorded parent must still be the SAME live process (matching start time) and
            // the child cannot be older than it; otherwise a recycled parent PID could adopt
            // unrelated processes into the tree. Identity is re-read from /proc rather than
            // trusted from the snapshot, like the Windows path does with the process handle.
            if (!process.creationIdentity || !parent->second
                || process.creationIdentity < parent->second
                || readStartTicks(process.parentPid) != parent->second) {
                state_->error(QStringLiteral("Не подтверждена принадлежность PID %1 дереву.").arg(process.pid));
                continue;
            }
            if (readStartTicks(process.pid) != process.creationIdentity) {
                state_->error(QStringLiteral("Недоступна идентификация дочернего PID %1.").arg(process.pid));
                continue;
            }
            state_->entries.emplace(process.pid, process.creationIdentity);
            changed = true;
        }
    }
#endif
}

QSet<quint32> AppProcessSession::alivePids() const
{
#ifdef Q_OS_WIN
    QSet<quint32> result;
    for (const auto& [pid, entry] : state_->entries)
        if (WaitForSingleObject(entry.handle.get(), 0) != WAIT_OBJECT_0) result.insert(pid);
    return result;
#else
    // Live, like the Windows handle wait: the worker polls this while waiting for exits.
    QSet<quint32> result;
    for (const auto& [pid, start] : state_->entries)
        if (isRunningSame(pid, start)) result.insert(pid);
    return result;
#endif
}
bool AppProcessSession::matches(const orion::core::ProcessInfo& process) const
{
#ifdef Q_OS_WIN
    const auto it = state_->entries.find(process.pid);
    return it != state_->entries.end() && process.creationIdentity != 0
        && it->second.created == process.creationIdentity;
#else
    const auto it = state_->entries.find(process.pid);
    return it != state_->entries.end() && process.creationIdentity != 0 && it->second == process.creationIdentity;
#endif
}
int AppProcessSession::requestClose(const std::function<bool()>& cancelled)
{
#ifdef Q_OS_WIN
    struct Context { QSet<quint32> pids; const std::function<bool()>* cancelled; int sent{0}; } context{{}, &cancelled};
    for (const auto pid : alivePids()) {
        if (cancelled()) break;
        const auto& entry = state_->entries.at(pid);
        if (entry.created && safeToClose(entry.handle.get())) context.pids.insert(pid);
        else state_->error(QStringLiteral("Не подтверждена безопасность закрытия PID %1.").arg(pid));
    }
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& current = *reinterpret_cast<Context*>(parameter);
        if ((*current.cancelled)()) return FALSE;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (current.pids.contains(pid) && IsWindowVisible(window)
            && PostMessageW(window, WM_CLOSE, 0, 0)) ++current.sent;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.sent;
#else
    // SIGTERM is the platform's standard cooperative shutdown request: well-behaved
    // applications handle it to save state and exit. Only members of the launched tree
    // whose identity still matches are signalled; anything that ignores it is handled by
    // forceTerminate() after the grace period.
    int sent = 0;
    for (const auto pid : alivePids()) {
        if (cancelled()) break;
        const auto entry = state_->entries.find(pid);
        if (entry == state_->entries.end() || !entry->second || refusedTarget(pid)) {
            state_->error(QStringLiteral("Не подтверждена безопасность закрытия PID %1.").arg(pid));
            continue;
        }
        if (signalIfSameProcess(pid, entry->second, SIGTERM)) ++sent;
    }
    return sent;
#endif
}
bool AppProcessSession::forceTerminate(quint32 pid)
{
#ifdef Q_OS_WIN
    const auto it = state_->entries.find(pid);
    if (it == state_->entries.end() || pid <= 4 || pid == QCoreApplication::applicationPid()) return false;
    OwnedHandle handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid));
    if (!handle || !it->second.created || identity(handle.get()) != it->second.created || !safeToClose(handle.get())) {
        state_->error(QStringLiteral("Отказ в безопасном завершении PID %1.").arg(pid)); return false;
    }
    if (WaitForSingleObject(handle.get(), 0) == WAIT_OBJECT_0) return false;
    if (!TerminateProcess(handle.get(), 1)) {
        state_->error(QStringLiteral("Windows не завершила PID %1, код %2.").arg(pid).arg(GetLastError()));
        return false;
    }
    return true;
#else
    const auto it = state_->entries.find(pid);
    if (it == state_->entries.end() || refusedTarget(pid) || !it->second) return false;
    if (!isRunningSame(pid, it->second)) return false; // already gone, or the PID now belongs to someone else
    if (!signalIfSameProcess(pid, it->second, SIGKILL)) {
        state_->error(QStringLiteral("Не удалось завершить PID %1.").arg(pid));
        return false;
    }
    return true;
#endif
}
std::optional<int> AppProcessSession::rootExitCode() const
{
#ifdef Q_OS_WIN
    const auto root = state_->entries.find(state_->root);
    if (root == state_->entries.end() || WaitForSingleObject(root->second.handle.get(), 0) != WAIT_OBJECT_0)
        return std::nullopt;
    DWORD code = 0;
    if (GetExitCodeProcess(root->second.handle.get(), &code)) return static_cast<int>(code);
#else
    if (state_->rootExit) return state_->rootExit;
    if (state_->root == 0) return std::nullopt;
    int status = 0;
    if (::waitpid(static_cast<pid_t>(state_->root), &status, WNOHANG) == static_cast<pid_t>(state_->root)) {
        // Shell convention: a process killed by signal N is reported as 128 + N.
        state_->rootExit = WIFEXITED(status) ? WEXITSTATUS(status)
            : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        return state_->rootExit;
    }
#endif
    return std::nullopt;
}
}
