#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orion::core {

struct ProcessInfo {
    std::uint32_t pid {0};
    std::uint32_t parentPid {0};
    std::string name;
    std::uint32_t threadCount {0};
    std::optional<double> cpuPercent;
    std::optional<std::uint64_t> workingSetBytes;
    std::optional<std::uint64_t> privateBytes;
    std::optional<std::uint32_t> handleCount;
    std::optional<std::uint64_t> pageFaultCount;
    std::optional<std::uint64_t> readBytes;
    std::optional<std::uint64_t> writeBytes;
    std::optional<double> contextSwitchesPerSecond;
    // Native creation timestamp; distinguishes PID reuse. Zero is never a valid identity.
    std::uint64_t creationIdentity {0};
};

struct ProcessSnapshot {
    std::vector<ProcessInfo> processes;
    std::uint32_t logicalProcessorCount {0};
    std::string source;
    std::string reason;
};

} // namespace orion::core
