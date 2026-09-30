#include "orion/platform/system_backend.h"
#include "orion/platform/process_collector.h"
#include "orion/platform/autostart_collector.h"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int main()
{
    auto backend = orion::platform::makeSystemBackend();
    if (!backend) {
        std::cerr << "No platform backend was created.\n";
        return EXIT_FAILURE;
    }

    const auto sample = backend->sample(std::chrono::milliseconds {120});
    if (!sample.cpuUsagePercent.value.has_value()
        || *sample.cpuUsagePercent.value < 0.0
        || *sample.cpuUsagePercent.value > 100.0) {
        std::cerr << "CPU usage is outside 0..100.\n";
        return EXIT_FAILURE;
    }
    if (!sample.ramUsagePercent.value.has_value()
        || *sample.ramUsagePercent.value < 0.0
        || *sample.ramUsagePercent.value > 100.0) {
        std::cerr << "RAM usage is outside 0..100.\n";
        return EXIT_FAILURE;
    }
    if (!sample.ramTotalBytes.value.has_value() || *sample.ramTotalBytes.value == 0) {
        std::cerr << "Total physical RAM was not detected.\n";
        return EXIT_FAILURE;
    }
#ifdef _WIN32
    if (!sample.commitUsedBytes.usable() || !sample.commitLimitBytes.usable()
        || !sample.commitUsedPercent.usable()
        || *sample.commitUsedBytes.value > *sample.commitLimitBytes.value
        || *sample.commitUsedPercent.value < 0.0
        || *sample.commitUsedPercent.value > 100.0) {
        std::cerr << "Windows commit counters violate the native telemetry contract.\n";
        return EXIT_FAILURE;
    }
    if (!sample.pagesInputPerSecond.usable()
        || !sample.pageReadsPerSecond.usable()
        || !sample.pagesPerSecond.usable()
        || !sample.systemContextSwitchesPerSecond.usable()) {
        std::cerr << "Windows paging/context-switch rates are unavailable.\n";
        return EXIT_FAILURE;
    }
#endif
    if (sample.osName.empty()) {
        std::cerr << "Operating system name is empty.\n";
        return EXIT_FAILURE;
    }
    if (sample.cpuCores.value.has_value()) {
        if (sample.cpuCores.value->empty()) {
            std::cerr << "Per-core telemetry is marked available but contains no cores.\n";
            return EXIT_FAILURE;
        }
        for (const auto& core : *sample.cpuCores.value) {
            if (core.percent < 0.0 || core.percent > 100.0) {
                std::cerr << "Logical-core usage is outside 0..100.\n";
                return EXIT_FAILURE;
            }
        }
    }
    if (sample.gpuName.empty()) {
        std::cerr << "GPU identity must never be empty.\n";
        return EXIT_FAILURE;
    }
    if (sample.gpuUsagePercent.value.has_value()
        && (*sample.gpuUsagePercent.value < 0.0
            || *sample.gpuUsagePercent.value > 100.0)) {
        std::cerr << "GPU usage is outside 0..100.\n";
        return EXIT_FAILURE;
    }
    if (sample.gpuTemperatureC.value.has_value()
        && (*sample.gpuTemperatureC.value < -50.0
            || *sample.gpuTemperatureC.value > 150.0)) {
        std::cerr << "GPU temperature is outside the supported sanity range.\n";
        return EXIT_FAILURE;
    }
    if (sample.gpuMemoryTotalMb.value.has_value()
        && *sample.gpuMemoryTotalMb.value <= 0.0) {
        std::cerr << "GPU memory total is not positive.\n";
        return EXIT_FAILURE;
    }
    if (sample.temperatures.value.has_value()) {
        for (const auto& sensor : *sample.temperatures.value) {
            if (sensor.label.empty() || sensor.source.empty()
                || sensor.valueC < -50.0 || sensor.valueC > 160.0) {
                std::cerr << "A temperature channel violates the sensor contract.\n";
                return EXIT_FAILURE;
            }
        }
    }
    if (sample.fans.value.has_value()) {
        for (const auto& fan : *sample.fans.value) {
            if (fan.label.empty() || fan.source.empty()
                || (!fan.rpm.has_value() && !fan.percent.has_value())
                || (fan.rpm.has_value() && *fan.rpm < 0.0)
                || (fan.percent.has_value()
                    && (*fan.percent < 0.0 || *fan.percent > 100.0))) {
                std::cerr << "A fan channel violates the sensor contract.\n";
                return EXIT_FAILURE;
            }
        }
    }
    if (sample.gpuMemoryTotalMb.value.has_value()
        && sample.gpuMemoryUsedMb.value.has_value()
        && *sample.gpuMemoryUsedMb.value > *sample.gpuMemoryTotalMb.value) {
        std::cerr << "GPU memory usage exceeds the detected total.\n";
        return EXIT_FAILURE;
    }
    if (!sample.disks.value.has_value() || sample.disks.value->empty()) {
        std::cerr << "No mounted disk volumes were detected.\n";
        return EXIT_FAILURE;
    }
    for (const auto& disk : *sample.disks.value) {
        if (disk.mountPoint.empty() || disk.totalBytes == 0
            || disk.usedBytes > disk.totalBytes || disk.freeBytes > disk.totalBytes
            || disk.usedPercent < 0.0 || disk.usedPercent > 100.0) {
            std::cerr << "A disk volume violates the native telemetry contract.\n";
            return EXIT_FAILURE;
        }
    }

    auto processCollector = orion::platform::makeProcessCollector();
    if (!processCollector) {
        std::cerr << "No process collector was created.\n";
        return EXIT_FAILURE;
    }
    const auto firstProcesses = processCollector->sample();
    std::this_thread::sleep_for(std::chrono::milliseconds {80});
    const auto processes = processCollector->sample();
    if (processes.processes.empty() || processes.logicalProcessorCount == 0) {
        std::cerr << "Native process inventory is empty or has no CPU topology.\n";
        return EXIT_FAILURE;
    }
#ifdef _WIN32
    const auto currentPid = static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    const auto currentPid = static_cast<std::uint32_t>(getpid());
#endif
    const auto current = std::ranges::find_if(processes.processes, [currentPid](const auto& process) {
        return process.pid == currentPid;
    });
    if (current == processes.processes.end() || current->name.empty()) {
        std::cerr << "Native process inventory did not include the named test process.\n";
        return EXIT_FAILURE;
    }
    if (!current->cpuPercent.has_value() || *current->cpuPercent < 0.0
        || !current->workingSetBytes.has_value() || *current->workingSetBytes == 0) {
        std::cerr << "Sequential process CPU or working-set counters are unavailable.\n";
        return EXIT_FAILURE;
    }

    auto autostartCollector = orion::platform::makeAutostartCollector();
    if (!autostartCollector) {
        std::cerr << "No autostart collector was created.\n";
        return EXIT_FAILURE;
    }
    const auto autostart = autostartCollector->scan();
    if (autostart.source.empty()) {
        std::cerr << "Autostart collector did not identify its native sources.\n";
        return EXIT_FAILURE;
    }
    for (const auto& entry : autostart.entries) {
        if (entry.name.empty() || entry.source.empty()) {
            std::cerr << "An autostart entry violates the native contract.\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << "Platform smoke test passed for " << backend->name()
              << " (GPU: " << sample.gpuName
              << ", temperatures: "
              << (sample.temperatures.value.has_value()
                      ? sample.temperatures.value->size() : 0)
              << ", fans: "
              << (sample.fans.value.has_value() ? sample.fans.value->size() : 0)
              << ", processes: " << processes.processes.size()
              << ", autostart: " << autostart.entries.size()
              << ").\n";
    return EXIT_SUCCESS;
}
