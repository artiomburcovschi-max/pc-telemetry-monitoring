#include "orion/platform/system_backend.h"

#include "disk_collector.h"
#include "gpu_collector.h"
#include "sensor_collector.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <psapi.h>
#include <winternl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace orion::platform {
namespace {

struct CpuTimes {
    std::uint64_t idle {0};
    std::uint64_t total {0};
    bool valid {false};
};

struct ProcessorTimes {
    std::uint64_t idle {0};
    std::uint64_t kernel {0};
    std::uint64_t user {0};
};

struct SystemProcessorPerformanceInformation {
    LARGE_INTEGER idleTime;
    LARGE_INTEGER kernelTime;
    LARGE_INTEGER userTime;
    LARGE_INTEGER dpcTime;
    LARGE_INTEGER interruptTime;
    ULONG interruptCount;
};

struct NetworkCounters {
    std::string scope;
    std::uint64_t received {0};
    std::uint64_t sent {0};
    std::uint64_t errors {0};
    std::uint64_t drops {0};
    bool valid {false};
};

struct CommitCounters {
    std::uint64_t usedBytes {0};
    std::uint64_t limitBytes {0};
    std::uint64_t peakBytes {0};
    bool valid {false};
};

struct PagefileCounters {
    std::uint64_t totalPages {0};
    std::uint64_t usedPages {0};
    std::uint64_t peakPages {0};
    bool valid {false};
};

[[nodiscard]] std::uint64_t fileTimeValue(const FILETIME& value) noexcept
{
    ULARGE_INTEGER converted {};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

[[nodiscard]] CpuTimes readCpuTimes() noexcept
{
    FILETIME idle {};
    FILETIME kernel {};
    FILETIME user {};
    if (!GetSystemTimes(&idle, &kernel, &user)) {
        return {};
    }
    return {
        fileTimeValue(idle),
        fileTimeValue(kernel) + fileTimeValue(user),
        true,
    };
}

// Custom structure that works with newer Windows SDK versions
struct SYSTEM_PERFORMANCE_INFORMATION_COMPATIBLE {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
    ULONG ReadOperationCount;
    ULONG WriteOperationCount;
    ULONG OtherOperationCount;
    ULONG AvailablePages;
    ULONG TotalPages;
    ULONG TotalSystemHandles;
    ULONG TotalSystemThreads;
    ULONG SystemCallCount;
    ULONG ContextSwitchCount;
    LARGE_INTEGER SystemCallTime;
    ULONG InterruptCount;
    ULONG InterruptTime;
    ULONG DpcCount;
    ULONG DpcTime;
    ULONG DpcRequestRate;
    ULONG TimeoutCount;
    ULONG AlignmentFixupCount;
    ULONG ExceptionDispatchCount;
    ULONG FloatingEmulationCount;
    ULONG ByteOperationCount;
};

[[nodiscard]] std::optional<SYSTEM_PERFORMANCE_INFORMATION_COMPATIBLE>
readSystemPerformance() noexcept
{
    using QuerySystemInformation = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto procedure = module == nullptr
        ? nullptr : GetProcAddress(module, "NtQuerySystemInformation");
    QuerySystemInformation query = nullptr;
    static_assert(sizeof(query) == sizeof(procedure));
    std::memcpy(&query, &procedure, sizeof(query));
    if (query == nullptr) return std::nullopt;

    SYSTEM_PERFORMANCE_INFORMATION_COMPATIBLE info {};
    ULONG returned = 0;
    constexpr ULONG systemPerformanceInformation = 2;
    if (query(systemPerformanceInformation, &info, sizeof(info), &returned) < 0) {
        return std::nullopt;
    }
    return info;
}

[[nodiscard]] CommitCounters readCommitCounters() noexcept
{
    PERFORMANCE_INFORMATION info {};
    info.cb = sizeof(info);
    if (GetPerformanceInfo(&info, sizeof(info)) == FALSE || info.PageSize == 0) {
        return {};
    }
    return {
        static_cast<std::uint64_t>(info.CommitTotal) * info.PageSize,
        static_cast<std::uint64_t>(info.CommitLimit) * info.PageSize,
        static_cast<std::uint64_t>(info.CommitPeak) * info.PageSize,
        true,
    };
}

BOOL CALLBACK collectPagefile(
    LPVOID context,
    PENUM_PAGE_FILE_INFORMATION info,
    LPCWSTR)
{
    auto* counters = static_cast<PagefileCounters*>(context);
    if (counters == nullptr || info == nullptr) return FALSE;
    counters->totalPages += info->TotalSize;
    counters->usedPages += info->TotalInUse;
    counters->peakPages += info->PeakUsage;
    counters->valid = true;
    return TRUE;
}

[[nodiscard]] PagefileCounters readPagefileCounters() noexcept
{
    PagefileCounters counters;
    if (EnumPageFilesW(collectPagefile, &counters) == FALSE) {
        counters.valid = false;
    }
    return counters;
}

[[nodiscard]] std::optional<double> cpuPercent(
    const CpuTimes& before,
    const CpuTimes& after) noexcept
{
    if (!before.valid || !after.valid
        || after.total <= before.total || after.idle < before.idle) {
        return std::nullopt;
    }
    const auto totalDelta = after.total - before.total;
    const auto idleDelta = after.idle - before.idle;
    if (idleDelta >= totalDelta) {
        return 0.0;
    }
    return std::clamp(
        100.0 * static_cast<double>(totalDelta - idleDelta)
            / static_cast<double>(totalDelta),
        0.0,
        100.0);
}

[[nodiscard]] std::vector<ProcessorTimes> readProcessorTimes()
{
    using QuerySystemInformation = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
    constexpr ULONG processorPerformanceInformation = 8;
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto procedure = module == nullptr
        ? nullptr
        : GetProcAddress(module, "NtQuerySystemInformation");
    QuerySystemInformation query = nullptr;
    static_assert(sizeof(query) == sizeof(procedure));
    std::memcpy(&query, &procedure, sizeof(query));
    if (query == nullptr) {
        return {};
    }

    const auto processorCount = std::max<DWORD>(
        GetActiveProcessorCount(ALL_PROCESSOR_GROUPS), 1);
    std::vector<SystemProcessorPerformanceInformation> nativeTimes(processorCount);
    ULONG returnedBytes = 0;
    const auto status = query(
        processorPerformanceInformation,
        nativeTimes.data(),
        static_cast<ULONG>(nativeTimes.size() * sizeof(nativeTimes.front())),
        &returnedBytes);
    if (status < 0 || returnedBytes < sizeof(nativeTimes.front())) {
        return {};
    }

    const auto returnedCount = std::min<std::size_t>(
        returnedBytes / sizeof(nativeTimes.front()), nativeTimes.size());
    std::vector<ProcessorTimes> result;
    result.reserve(returnedCount);
    for (std::size_t index = 0; index < returnedCount; ++index) {
        result.push_back({
            static_cast<std::uint64_t>(nativeTimes[index].idleTime.QuadPart),
            static_cast<std::uint64_t>(nativeTimes[index].kernelTime.QuadPart),
            static_cast<std::uint64_t>(nativeTimes[index].userTime.QuadPart),
        });
    }
    return result;
}

[[nodiscard]] NetworkCounters readNetworkCounters() noexcept
{
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || table == nullptr) {
        return {};
    }

    NetworkCounters counters;
    std::vector<std::uint64_t> interfaces;
    for (ULONG index = 0; index < table->NumEntries; ++index) {
        const auto& row = table->Table[index];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK
            || row.OperStatus != IfOperStatusUp) {
            continue;
        }
        counters.received += row.InOctets;
        interfaces.push_back(row.InterfaceLuid.Value);
        counters.sent += row.OutOctets;
        counters.errors += row.InErrors + row.OutErrors;
        counters.drops += row.InDiscards + row.OutDiscards;
    }
    std::sort(interfaces.begin(), interfaces.end());
    for (const auto identity : interfaces) counters.scope += std::to_string(identity) + ";";
    counters.valid = true;
    FreeMibTable(table);
    return counters;
}

