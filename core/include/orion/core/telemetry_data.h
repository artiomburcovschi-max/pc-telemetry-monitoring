#pragma once

#include "orion/core/cpu_samples.h"
#include "orion/core/disk_volume.h"
#include "orion/core/metric.h"
#include "orion/core/sensor_reading.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace orion::core {

struct TelemetryData {
    std::chrono::system_clock::time_point collectedAt {std::chrono::system_clock::now()};

    Metric<double> cpuUsagePercent;
    Metric<std::vector<CpuCoreLoad>> cpuCores;
    Metric<std::vector<double>> cpuCoreFrequenciesMhz;
    Metric<double> cpuFrequencyMhz;
    Metric<double> cpuTemperatureC;
    std::string cpuName {"Unknown CPU"};

    Metric<double> gpuUsagePercent;
    Metric<double> gpuTemperatureC;
    Metric<double> gpuMemoryTotalMb;
    Metric<double> gpuMemoryUsedMb;
    Metric<double> gpuMemoryPercent;
    std::string gpuName {"Unknown GPU"};

    Metric<double> ramUsagePercent;
    Metric<std::uint64_t> ramTotalBytes;
    Metric<double> ramAvailablePercent;
    Metric<double> swapUsedPercent;
    Metric<std::uint64_t> pagingInBytes;
    Metric<std::uint64_t> pagingOutBytes;
    Metric<std::uint64_t> commitUsedBytes;
    Metric<std::uint64_t> commitLimitBytes;
    Metric<std::uint64_t> commitPeakBytes;
    Metric<double> commitUsedPercent;
    Metric<double> pagefileUsedPercent;
    Metric<double> pagefilePeakPercent;
    Metric<double> pagesInputPerSecond;
    Metric<double> pageReadsPerSecond;
    Metric<double> pagesPerSecond;
    Metric<double> pagesOutputPerSecond;
    Metric<double> pageWritesPerSecond;
    Metric<double> systemContextSwitchesPerSecond;
    Metric<double> diskBusyPercent;
    Metric<double> diskReadLatencyMs;
    Metric<double> diskWriteLatencyMs;

    Metric<std::vector<DiskVolume>> disks;
    Metric<std::vector<TemperatureSensor>> temperatures;
    Metric<std::vector<FanSensor>> fans;

    std::string osName;

    Metric<double> netDownloadBytesPerSecond;
    Metric<double> netUploadBytesPerSecond;
    Metric<double> netPingMs;
    Metric<std::uint64_t> netTotalReceivedBytes;
    Metric<std::uint64_t> netTotalSentBytes;
    Metric<std::uint64_t> totalErrors;
    Metric<std::uint64_t> totalDrops;
    std::string netCounterScope;
};

} // namespace orion::core
