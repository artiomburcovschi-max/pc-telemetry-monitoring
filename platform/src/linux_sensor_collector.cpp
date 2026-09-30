#include "sensor_collector.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace orion::platform::detail {
namespace {

[[nodiscard]] std::string textFromFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::string value;
    std::getline(input, value);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
    return value;
}

[[nodiscard]] std::optional<double> numberFromFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    double value = 0.0;
    return input >> value ? std::optional {value} : std::nullopt;
}

[[nodiscard]] std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] bool containsAny(const std::string_view value,
                               const std::initializer_list<std::string_view> tokens)
{
    return std::ranges::any_of(tokens, [value](const auto token) {
        return value.find(token) != std::string_view::npos;
    });
}

[[nodiscard]] orion::core::SensorComponent classify(
    const std::string& chip,
    const std::string& label,
    const std::string& identifier)
{
    const auto text = lower(chip + " " + label + " " + identifier);
    if (containsAny(text, {"coretemp", "k10temp", "zenpower", "cpu_thermal", "cpu package", "package id", "tctl", "tdie", "core "})) {
        return orion::core::SensorComponent::Cpu;
    }
    if (containsAny(text, {"amdgpu", "nouveau", "nvidia", "gpu", "edge", "junction", "hotspot"})) {
        return orion::core::SensorComponent::Gpu;
    }
    if (containsAny(text, {"nvme", "drivetemp", "sata", "storage"})) {
        return orion::core::SensorComponent::Storage;
    }
    if (containsAny(text, {"motherboard", "mainboard", "chipset", "pch", "system", "acpi"})) {
        return orion::core::SensorComponent::System;
    }
    return orion::core::SensorComponent::Other;
}

[[nodiscard]] std::optional<double> temperatureFromFile(const std::filesystem::path& path)
{
    auto value = numberFromFile(path);
    if (!value.has_value()) {
        return std::nullopt;
    }
    if (std::abs(*value) > 500.0) {
        *value /= 1000.0;
    }
    return *value >= -50.0 && *value <= 160.0 ? value : std::nullopt;
}

[[nodiscard]] std::optional<unsigned int> channelIndex(
    const std::string& filename,
    const std::string_view prefix)
{
    if (!filename.starts_with(prefix) || !filename.ends_with("_input")) {
        return std::nullopt;
    }
    const auto number = filename.substr(prefix.size(), filename.size() - prefix.size() - 6);
    if (number.empty() || !std::ranges::all_of(number, [](const unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        return std::nullopt;
    }
    try {
        return static_cast<unsigned int>(std::stoul(number));
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

SensorSnapshot readSensorSnapshot(const GpuSample& gpu)
{
    SensorSnapshot snapshot;
    std::error_code error;
    const std::filesystem::path root {"/sys/class/hwmon"};
    for (const auto& hwmon : std::filesystem::directory_iterator(root, error)) {
        if (error) {
            break;
        }
        const auto chip = textFromFile(hwmon.path() / "name");
        std::error_code channelError;
        for (const auto& channel : std::filesystem::directory_iterator(hwmon.path(), channelError)) {
            if (channelError) {
                break;
            }
            const auto filename = channel.path().filename().string();
            if (const auto index = channelIndex(filename, "temp"); index.has_value()) {
                const auto value = temperatureFromFile(channel.path());
                if (!value.has_value()) {
                    continue;
                }
                const auto base = "temp" + std::to_string(*index);
                auto label = textFromFile(hwmon.path() / (base + "_label"));
                if (label.empty()) {
                    label = (chip.empty() ? "hwmon" : chip) + " " + base;
                }
                snapshot.temperatures.push_back({
                    classify(chip, label, channel.path().string()),
                    label,
                    *value,
                    temperatureFromFile(hwmon.path() / (base + "_max")),
                    temperatureFromFile(hwmon.path() / (base + "_crit")),
                    "Linux hwmon sysfs",
                    channel.path().string(),
                });
            } else if (const auto index = channelIndex(filename, "fan"); index.has_value()) {
                const auto rpm = numberFromFile(channel.path());
                if (!rpm.has_value() || *rpm < 0.0) {
                    continue;
                }
                const auto base = "fan" + std::to_string(*index);
                auto label = textFromFile(hwmon.path() / (base + "_label"));
                if (label.empty()) {
                    label = (chip.empty() ? "hwmon" : chip) + " " + base;
                }
                snapshot.fans.push_back({
                    classify(chip, label, channel.path().string()),
                    label,
                    *rpm,
                    std::nullopt,
                    "Linux hwmon sysfs",
                    channel.path().string(),
                });
            }
        }
    }

    const bool hasGpuTemperature = std::ranges::any_of(
        snapshot.temperatures, [](const auto& sensor) {
            return sensor.component == orion::core::SensorComponent::Gpu;
        });
    if (!hasGpuTemperature && gpu.temperatureC.has_value()) {
        snapshot.temperatures.push_back({
            orion::core::SensorComponent::Gpu,
            "GPU Core",
            *gpu.temperatureC,
            std::nullopt,
            std::nullopt,
            gpu.source,
            "gpu:primary:temperature",
        });
    }
    const bool hasGpuFan = std::ranges::any_of(snapshot.fans, [](const auto& sensor) {
        return sensor.component == orion::core::SensorComponent::Gpu;
    });
    if (!hasGpuFan && gpu.fanPercent.has_value()) {
        snapshot.fans.push_back({
            orion::core::SensorComponent::Gpu,
            "GPU Fan",
            std::nullopt,
            *gpu.fanPercent,
            gpu.source,
            "gpu:primary:fan",
        });
    }

    snapshot.source = !snapshot.temperatures.empty() || !snapshot.fans.empty()
        ? "Linux hwmon sysfs / vendor driver"
        : "Linux hwmon sysfs";
    if (snapshot.temperatures.empty() && snapshot.fans.empty()) {
        snapshot.reason = "No temperature or fan channels were published in /sys/class/hwmon";
    }
    return snapshot;
}

} // namespace orion::platform::detail
