#include "orion/platform/system_backend.h"

#include "disk_collector.h"
#include "gpu_collector.h"
#include "sensor_collector.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace orion::platform {
namespace {

struct CpuTimes {
    std::uint64_t idle {0};
    std::uint64_t total {0};
    bool valid {false};
};

struct NetworkCounters {
    std::string scope;
    std::uint64_t received {0};
    std::uint64_t sent {0};
    std::uint64_t errors {0};
    std::uint64_t drops {0};
    bool valid {false};
};

[[nodiscard]] CpuTimes readCpuTimes()
{
    std::ifstream input("/proc/stat");
    std::string label;
    std::uint64_t user = 0;
    std::uint64_t nice = 0;
    std::uint64_t system = 0;
    std::uint64_t idle = 0;
    std::uint64_t ioWait = 0;
    std::uint64_t irq = 0;
    std::uint64_t softIrq = 0;
    std::uint64_t steal = 0;
    input >> label >> user >> nice >> system >> idle >> ioWait >> irq >> softIrq >> steal;
    if (!input || label != "cpu") {
        return {};
    }
    return {
        idle + ioWait,
        user + nice + system + idle + ioWait + irq + softIrq + steal,
        true,
    };
}

[[nodiscard]] std::vector<CpuTimes> readPerCoreTimes()
{
    std::ifstream input("/proc/stat");
    std::vector<CpuTimes> result;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string label;
        std::uint64_t user = 0;
        std::uint64_t nice = 0;
        std::uint64_t system = 0;
        std::uint64_t idle = 0;
        std::uint64_t ioWait = 0;
        std::uint64_t irq = 0;
        std::uint64_t softIrq = 0;
        std::uint64_t steal = 0;
        stream >> label;
        if (!label.starts_with("cpu")) {
            break;
        }
        if (label == "cpu") {
            continue;
        }
        stream >> user >> nice >> system >> idle >> ioWait >> irq >> softIrq >> steal;
        if (stream) {
            result.push_back({
                idle + ioWait,
                user + nice + system + idle + ioWait + irq + softIrq + steal,
                true,
            });
        }
    }
    return result;
}

[[nodiscard]] std::vector<double> readCpuFrequenciesMhz()
{
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    std::vector<double> result;
    while (std::getline(input, line)) {
        if (!line.starts_with("cpu MHz")) {
            continue;
        }
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        try {
            result.push_back(std::stod(line.substr(separator + 1)));
        } catch (...) {
        }
    }
    return result;
}

[[nodiscard]] std::optional<double> averageCpuFrequencyMhz(
    const std::vector<double>& frequencies)
{
    if (frequencies.empty()) {
        return std::nullopt;
    }
    double total = 0.0;
    for (const double frequency : frequencies) {
        total += frequency;
    }
    return total / static_cast<double>(frequencies.size());
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

[[nodiscard]] std::unordered_map<std::string, std::uint64_t> readMemoryKiB()
{
    std::ifstream input("/proc/meminfo");
    std::unordered_map<std::string, std::uint64_t> values;
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    while (input >> key >> value >> unit) {
        if (!key.empty() && key.back() == ':') {
            key.pop_back();
        }
        values[key] = value;
    }
    return values;
}

[[nodiscard]] NetworkCounters readNetworkCounters()
{
    std::ifstream input("/proc/net/dev");
    std::string line;
    std::getline(input, line);
    std::getline(input, line);

    NetworkCounters counters;
    std::vector<std::string> interfaces;
    while (std::getline(input, line)) {
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        std::string interfaceName = line.substr(0, separator);
        interfaceName.erase(
            std::remove_if(
                interfaceName.begin(),
                interfaceName.end(),
                [](const unsigned char character) { return std::isspace(character) != 0; }),
            interfaceName.end());
        if (interfaceName == "lo") {
            continue;
        }

        std::istringstream fields(line.substr(separator + 1));
        std::uint64_t rxBytes = 0;
        std::uint64_t rxPackets = 0;
        std::uint64_t rxErrors = 0;
        std::uint64_t rxDrops = 0;
        std::uint64_t rxFifo = 0;
        std::uint64_t rxFrame = 0;
        std::uint64_t rxCompressed = 0;
        std::uint64_t rxMulticast = 0;
        std::uint64_t txBytes = 0;
        std::uint64_t txPackets = 0;
        std::uint64_t txErrors = 0;
        std::uint64_t txDrops = 0;
        if (!(fields >> rxBytes >> rxPackets >> rxErrors >> rxDrops
            >> rxFifo >> rxFrame >> rxCompressed >> rxMulticast
            >> txBytes >> txPackets >> txErrors >> txDrops)) return {};
        interfaces.push_back(interfaceName);
        counters.received += rxBytes;
        counters.sent += txBytes;
        counters.errors += rxErrors + txErrors;
        counters.drops += rxDrops + txDrops;
    }
    std::sort(interfaces.begin(), interfaces.end());
    for (const auto& identity : interfaces) counters.scope += identity + ";";
    counters.valid = input.is_open() && !input.bad() && (static_cast<bool>(input) || input.eof());
    return counters;
}

[[nodiscard]] std::string valueFromFile(
    const std::string& path,
    const std::string& prefix)
{
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.starts_with(prefix)) {
            continue;
        }
        std::string value = line.substr(prefix.size());
        if (!value.empty() && value.front() == '=') {
            value.erase(value.begin());
        }
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }
    return {};
}

