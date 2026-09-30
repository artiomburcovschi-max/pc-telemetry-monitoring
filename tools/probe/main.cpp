#include "orion/platform/system_backend.h"
#include "orion/platform/process_collector.h"
#include "orion/platform/autostart_collector.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace {

[[nodiscard]] double mebibytes(const std::uint64_t bytes) noexcept
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

} // namespace

int main()
{
    auto backend = orion::platform::makeSystemBackend();
    if (!backend) {
        std::cerr << "ORION could not create a system telemetry backend.\n";
        return EXIT_FAILURE;
    }

    const auto data = backend->sample(std::chrono::milliseconds {350});
    const auto printMetric = [](const orion::core::Metric<double>& metric) {
        if (!metric.value.has_value()) {
            return std::string {"unknown ("}
                + std::string {orion::core::toString(metric.quality)} + ")";
        }
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(1) << *metric.value;
        return stream.str();
    };
    const auto logicalProcessorCount = data.cpuCores.usable()
        ? std::to_string(data.cpuCores.value->size())
        : std::string {"unknown"};
    std::cout << std::fixed << std::setprecision(1)
              << "ORION native telemetry probe\n"
              << "backend: " << backend->name() << '\n'
              << "os: " << data.osName << '\n'
              << "cpu: " << printMetric(data.cpuUsagePercent) << "% (" << data.cpuName << ")\n"
              << "logical processors: " << logicalProcessorCount << '\n'
              << "cpu frequency: " << printMetric(data.cpuFrequencyMhz) << " MHz\n"
              << "gpu: " << printMetric(data.gpuUsagePercent) << "% (" << data.gpuName << ")\n"
              << "gpu temperature: " << printMetric(data.gpuTemperatureC) << " C\n"
              << "gpu vram: " << printMetric(data.gpuMemoryUsedMb) << " / "
              << printMetric(data.gpuMemoryTotalMb) << " MiB ("
              << printMetric(data.gpuMemoryPercent) << "%)\n"
              << "ram: " << printMetric(data.ramUsagePercent) << "% / ";
    if (data.ramTotalBytes.value.has_value()) {
        std::cout << mebibytes(*data.ramTotalBytes.value) << " MiB\n";
    } else {
        std::cout << "unknown\n";
    }
    std::cout << "commit: " << printMetric(data.commitUsedPercent) << "%";
    if (data.commitUsedBytes.usable() && data.commitLimitBytes.usable()) {
        std::cout << " (" << mebibytes(*data.commitUsedBytes.value) << " / "
                  << mebibytes(*data.commitLimitBytes.value) << " MiB)";
    }
    std::cout << "\npagefile used: " << printMetric(data.pagefileUsedPercent) << "%\n"
              << "hard-fault reads: " << printMetric(data.pagesInputPerSecond)
              << " pages/s, " << printMetric(data.pageReadsPerSecond) << " reads/s\n"
              << "system context switches: "
              << printMetric(data.systemContextSwitchesPerSecond) << "/s\n"
              << "disk busy: " << printMetric(data.diskBusyPercent)
              << "%, latency R/W: " << printMetric(data.diskReadLatencyMs)
              << "/" << printMetric(data.diskWriteLatencyMs) << " ms\n";
    if (data.disks.usable()) {
        std::cout << "disk volumes: " << data.disks.value->size() << '\n';
        for (const auto& disk : *data.disks.value) {
            std::cout << "  " << disk.mountPoint << " " << disk.storageType
                      << " " << std::fixed << std::setprecision(1)
                      << (static_cast<double>(disk.usedBytes) * 100.0
                          / static_cast<double>(disk.totalBytes))
                      << "% used";
            if (disk.readBytesPerSecond.has_value()) {
                std::cout << ", read " << *disk.readBytesPerSecond / (1024.0 * 1024.0)
                          << " MiB/s";
            }
            if (disk.writeBytesPerSecond.has_value()) {
                std::cout << ", write " << *disk.writeBytesPerSecond / (1024.0 * 1024.0)
                          << " MiB/s";
            }
            std::cout << '\n';
        }
    } else {
        std::cout << "disk volumes: unknown\n";
    }
    if (data.temperatures.usable()) {
        std::cout << "temperature sensors: " << data.temperatures.value->size() << '\n';
        for (const auto& sensor : *data.temperatures.value) {
            std::cout << "  " << orion::core::toString(sensor.component)
                      << " / " << sensor.label << ": " << sensor.valueC
                      << " C (" << sensor.source << ")\n";
        }
    } else {
        std::cout << "temperature sensors: unknown (" << data.temperatures.reason << ")\n";
    }
    if (data.fans.usable()) {
        std::cout << "fan sensors: " << data.fans.value->size() << '\n';
        for (const auto& fan : *data.fans.value) {
            std::cout << "  " << orion::core::toString(fan.component)
                      << " / " << fan.label << ": ";
            if (fan.rpm.has_value()) {
                std::cout << *fan.rpm << " RPM";
            }
            if (fan.percent.has_value()) {
                std::cout << (fan.rpm.has_value() ? " / " : "") << *fan.percent << "%";
            }
            std::cout << " (" << fan.source << ")\n";
        }
    } else {
        std::cout << "fan sensors: unknown (" << data.fans.reason << ")\n";
    }
    std::cout << "network down: "
              << (data.netDownloadBytesPerSecond.value.has_value()
                      ? printMetric(data.netDownloadBytesPerSecond) + " B/s"
                      : printMetric(data.netDownloadBytesPerSecond))
              << '\n'
              << "network up: "
              << (data.netUploadBytesPerSecond.value.has_value()
                      ? printMetric(data.netUploadBytesPerSecond) + " B/s"
                      : printMetric(data.netUploadBytesPerSecond))
              << '\n';
    auto processCollector = orion::platform::makeProcessCollector();
    if (processCollector) {
        processCollector->sample();
        std::this_thread::sleep_for(std::chrono::milliseconds {250});
        auto processes = processCollector->sample();
        std::ranges::sort(processes.processes, [](const auto& left, const auto& right) {
            return left.cpuPercent.value_or(-1.0) > right.cpuPercent.value_or(-1.0);
        });
        std::cout << "processes: " << processes.processes.size()
                  << " (source: " << processes.source << ")\n";
        const auto visible = std::min<std::size_t>(processes.processes.size(), 5);
        for (std::size_t index = 0; index < visible; ++index) {
            const auto& process = processes.processes[index];
            std::cout << "  " << process.name << " (PID " << process.pid << "): ";
            if (process.cpuPercent.has_value()) {
                std::cout << *process.cpuPercent << "% CPU";
            } else {
                std::cout << "CPU unknown";
            }
            if (process.workingSetBytes.has_value()) {
                std::cout << ", " << mebibytes(*process.workingSetBytes) << " MiB RAM";
            }
            if (process.contextSwitchesPerSecond.has_value()) {
                std::cout << ", " << *process.contextSwitchesPerSecond << " ctx/s";
            }
            std::cout << ", " << process.threadCount << " threads\n";
        }
    }
    if (auto autostartCollector = orion::platform::makeAutostartCollector()) {
        const auto autostart = autostartCollector->scan();
        const auto userCount = std::ranges::count_if(autostart.entries, [](const auto& entry) {
            return entry.category == orion::core::AutostartCategory::user;
        });
        std::cout << "autostart entries: " << autostart.entries.size()
                  << " (user: " << userCount
                  << ", system: " << autostart.entries.size() - userCount
                  << ", source: " << autostart.source << ")\n";
    }
    return EXIT_SUCCESS;
}
