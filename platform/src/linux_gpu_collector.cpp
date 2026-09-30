#include "gpu_collector.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace orion::platform::detail {
namespace {

using NvmlDevice = void*;
struct NvmlUtilization {
    unsigned int gpu;
    unsigned int memory;
};
struct NvmlMemory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

template<typename Function>
[[nodiscard]] Function loadFunction(void* module, const char* name)
{
    const auto symbol = dlsym(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(symbol));
    std::memcpy(&function, &symbol, sizeof(function));
    return function;
}

class NvmlApi final {
public:
    NvmlApi()
        : module_(dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL))
    {
        if (module_ == nullptr) {
            return;
        }
        init_ = loadFunction<Init>(module_, "nvmlInit_v2");
        shutdown_ = loadFunction<Shutdown>(module_, "nvmlShutdown");
        count_ = loadFunction<DeviceGetCount>(module_, "nvmlDeviceGetCount_v2");
        handle_ = loadFunction<DeviceGetHandle>(module_, "nvmlDeviceGetHandleByIndex_v2");
        name_ = loadFunction<DeviceGetName>(module_, "nvmlDeviceGetName");
        utilization_ = loadFunction<DeviceGetUtilization>(module_, "nvmlDeviceGetUtilizationRates");
        temperature_ = loadFunction<DeviceGetTemperature>(module_, "nvmlDeviceGetTemperature");
        fanSpeed_ = loadFunction<DeviceGetFanSpeed>(module_, "nvmlDeviceGetFanSpeed");
        memory_ = loadFunction<DeviceGetMemory>(module_, "nvmlDeviceGetMemoryInfo");
        ready_ = init_ != nullptr && shutdown_ != nullptr && count_ != nullptr
            && handle_ != nullptr && name_ != nullptr && utilization_ != nullptr
            && temperature_ != nullptr && memory_ != nullptr && init_() == 0;
    }

    ~NvmlApi()
    {
        if (ready_) {
            shutdown_();
        }
        if (module_ != nullptr) {
            dlclose(module_);
        }
    }

    [[nodiscard]] std::optional<GpuSample> sample() const
    {
        if (!ready_) {
            return std::nullopt;
        }
        unsigned int count = 0;
        if (count_(&count) != 0 || count == 0) {
            return std::nullopt;
        }
        NvmlDevice selected = nullptr;
        NvmlMemory selectedMemory {};
        for (unsigned int index = 0; index < count; ++index) {
            NvmlDevice candidate = nullptr;
            NvmlMemory candidateMemory {};
            if (handle_(index, &candidate) == 0 && memory_(candidate, &candidateMemory) == 0
                && (selected == nullptr || candidateMemory.total > selectedMemory.total)) {
                selected = candidate;
                selectedMemory = candidateMemory;
            }
        }
        if (selected == nullptr) {
            return std::nullopt;
        }
        GpuSample result;
        result.source = "NVIDIA NVML";
        std::array<char, 256> nameBuffer {};
        if (name_(selected, nameBuffer.data(), static_cast<unsigned int>(nameBuffer.size())) == 0) {
            result.name = nameBuffer.data();
        }
        result.memoryTotalBytes = static_cast<std::uint64_t>(selectedMemory.total);
        result.memoryUsedBytes = static_cast<std::uint64_t>(selectedMemory.used);
        NvmlUtilization utilization {};
        if (utilization_(selected, &utilization) == 0) {
            result.usagePercent = std::clamp(static_cast<double>(utilization.gpu), 0.0, 100.0);
        }
        unsigned int temperature = 0;
        if (temperature_(selected, 0, &temperature) == 0) {
            result.temperatureC = static_cast<double>(temperature);
        }
        unsigned int fanSpeed = 0;
        if (fanSpeed_ != nullptr && fanSpeed_(selected, &fanSpeed) == 0) {
            result.fanPercent = std::clamp(static_cast<double>(fanSpeed), 0.0, 100.0);
        }
        return result;
    }

private:
    using Init = int (*)();
    using Shutdown = int (*)();
    using DeviceGetCount = int (*)(unsigned int*);
    using DeviceGetHandle = int (*)(unsigned int, NvmlDevice*);
    using DeviceGetName = int (*)(NvmlDevice, char*, unsigned int);
    using DeviceGetUtilization = int (*)(NvmlDevice, NvmlUtilization*);
    using DeviceGetTemperature = int (*)(NvmlDevice, unsigned int, unsigned int*);
    using DeviceGetFanSpeed = int (*)(NvmlDevice, unsigned int*);
    using DeviceGetMemory = int (*)(NvmlDevice, NvmlMemory*);

    void* module_ {nullptr};
    Init init_ {nullptr};
    Shutdown shutdown_ {nullptr};
    DeviceGetCount count_ {nullptr};
    DeviceGetHandle handle_ {nullptr};
    DeviceGetName name_ {nullptr};
    DeviceGetUtilization utilization_ {nullptr};
    DeviceGetTemperature temperature_ {nullptr};
    DeviceGetFanSpeed fanSpeed_ {nullptr};
    DeviceGetMemory memory_ {nullptr};
    bool ready_ {false};
};

[[nodiscard]] std::optional<std::uint64_t> integerFromFile(
    const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::uint64_t value = 0;
    return input >> value ? std::optional {value} : std::nullopt;
}

[[nodiscard]] std::string textFromFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::string value;
    input >> value;
    return value;
}

[[nodiscard]] GpuSample sysfsSample()
{
    GpuSample best;
    best.source = "Linux DRM sysfs";
    best.reason = "Only metrics exposed by the active DRM driver are available";
    std::uint64_t bestMemory = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm", error)) {
        const auto card = entry.path().filename().string();
        if (!card.starts_with("card") || card.find('-') != std::string::npos) {
            continue;
        }
        const auto device = entry.path() / "device";
        GpuSample candidate;
        candidate.source = "Linux DRM sysfs";
        const auto vendor = textFromFile(device / "vendor");
        candidate.name = vendor == "0x1002" ? "AMD GPU"
            : vendor == "0x8086" ? "Intel GPU"
            : vendor == "0x10de" ? "NVIDIA GPU"
                                  : "DRM GPU";
        if (const auto usage = integerFromFile(device / "gpu_busy_percent"); usage.has_value()) {
            candidate.usagePercent = std::clamp(static_cast<double>(*usage), 0.0, 100.0);
        }
        candidate.memoryTotalBytes = integerFromFile(device / "mem_info_vram_total");
        candidate.memoryUsedBytes = integerFromFile(device / "mem_info_vram_used");
        const auto hwmonRoot = device / "hwmon";
        if (std::filesystem::exists(hwmonRoot, error)) {
            for (const auto& hwmon : std::filesystem::directory_iterator(hwmonRoot, error)) {
                if (const auto milliCelsius = integerFromFile(hwmon.path() / "temp1_input");
                    milliCelsius.has_value()) {
                    candidate.temperatureC = static_cast<double>(*milliCelsius) / 1000.0;
                    break;
                }
            }
        }
        const auto memory = candidate.memoryTotalBytes.value_or(0);
        if (best.name == "Unknown GPU" || memory > bestMemory) {
            best = std::move(candidate);
            bestMemory = memory;
        }
    }
    return best;
}

} // namespace

GpuSample readGpuSample()
{
    static NvmlApi nvml;
    if (const auto sample = nvml.sample(); sample.has_value()) {
        return *sample;
    }
    return sysfsSample();
}

} // namespace orion::platform::detail