[[nodiscard]] std::string utf8FromWide(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

[[nodiscard]] std::string readCpuName()
{
    wchar_t value[256] {};
    DWORD size = sizeof(value);
    const auto status = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString",
        RRF_RT_REG_SZ,
        nullptr,
        value,
        &size);
    return status == ERROR_SUCCESS ? utf8FromWide(value) : "Unknown CPU";
}

[[nodiscard]] std::optional<double> readCpuFrequencyMhz()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    const auto status = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"~MHz",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    return status == ERROR_SUCCESS && value > 0
        ? std::optional {static_cast<double>(value)}
        : std::nullopt;
}

[[nodiscard]] std::vector<double> readCpuFrequenciesMhz()
{
    const auto processorCount = std::max<DWORD>(
        GetActiveProcessorCount(ALL_PROCESSOR_GROUPS), 1);
    std::vector<double> result;
    result.reserve(processorCount);
    for (DWORD index = 0; index < processorCount; ++index) {
        const auto key = std::wstring(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\")
            + std::to_wstring(index);
        DWORD value = 0;
        DWORD size = sizeof(value);
        const auto status = RegGetValueW(
            HKEY_LOCAL_MACHINE,
            key.c_str(),
            L"~MHz",
            RRF_RT_REG_DWORD,
            nullptr,
            &value,
            &size);
        if (status != ERROR_SUCCESS || value == 0) {
            return {};
        }
        result.push_back(static_cast<double>(value));
    }
    return result;
}

class PerCoreQuery final {
public:
    PerCoreQuery() : before_(readProcessorTimes()) {}

    PerCoreQuery(const PerCoreQuery&) = delete;
    PerCoreQuery& operator=(const PerCoreQuery&) = delete;

    [[nodiscard]] std::vector<orion::core::CpuCoreLoad> collect()
    {
        std::vector<orion::core::CpuCoreLoad> result;
        const auto after = readProcessorTimes();
        if (before_.empty() || after.size() != before_.size()) {
            return result;
        }
        result.reserve(after.size());
        for (std::size_t index = 0; index < after.size(); ++index) {
            const auto kernelDelta = after[index].kernel - before_[index].kernel;
            const auto userDelta = after[index].user - before_[index].user;
            const auto idleDelta = after[index].idle - before_[index].idle;
            const auto totalDelta = kernelDelta + userDelta;
            const auto busyDelta = totalDelta > idleDelta ? totalDelta - idleDelta : 0;
            const auto load = totalDelta > 0
                ? 100.0 * static_cast<double>(busyDelta)
                    / static_cast<double>(totalDelta)
                : 0.0;
            result.push_back({index + 1, std::clamp(load, 0.0, 100.0)});
        }
        return result;
    }

private:
    std::vector<ProcessorTimes> before_;
};

class WindowsSystemBackend final : public SystemBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "windows";
    }

    [[nodiscard]] orion::core::TelemetryData sample(
        const std::chrono::milliseconds interval) override
    {
        const auto safeInterval = std::max(interval, std::chrono::milliseconds {50});
        PerCoreQuery perCoreQuery;
        const auto cpuBefore = readCpuTimes();
        const auto networkBefore = readNetworkCounters();
        const auto performanceBefore = readSystemPerformance();
        const auto disksBefore = detail::readDiskCounterSamples();
        const auto started = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(safeInterval);
        const auto cpuAfter = readCpuTimes();
        const auto coreLoads = perCoreQuery.collect();
        const auto networkAfter = readNetworkCounters();
        const auto performanceAfter = readSystemPerformance();
        const auto disksAfter = detail::readDiskCounterSamples();
        const auto gpu = detail::readGpuSample();
        const auto sensors = detail::readSensorSnapshot(gpu);
        const double elapsed = std::max(
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
            0.001);

        MEMORYSTATUSEX memory {};
        memory.dwLength = sizeof(memory);
        const bool hasMemory = GlobalMemoryStatusEx(&memory) != FALSE;
        const auto commit = readCommitCounters();
        const auto pagefile = readPagefileCounters();

        orion::core::TelemetryData data;
        data.collectedAt = std::chrono::system_clock::now();
        const auto cpuUsage = cpuPercent(cpuBefore, cpuAfter);
        data.cpuUsagePercent = cpuUsage.has_value()
            ? orion::core::Metric<double>::valid(
                *cpuUsage, "GetSystemTimes", data.collectedAt)
            : orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::CollectorError,
                "GetSystemTimes",
                "Windows CPU counters were unavailable or did not advance");
        data.cpuName = readCpuName();
        if (const auto* cpuTemperature = detail::componentTemperature(
                sensors, orion::core::SensorComponent::Cpu);
            cpuTemperature != nullptr) {
            data.cpuTemperatureC = orion::core::Metric<double>::valid(
                cpuTemperature->valueC, cpuTemperature->source, data.collectedAt);
        } else {
            data.cpuTemperatureC = orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::Unsupported,
                sensors.source,
                sensors.reason);
        }
        if (!coreLoads.empty()) {
            data.cpuCores = orion::core::Metric<std::vector<orion::core::CpuCoreLoad>>::valid(
                coreLoads,
                "NtQuerySystemInformation processor times",
                data.collectedAt);
        } else {
            data.cpuCores = orion::core::Metric<std::vector<orion::core::CpuCoreLoad>>::unavailable(
                orion::core::DataQuality::CollectorError,
                "NtQuerySystemInformation",
                "Per-core performance counters are unavailable");
        }
        const auto frequencyMhz = readCpuFrequencyMhz();
        const auto coreFrequenciesMhz = readCpuFrequenciesMhz();
        if (!coreFrequenciesMhz.empty()) {
            data.cpuCoreFrequenciesMhz =
                orion::core::Metric<std::vector<double>>::estimated(
                    coreFrequenciesMhz,
                    "Windows per-processor registry",
                    "Registry frequencies are point estimates",
                    data.collectedAt);
        }
        if (frequencyMhz.has_value()) {
            data.cpuFrequencyMhz = orion::core::Metric<double>::estimated(
                *frequencyMhz,
                "Windows processor registry",
                "Registry frequency is a point estimate, not a time-window average",
                data.collectedAt);
        }
        data.osName = "Windows";
        data.gpuName = gpu.name;
        data.gpuUsagePercent = gpu.usagePercent.has_value()
            ? orion::core::Metric<double>::valid(*gpu.usagePercent, gpu.source, data.collectedAt)
            : orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::Unsupported, gpu.source, gpu.reason);
        data.gpuTemperatureC = gpu.temperatureC.has_value()
            ? orion::core::Metric<double>::valid(*gpu.temperatureC, gpu.source, data.collectedAt)
            : orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::Unsupported, gpu.source, gpu.reason);
        if (!data.gpuTemperatureC.usable()) {
            if (const auto* gpuTemperature = detail::componentTemperature(
                    sensors, orion::core::SensorComponent::Gpu);
                gpuTemperature != nullptr) {
                data.gpuTemperatureC = orion::core::Metric<double>::valid(
                    gpuTemperature->valueC, gpuTemperature->source, data.collectedAt);
            }
        }
        data.temperatures = !sensors.temperatures.empty()
            ? orion::core::Metric<std::vector<orion::core::TemperatureSensor>>::valid(
                sensors.temperatures, sensors.source, data.collectedAt)
            : orion::core::Metric<std::vector<orion::core::TemperatureSensor>>::unavailable(
                orion::core::DataQuality::Unsupported, sensors.source, sensors.reason);
        data.fans = !sensors.fans.empty()
            ? orion::core::Metric<std::vector<orion::core::FanSensor>>::valid(
                sensors.fans, sensors.source, data.collectedAt)
            : orion::core::Metric<std::vector<orion::core::FanSensor>>::unavailable(
                orion::core::DataQuality::Unsupported, sensors.source, sensors.reason);
        if (gpu.memoryTotalBytes.has_value()) {
            data.gpuMemoryTotalMb = orion::core::Metric<double>::valid(
                static_cast<double>(*gpu.memoryTotalBytes) / (1024.0 * 1024.0),
                gpu.source,
                data.collectedAt);
        }
        if (gpu.memoryUsedBytes.has_value()) {
            data.gpuMemoryUsedMb = orion::core::Metric<double>::valid(
                static_cast<double>(*gpu.memoryUsedBytes) / (1024.0 * 1024.0),
                gpu.source,
                data.collectedAt);
        }
        if (gpu.memoryTotalBytes.has_value() && gpu.memoryUsedBytes.has_value()
            && *gpu.memoryTotalBytes > 0) {
            data.gpuMemoryPercent = orion::core::Metric<double>::valid(
                100.0 * static_cast<double>(*gpu.memoryUsedBytes)
                    / static_cast<double>(*gpu.memoryTotalBytes),
                gpu.source,
                data.collectedAt);
        }

        if (hasMemory) {
            data.ramUsagePercent = orion::core::Metric<double>::valid(
                static_cast<double>(memory.dwMemoryLoad),
                "GlobalMemoryStatusEx",
                data.collectedAt);
            data.ramTotalBytes = orion::core::Metric<std::uint64_t>::valid(
                memory.ullTotalPhys,
                "GlobalMemoryStatusEx",
                data.collectedAt);
            if (memory.ullTotalPhys > 0) {
                data.ramAvailablePercent = orion::core::Metric<double>::valid(
                    100.0 * static_cast<double>(memory.ullAvailPhys)
                        / static_cast<double>(memory.ullTotalPhys),
                    "GlobalMemoryStatusEx",
                    data.collectedAt);
            }
        } else {
            data.ramUsagePercent = orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::CollectorError,
                "GlobalMemoryStatusEx",
                "Physical memory counters are unavailable");
            data.ramTotalBytes = orion::core::Metric<std::uint64_t>::unavailable(
                orion::core::DataQuality::CollectorError,
                "GlobalMemoryStatusEx",
                "Physical memory counters are unavailable");
        }
        if (commit.valid) {
            data.commitUsedBytes = orion::core::Metric<std::uint64_t>::valid(
                commit.usedBytes, "GetPerformanceInfo", data.collectedAt);
            data.commitLimitBytes = orion::core::Metric<std::uint64_t>::valid(
                commit.limitBytes, "GetPerformanceInfo", data.collectedAt);
            data.commitPeakBytes = orion::core::Metric<std::uint64_t>::valid(
                commit.peakBytes, "GetPerformanceInfo", data.collectedAt);
            if (commit.limitBytes > 0) {
                data.commitUsedPercent = orion::core::Metric<double>::valid(
                    100.0 * static_cast<double>(commit.usedBytes)
                        / static_cast<double>(commit.limitBytes),
                    "GetPerformanceInfo", data.collectedAt);
            }
        }
        if (pagefile.valid && pagefile.totalPages > 0) {
            data.pagefileUsedPercent = orion::core::Metric<double>::valid(
                100.0 * static_cast<double>(pagefile.usedPages)
                    / static_cast<double>(pagefile.totalPages),
                "EnumPageFilesW", data.collectedAt);
            data.pagefilePeakPercent = orion::core::Metric<double>::valid(
                100.0 * static_cast<double>(pagefile.peakPages)
                    / static_cast<double>(pagefile.totalPages),
                "EnumPageFilesW", data.collectedAt);
            data.swapUsedPercent = data.pagefileUsedPercent;
        } else {
            data.swapUsedPercent = orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::CollectorError,
                "EnumPageFilesW",
                "No Windows pagefile counters were readable");
        }
        data.pagingInBytes = orion::core::Metric<std::uint64_t>::unavailable(
            orion::core::DataQuality::Unsupported,
            "NtQuerySystemInformation",
            "Hard-fault counters must not be relabelled as paging bytes");
        data.pagingOutBytes = data.pagingInBytes;

        if (performanceBefore.has_value() && performanceAfter.has_value()) {
            const auto rate = [elapsed](const ULONG before, const ULONG after) {
                return after >= before
                    ? std::optional {static_cast<double>(after - before) / elapsed}
                    : std::nullopt;
            };
            const auto estimatedCounter = [&](const double value) {
                return orion::core::Metric<double>::estimated(
                    value,
                    "NtQuerySystemInformation delta",
                    "Hard-fault disk reads can include file-backed pages, not only pagefile I/O",
                    data.collectedAt);
            };
            // Note: SYSTEM_PERFORMANCE_INFORMATION fields for paging metrics may not be available
            // in newer Windows SDK versions. Setting unavailable for compatibility.
            data.pagesInputPerSecond = orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::Unsupported,
                "NtQuerySystemInformation",
                "PagesRead field not available in this Windows SDK version");
            data.pageReadsPerSecond = orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::Unsupported,
                "NtQuerySystemInformation",
                "PageReadIos field not available in this Windows SDK version");
            data.pagesOutputPerSecond = orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::Unsupported,
                "NtQuerySystemInformation",
                "PagefilePagesWritten/MappedFilePagesWritten fields not available in this Windows SDK version");
            data.pageWritesPerSecond = orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::Unsupported,
                "NtQuerySystemInformation",
                "PagefilePageWriteIos/MappedFilePageWriteIos fields not available in this Windows SDK version");
            data.pagesPerSecond = orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::Unsupported,
                "NtQuerySystemInformation",
                "Paging counters not available in this Windows SDK version");
            
            // ContextSwitches is available in the compatible structure
            if (const auto switches = rate(
                    performanceBefore->ContextSwitchCount,
                    performanceAfter->ContextSwitchCount);
                switches.has_value()) {
                data.systemContextSwitchesPerSecond = orion::core::Metric<double>::valid(
                    *switches, "NtQuerySystemInformation delta", data.collectedAt);
            }
        }

        const auto disks = detail::calculateDiskVolumes(disksBefore, disksAfter, elapsed);
        data.disks = !disks.empty()
            ? orion::core::Metric<std::vector<orion::core::DiskVolume>>::valid(
                disks, "GetLogicalDriveStrings/GetDiskFreeSpaceEx/IOCTL", data.collectedAt)
            : orion::core::Metric<std::vector<orion::core::DiskVolume>>::unavailable(
                orion::core::DataQuality::CollectorError,
                "Windows volume collector",
                "No mounted fixed or removable volumes were readable");
        std::optional<double> diskBusy;
        double readLatencyWeighted = 0.0;
        double writeLatencyWeighted = 0.0;
        double readWeight = 0.0;
        double writeWeight = 0.0;
        for (const auto& disk : disks) {
            if (disk.busyPercent.has_value()) {
                diskBusy = std::max(diskBusy.value_or(0.0), *disk.busyPercent);
            }
            if (disk.readLatencyMs.has_value() && disk.readBytesPerSecond.has_value()) {
                const double weight = std::max(*disk.readBytesPerSecond, 1.0);
                readLatencyWeighted += *disk.readLatencyMs * weight;
                readWeight += weight;
            }
            if (disk.writeLatencyMs.has_value() && disk.writeBytesPerSecond.has_value()) {
                const double weight = std::max(*disk.writeBytesPerSecond, 1.0);
                writeLatencyWeighted += *disk.writeLatencyMs * weight;
                writeWeight += weight;
            }
        }
        if (diskBusy.has_value()) data.diskBusyPercent =
            orion::core::Metric<double>::valid(*diskBusy, "IOCTL_DISK_PERFORMANCE delta", data.collectedAt);
        if (readWeight > 0.0) data.diskReadLatencyMs =
            orion::core::Metric<double>::valid(readLatencyWeighted / readWeight, "IOCTL_DISK_PERFORMANCE delta", data.collectedAt);
        if (writeWeight > 0.0) data.diskWriteLatencyMs =
            orion::core::Metric<double>::valid(writeLatencyWeighted / writeWeight, "IOCTL_DISK_PERFORMANCE delta", data.collectedAt);

        if (networkAfter.valid) {
            data.netCounterScope = networkAfter.scope;
            data.netTotalReceivedBytes = orion::core::Metric<std::uint64_t>::valid(
                networkAfter.received, "GetIfTable2", data.collectedAt);
            data.netTotalSentBytes = orion::core::Metric<std::uint64_t>::valid(
                networkAfter.sent, "GetIfTable2", data.collectedAt);
            data.totalErrors = orion::core::Metric<std::uint64_t>::valid(
                networkAfter.errors, "GetIfTable2", data.collectedAt);
            data.totalDrops = orion::core::Metric<std::uint64_t>::valid(
                networkAfter.drops, "GetIfTable2", data.collectedAt);
        }
        if (networkBefore.valid && networkAfter.valid
            && networkBefore.scope == networkAfter.scope
            && networkAfter.received >= networkBefore.received) {
            data.netDownloadBytesPerSecond = orion::core::Metric<double>::valid(
                static_cast<double>(networkAfter.received - networkBefore.received) / elapsed,
                "GetIfTable2 delta",
                data.collectedAt);
        }
        if (networkBefore.valid && networkAfter.valid
            && networkBefore.scope == networkAfter.scope
            && networkAfter.sent >= networkBefore.sent) {
            data.netUploadBytesPerSecond = orion::core::Metric<double>::valid(
                static_cast<double>(networkAfter.sent - networkBefore.sent) / elapsed,
                "GetIfTable2 delta",
                data.collectedAt);
        }
        return data;
    }
};

} // namespace

std::unique_ptr<SystemBackend> makeWindowsSystemBackend()
{
    return std::make_unique<WindowsSystemBackend>();
}

} // namespace orion::platform