[[nodiscard]] std::string readCpuName()
{
    const auto name = valueFromFile("/proc/cpuinfo", "model name");
    const auto separator = name.find(':');
    if (separator == std::string::npos) {
        return name.empty() ? "Unknown CPU" : name;
    }
    auto trimmed = name.substr(separator + 1);
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    return trimmed;
}

class LinuxSystemBackend final : public SystemBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "linux";
    }

    [[nodiscard]] orion::core::TelemetryData sample(
        const std::chrono::milliseconds interval) override
    {
        const auto safeInterval = std::max(interval, std::chrono::milliseconds {50});
        const auto cpuBefore = readCpuTimes();
        const auto coresBefore = readPerCoreTimes();
        const auto networkBefore = readNetworkCounters();
        const auto disksBefore = detail::readDiskCounterSamples();
        const auto started = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(safeInterval);
        const auto cpuAfter = readCpuTimes();
        const auto coresAfter = readPerCoreTimes();
        const auto networkAfter = readNetworkCounters();
        const auto disksAfter = detail::readDiskCounterSamples();
        const auto gpu = detail::readGpuSample();
        const auto sensors = detail::readSensorSnapshot(gpu);
        const double elapsed = std::max(
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
            0.001);

        const auto memory = readMemoryKiB();
        const auto memoryTotal = memory.contains("MemTotal") ? memory.at("MemTotal") : 0;
        const auto memoryAvailable = memory.contains("MemAvailable")
            ? memory.at("MemAvailable")
            : 0;
        const auto swapTotal = memory.contains("SwapTotal") ? memory.at("SwapTotal") : 0;
        const auto swapFree = memory.contains("SwapFree") ? memory.at("SwapFree") : 0;

        orion::core::TelemetryData data;
        data.collectedAt = std::chrono::system_clock::now();
        const auto cpuUsage = cpuPercent(cpuBefore, cpuAfter);
        data.cpuUsagePercent = cpuUsage.has_value()
            ? orion::core::Metric<double>::valid(
                *cpuUsage, "/proc/stat", data.collectedAt)
            : orion::core::Metric<double>::unavailable(
                orion::core::DataQuality::CollectorError,
                "/proc/stat",
                "CPU counters were unavailable or did not advance");
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
        if (!coresBefore.empty() && coresBefore.size() == coresAfter.size()) {
            std::vector<orion::core::CpuCoreLoad> coreLoads;
            coreLoads.reserve(coresAfter.size());
            for (std::size_t index = 0; index < coresAfter.size(); ++index) {
                const auto value = cpuPercent(coresBefore[index], coresAfter[index]);
                if (!value.has_value()) {
                    coreLoads.clear();
                    break;
                }
                coreLoads.push_back({index + 1, *value});
            }
            if (!coreLoads.empty()) {
                data.cpuCores = orion::core::Metric<std::vector<orion::core::CpuCoreLoad>>::valid(
                    coreLoads,
                    "/proc/stat per-core",
                    data.collectedAt);
            }
        }
        const auto coreFrequenciesMhz = readCpuFrequenciesMhz();
        if (!coreFrequenciesMhz.empty()) {
            data.cpuCoreFrequenciesMhz =
                orion::core::Metric<std::vector<double>>::valid(
                    coreFrequenciesMhz, "/proc/cpuinfo per-core", data.collectedAt);
        }
        const auto frequencyMhz = averageCpuFrequencyMhz(coreFrequenciesMhz);
        if (frequencyMhz.has_value()) {
            data.cpuFrequencyMhz = orion::core::Metric<double>::valid(
                *frequencyMhz, "/proc/cpuinfo", data.collectedAt);
        }
        data.osName = valueFromFile("/etc/os-release", "PRETTY_NAME");
        if (data.osName.empty()) {
            data.osName = "Linux";
        }
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

        if (memoryTotal > 0) {
            data.ramTotalBytes = orion::core::Metric<std::uint64_t>::valid(
                memoryTotal * 1024ULL, "/proc/meminfo", data.collectedAt);
        }
        if (memoryTotal > 0) {
            const double availablePercent =
                100.0 * static_cast<double>(memoryAvailable)
                / static_cast<double>(memoryTotal);
            data.ramAvailablePercent = orion::core::Metric<double>::valid(
                availablePercent, "/proc/meminfo", data.collectedAt);
            data.ramUsagePercent = orion::core::Metric<double>::valid(
                100.0 - availablePercent, "/proc/meminfo", data.collectedAt);
        }
        if (swapTotal > 0) {
            data.swapUsedPercent = orion::core::Metric<double>::valid(
                100.0 * static_cast<double>(swapTotal - std::min(swapFree, swapTotal))
                    / static_cast<double>(swapTotal),
                "/proc/meminfo",
                data.collectedAt);
        } else {
            data.swapUsedPercent = orion::core::Metric<double>::valid(
                0.0, "/proc/meminfo", data.collectedAt);
        }
        data.pagingInBytes = orion::core::Metric<std::uint64_t>::unavailable(
            orion::core::DataQuality::Unsupported,
            "stage1-linux-backend",
            "Cumulative swap byte counters are scheduled for the collector stage");
        data.pagingOutBytes = data.pagingInBytes;

        const auto disks = detail::calculateDiskVolumes(disksBefore, disksAfter, elapsed);
        data.disks = !disks.empty()
            ? orion::core::Metric<std::vector<orion::core::DiskVolume>>::valid(
                disks, "/proc/mounts + statvfs + /sys/class/block", data.collectedAt)
            : orion::core::Metric<std::vector<orion::core::DiskVolume>>::unavailable(
                orion::core::DataQuality::CollectorError,
                "Linux volume collector",
                "No mounted block-device volumes were readable");

        if (networkAfter.valid) {
            data.netCounterScope = networkAfter.scope;
            data.netTotalReceivedBytes = orion::core::Metric<std::uint64_t>::valid(
                networkAfter.received, "/proc/net/dev", data.collectedAt);
            data.netTotalSentBytes = orion::core::Metric<std::uint64_t>::valid(
                networkAfter.sent, "/proc/net/dev", data.collectedAt);
            data.totalErrors = orion::core::Metric<std::uint64_t>::valid(
                networkAfter.errors, "/proc/net/dev", data.collectedAt);
            data.totalDrops = orion::core::Metric<std::uint64_t>::valid(
                networkAfter.drops, "/proc/net/dev", data.collectedAt);
        }
        if (networkBefore.valid && networkAfter.valid
            && networkBefore.scope == networkAfter.scope
            && networkAfter.received >= networkBefore.received) {
            data.netDownloadBytesPerSecond = orion::core::Metric<double>::valid(
                static_cast<double>(networkAfter.received - networkBefore.received) / elapsed,
                "/proc/net/dev delta",
                data.collectedAt);
        }
        if (networkBefore.valid && networkAfter.valid
            && networkBefore.scope == networkAfter.scope
            && networkAfter.sent >= networkBefore.sent) {
            data.netUploadBytesPerSecond = orion::core::Metric<double>::valid(
                static_cast<double>(networkAfter.sent - networkBefore.sent) / elapsed,
                "/proc/net/dev delta",
                data.collectedAt);
        }
        return data;
    }
};

} // namespace

std::unique_ptr<SystemBackend> makeLinuxSystemBackend()
{
    return std::make_unique<LinuxSystemBackend>();
}

} // namespace orion::platform
