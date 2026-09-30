#include "orion/platform/process_collector.h"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace orion::platform {
namespace {

[[nodiscard]] bool numericName(const std::string& value)
{
    return !value.empty() && std::ranges::all_of(value, [](const unsigned char character) {
        return std::isdigit(character) != 0;
    });
}

[[nodiscard]] std::optional<std::uint64_t> unsignedFromText(const std::string& value)
{
    try {
        return static_cast<std::uint64_t>(std::stoull(value));
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::uint64_t> valueFromStatus(
    const std::filesystem::path& path,
    const std::string_view key)
{
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.starts_with(key)) {
            continue;
        }
        std::istringstream stream(line.substr(key.size()));
        std::uint64_t value = 0;
        std::string unit;
        if (stream >> value >> unit) {
            if (unit == "kB") {
                value *= 1024ULL;
            }
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> valueFromIo(
    const std::filesystem::path& path,
    const std::string_view key)
{
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.starts_with(key)) {
            continue;
        }
        return unsignedFromText(line.substr(key.size()));
    }
    return std::nullopt;
}

struct ParsedStat {
    std::string name;
    char state {'?'};
    std::uint32_t parentPid {0};
    std::uint32_t threadCount {0};
    std::uint64_t cpuTicks {0};
    // Field 22 of /proc/<pid>/stat: start time in clock ticks since boot. Together
    // with the PID it uniquely identifies a process for the lifetime of a boot, so it
    // plays the role of the Windows creation time. Zero means "unknown".
    std::uint64_t startTicks {0};
};

[[nodiscard]] std::optional<ParsedStat> parseStat(const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::string line;
    std::getline(input, line);
    const auto left = line.find('(');
    const auto right = line.rfind(')');
    if (left == std::string::npos || right == std::string::npos || right + 2 >= line.size()) {
        return std::nullopt;
    }
    ParsedStat result;
    result.name = line.substr(left + 1, right - left - 1);
    std::istringstream stream(line.substr(right + 2));
    std::vector<std::string> values;
    std::string value;
    while (stream >> value) {
        values.push_back(std::move(value));
    }
    if (values.size() <= 17) {
        return std::nullopt;
    }
    const auto parentPid = unsignedFromText(values[1]);
    const auto userTicks = unsignedFromText(values[11]);
    const auto kernelTicks = unsignedFromText(values[12]);
    const auto threads = unsignedFromText(values[17]);
    if (!parentPid.has_value() || !userTicks.has_value() || !kernelTicks.has_value()
        || !threads.has_value()) {
        return std::nullopt;
    }
    result.state = values[0].empty() ? '?' : values[0].front();
    result.parentPid = static_cast<std::uint32_t>(*parentPid);
    result.threadCount = static_cast<std::uint32_t>(*threads);
    result.cpuTicks = *userTicks + *kernelTicks;
    if (values.size() > 19) {
        result.startTicks = unsignedFromText(values[19]).value_or(0);
    }
    return result;
}

// The kernel truncates comm (field 2 of /proc/<pid>/stat) to 15 characters, so a
// long executable name such as "orion_ui_contract_tests" would otherwise be
// reported as "orion_ui_contra". Windows reports the full image name, so searches
// and name-based matching would silently behave differently on Linux. When the
// comm looks truncated, recover the full name from /proc/<pid>/exe or argv[0], but
// only accept a candidate that begins with the truncated comm (a process may set
// its comm to something unrelated to its binary, and kernel threads have neither).
constexpr std::size_t kCommMaxLength = 15;

[[nodiscard]] std::string stripDeletedSuffix(std::string value)
{
    constexpr std::string_view suffix = " (deleted)";
    if (value.size() > suffix.size() && value.ends_with(suffix)) {
        value.resize(value.size() - suffix.size());
    }
    return value;
}

[[nodiscard]] std::string fullProcessName(const std::filesystem::path& procDir, const std::string& comm)
{
    if (comm.size() < kCommMaxLength) {
        return comm;
    }
    std::error_code error;
    const auto target = std::filesystem::read_symlink(procDir / "exe", error);
    if (!error) {
        const auto base = stripDeletedSuffix(target.filename().string());
        if (base.size() >= comm.size() && base.starts_with(comm)) {
            return base;
        }
    }
    std::ifstream cmdline(procDir / "cmdline", std::ios::binary);
    std::string argv0;
    if (cmdline && std::getline(cmdline, argv0, '\0') && !argv0.empty()) {
        const auto base = std::filesystem::path(argv0).filename().string();
        if (base.size() >= comm.size() && base.starts_with(comm)) {
            return base;
        }
    }
    return comm;
}

struct PreviousCpu {
    std::uint64_t ticks {0};
    std::chrono::steady_clock::time_point capturedAt;
};

class LinuxProcessCollector final : public ProcessCollector {
public:
    [[nodiscard]] orion::core::ProcessSnapshot sample() override
    {
        orion::core::ProcessSnapshot snapshot;
        snapshot.source = "/proc process/stat/status/io";
        const long clockTicks = sysconf(_SC_CLK_TCK);
        const long processorCount = sysconf(_SC_NPROCESSORS_ONLN);
        snapshot.logicalProcessorCount = static_cast<std::uint32_t>(std::max<long>(processorCount, 1));
        if (clockTicks <= 0) {
            snapshot.reason = "_SC_CLK_TCK is unavailable";
            return snapshot;
        }
        const auto now = std::chrono::steady_clock::now();
        std::unordered_map<std::uint32_t, PreviousCpu> nextPrevious;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator("/proc", error)) {
            if (error || !numericName(entry.path().filename().string())) {
                continue;
            }
            const auto pidText = entry.path().filename().string();
            const auto pid = unsignedFromText(pidText);
            const auto stat = parseStat(entry.path() / "stat");
            if (!pid.has_value() || !stat.has_value()) {
                continue;
            }
            // A zombie (Z) or dead (X) entry has already exited and only awaits reaping by
            // its parent. Windows never lists exited processes, and treating a zombie as
            // running would make "did the launched tree exit?" checks hang forever when
            // PID 1 does not reap (for example a container without an init process).
            if (stat->state == 'Z' || stat->state == 'X') {
                continue;
            }
            orion::core::ProcessInfo process;
            process.pid = static_cast<std::uint32_t>(*pid);
            process.creationIdentity = stat->startTicks;
            process.parentPid = stat->parentPid;
            process.threadCount = stat->threadCount;
            process.name = stat->name.empty() ? "PID " + pidText : fullProcessName(entry.path(), stat->name);
            const auto previous = previousCpu_.find(process.pid);
            if (previous != previousCpu_.end()) {
                const double seconds = std::chrono::duration<double>(now - previous->second.capturedAt).count();
                if (seconds > 0.01 && stat->cpuTicks >= previous->second.ticks) {
                    process.cpuPercent = 100.0
                        * static_cast<double>(stat->cpuTicks - previous->second.ticks)
                        / (seconds * static_cast<double>(clockTicks));
                }
            }
            nextPrevious.emplace(process.pid, PreviousCpu {stat->cpuTicks, now});
            process.workingSetBytes = valueFromStatus(entry.path() / "status", "VmRSS:");
            process.privateBytes = valueFromStatus(entry.path() / "status", "RssAnon:");
            process.readBytes = valueFromIo(entry.path() / "io", "read_bytes:");
            process.writeBytes = valueFromIo(entry.path() / "io", "write_bytes:");
            snapshot.processes.push_back(std::move(process));
        }
        previousCpu_ = std::move(nextPrevious);
        std::ranges::sort(snapshot.processes, [](const auto& left, const auto& right) {
            return left.name < right.name;
        });
        return snapshot;
    }

private:
    std::unordered_map<std::uint32_t, PreviousCpu> previousCpu_;
};

} // namespace

std::unique_ptr<ProcessCollector> makeProcessCollector()
{
    return std::make_unique<LinuxProcessCollector>();
}

} // namespace orion::platform
