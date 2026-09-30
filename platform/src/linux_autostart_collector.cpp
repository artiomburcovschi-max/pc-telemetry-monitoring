#include "orion/platform/autostart_collector.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace orion::platform {
namespace {

[[nodiscard]] std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] bool falseValue(const std::string& value)
{
    std::string normalized = trim(value);
    std::ranges::transform(normalized, normalized.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return normalized == "false";
}

[[nodiscard]] bool trueValue(const std::string& value)
{
    std::string normalized = trim(value);
    std::ranges::transform(normalized, normalized.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return normalized == "true";
}

[[nodiscard]] std::optional<orion::core::AutostartEntry> parseDesktopFile(
    const std::filesystem::path& path,
    const std::string& source,
    const orion::core::AutostartCategory category)
{
    std::ifstream input(path);
    if (!input) {
        return std::nullopt;
    }
    orion::core::AutostartEntry entry;
    entry.name = path.filename().string();
    entry.source = source;
    entry.enabled = true;
    entry.category = category;
    bool inDesktopEntry = false;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (line == "[Desktop Entry]") {
            inDesktopEntry = true;
            continue;
        }
        if (!line.empty() && line.front() == '[') {
            inDesktopEntry = false;
            continue;
        }
        if (!inDesktopEntry) {
            continue;
        }
        if (line.starts_with("Name=") && entry.name == path.filename().string()) {
            entry.name = line.substr(5);
        } else if (line.starts_with("Exec=")) {
            entry.command = line.substr(5);
        } else if (line.starts_with("Hidden=") && trueValue(line.substr(7))) {
            entry.enabled = false;
        } else if (line.starts_with("X-GNOME-Autostart-enabled=")
            && falseValue(line.substr(26))) {
            entry.enabled = false;
        }
    }
    return entry;
}

void readDesktopDirectory(
    std::vector<orion::core::AutostartEntry>& entries,
    const std::filesystem::path& directory,
    const std::string& source,
    const orion::core::AutostartCategory category)
{
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return;
    }
    for (const auto& item : std::filesystem::directory_iterator(
             directory, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (error || !item.is_regular_file(error) || item.path().extension() != ".desktop") {
            continue;
        }
        if (auto parsed = parseDesktopFile(item.path(), source, category); parsed.has_value()) {
            entries.push_back(std::move(*parsed));
        }
    }
}

void readSystemdUserUnits(std::vector<orion::core::AutostartEntry>& entries)
{
    // The command is fixed, read-only and mirrors the Python reference collector.
    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen("systemctl --user list-unit-files --type=service --no-legend 2>/dev/null", "r"),
        &pclose);
    if (!pipe) {
        return;
    }
    std::array<char, 4096> buffer {};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        std::istringstream line(buffer.data());
        std::string unit;
        std::string state;
        if (!(line >> unit >> state) || !unit.ends_with(".service")) {
            continue;
        }
        entries.push_back({
            unit,
            {},
            "systemd --user",
            state == "enabled" || state == "enabled-runtime",
            orion::core::AutostartCategory::user,
        });
    }
}

class LinuxAutostartCollector final : public AutostartCollector {
public:
    [[nodiscard]] orion::core::AutostartSnapshot scan() override
    {
        orion::core::AutostartSnapshot snapshot;
        snapshot.source = "XDG autostart + systemd --user";
        if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            readDesktopDirectory(snapshot.entries,
                std::filesystem::path(home) / ".config/autostart",
                "Startup (user folder)", orion::core::AutostartCategory::user);
        }
        readDesktopDirectory(snapshot.entries, "/etc/xdg/autostart",
            "Startup (system desktop component)", orion::core::AutostartCategory::system);
        readSystemdUserUnits(snapshot.entries);
        std::ranges::sort(snapshot.entries, [](const auto& left, const auto& right) {
            if (left.category != right.category) {
                return left.category == orion::core::AutostartCategory::user;
            }
            return left.name < right.name;
        });
        return snapshot;
    }
};

} // namespace

std::unique_ptr<AutostartCollector> makeAutostartCollector()
{
    return std::make_unique<LinuxAutostartCollector>();
}

} // namespace orion::platform
