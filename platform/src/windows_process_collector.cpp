#include "orion/platform/process_collector.h"

#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace orion::platform {
namespace {

[[nodiscard]] std::uint64_t fileTimeValue(const FILETIME value) noexcept
{
    ULARGE_INTEGER result {};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

[[nodiscard]] std::string utf8FromWide(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

struct PreviousCpu {
    std::uint64_t total100ns {0};
    std::chrono::steady_clock::time_point capturedAt;
    std::uint64_t creationIdentity {0};
};

class ThreadContextQuery final {
public:
    ThreadContextQuery()
    {
        if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS
            || PdhAddEnglishCounterW(
                query_, L"\\Thread(*)\\ID Process", 0, &processId_) != ERROR_SUCCESS
            || PdhAddEnglishCounterW(
                query_, L"\\Thread(*)\\Context Switches/sec", 0, &switches_) != ERROR_SUCCESS) {
            close();
            return;
        }
        valid_ = PdhCollectQueryData(query_) == ERROR_SUCCESS;
    }

    ~ThreadContextQuery() { close(); }
    ThreadContextQuery(const ThreadContextQuery&) = delete;
    ThreadContextQuery& operator=(const ThreadContextQuery&) = delete;

    [[nodiscard]] std::unordered_map<std::uint32_t, double> collect()
    {
        std::unordered_map<std::uint32_t, double> result;
        if (!valid_ || PdhCollectQueryData(query_) != ERROR_SUCCESS) return result;
        const auto pids = longValues(processId_);
        const auto rates = doubleValues(switches_);
        for (const auto& [name, pid] : pids) {
            const auto rate = rates.find(name);
            if (pid > 0 && rate != rates.end() && rate->second >= 0.0) {
                result[static_cast<std::uint32_t>(pid)] += rate->second;
            }
        }
        return result;
    }

private:
    [[nodiscard]] static bool usableStatus(const DWORD status) noexcept
    {
        return status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA;
    }

    [[nodiscard]] static std::unordered_map<std::wstring, LONG>
    longValues(const PDH_HCOUNTER counter)
    {
        DWORD bytes = 0;
        DWORD count = 0;
        if (PdhGetFormattedCounterArrayW(
                counter, PDH_FMT_LONG, &bytes, &count, nullptr)
                != static_cast<PDH_STATUS>(PDH_MORE_DATA)
            || bytes == 0) return {};
        std::vector<std::byte> storage(bytes);
        auto* values = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
        if (PdhGetFormattedCounterArrayW(
                counter, PDH_FMT_LONG, &bytes, &count, values) != ERROR_SUCCESS) return {};
        std::unordered_map<std::wstring, LONG> result;
        for (DWORD index = 0; index < count; ++index) {
            if (values[index].szName != nullptr && usableStatus(values[index].FmtValue.CStatus)) {
                result.emplace(values[index].szName, values[index].FmtValue.longValue);
            }
        }
        return result;
    }

    [[nodiscard]] static std::unordered_map<std::wstring, double>
    doubleValues(const PDH_HCOUNTER counter)
    {
        DWORD bytes = 0;
        DWORD count = 0;
        if (PdhGetFormattedCounterArrayW(
                counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr)
                != static_cast<PDH_STATUS>(PDH_MORE_DATA)
            || bytes == 0) return {};
        std::vector<std::byte> storage(bytes);
        auto* values = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
        if (PdhGetFormattedCounterArrayW(
                counter, PDH_FMT_DOUBLE, &bytes, &count, values) != ERROR_SUCCESS) return {};
        std::unordered_map<std::wstring, double> result;
        for (DWORD index = 0; index < count; ++index) {
            if (values[index].szName != nullptr && usableStatus(values[index].FmtValue.CStatus)) {
                result.emplace(values[index].szName, values[index].FmtValue.doubleValue);
            }
        }
        return result;
    }

    void close() noexcept
    {
        if (query_ != nullptr) PdhCloseQuery(query_);
        query_ = nullptr;
        valid_ = false;
    }

    PDH_HQUERY query_ {nullptr};
    PDH_HCOUNTER processId_ {nullptr};
    PDH_HCOUNTER switches_ {nullptr};
    bool valid_ {false};
};

class WindowsProcessCollector final : public ProcessCollector {
public:
    [[nodiscard]] orion::core::ProcessSnapshot sample() override
    {
        orion::core::ProcessSnapshot snapshot;
        snapshot.source = "Windows Tool Help / PSAPI";
        SYSTEM_INFO system {};
        GetSystemInfo(&system);
        snapshot.logicalProcessorCount = std::max<DWORD>(system.dwNumberOfProcessors, 1);

        const auto now = std::chrono::steady_clock::now();
        const auto contextSwitchRates = contextSwitchQuery_.collect();
        const HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (handle == INVALID_HANDLE_VALUE) {
            snapshot.reason = "CreateToolhelp32Snapshot failed";
            return snapshot;
        }
        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof(entry);
        std::unordered_map<std::uint32_t, PreviousCpu> nextPrevious;
        if (Process32FirstW(handle, &entry) != FALSE) {
            do {
                orion::core::ProcessInfo process;
                process.pid = entry.th32ProcessID;
                process.parentPid = entry.th32ParentProcessID;
                process.threadCount = entry.cntThreads;
                process.name = utf8FromWide(entry.szExeFile);
                if (process.name.empty()) {
                    process.name = "PID " + std::to_string(process.pid);
                }
                if (const auto rate = contextSwitchRates.find(process.pid);
                    rate != contextSwitchRates.end()) {
                    process.contextSwitchesPerSecond = rate->second;
                }

                const HANDLE processHandle = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                    FALSE,
                    process.pid);
                if (processHandle != nullptr) {
                    FILETIME created {};
                    FILETIME exited {};
                    FILETIME kernel {};
                    FILETIME user {};
                    if (GetProcessTimes(processHandle, &created, &exited, &kernel, &user) != FALSE) {
                        process.creationIdentity = fileTimeValue(created);
                        const std::uint64_t total = fileTimeValue(kernel) + fileTimeValue(user);
                        const auto previous = previousCpu_.find(process.pid);
                        if (previous != previousCpu_.end()
                            && previous->second.creationIdentity == process.creationIdentity) {
                            const double seconds = std::chrono::duration<double>(now - previous->second.capturedAt).count();
                            if (seconds > 0.01 && total >= previous->second.total100ns) {
                                process.cpuPercent = 100.0
                                    * static_cast<double>(total - previous->second.total100ns)
                                    / (seconds * 10'000'000.0);
                            }
                        }
                        nextPrevious.emplace(process.pid, PreviousCpu {total, now, process.creationIdentity});
                    }
                    PROCESS_MEMORY_COUNTERS_EX memory {};
                    memory.cb = sizeof(memory);
                    if (GetProcessMemoryInfo(
                            processHandle,
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                            sizeof(memory)) != FALSE) {
                        process.workingSetBytes = static_cast<std::uint64_t>(memory.WorkingSetSize);
                        process.privateBytes = static_cast<std::uint64_t>(memory.PrivateUsage);
                        process.pageFaultCount = static_cast<std::uint64_t>(memory.PageFaultCount);
                    }
                    DWORD handleCount = 0;
                    if (GetProcessHandleCount(processHandle, &handleCount) != FALSE) {
                        process.handleCount = static_cast<std::uint32_t>(handleCount);
                    }
                    IO_COUNTERS io {};
                    if (GetProcessIoCounters(processHandle, &io) != FALSE) {
                        process.readBytes = static_cast<std::uint64_t>(io.ReadTransferCount);
                        process.writeBytes = static_cast<std::uint64_t>(io.WriteTransferCount);
                    }
                    CloseHandle(processHandle);
                }
                snapshot.processes.push_back(std::move(process));
            } while (Process32NextW(handle, &entry) != FALSE);
        }
        CloseHandle(handle);
        previousCpu_ = std::move(nextPrevious);
        std::ranges::sort(snapshot.processes, [](const auto& left, const auto& right) {
            return left.name < right.name;
        });
        return snapshot;
    }

private:
    std::unordered_map<std::uint32_t, PreviousCpu> previousCpu_;
    ThreadContextQuery contextSwitchQuery_;
};

} // namespace

std::unique_ptr<ProcessCollector> makeProcessCollector()
{
    return std::make_unique<WindowsProcessCollector>();
}

} // namespace orion::platform
