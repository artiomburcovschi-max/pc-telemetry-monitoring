#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orion::core {

enum class AutostartCategory {
    user,
    system,
};

struct AutostartEntry {
    std::string name;
    std::string command;
    std::string source;
    std::optional<bool> enabled;
    AutostartCategory category {AutostartCategory::user};
};

struct AutostartSnapshot {
    std::vector<AutostartEntry> entries;
    std::string source;
    std::string reason;
};

[[nodiscard]] constexpr std::string_view toString(const AutostartCategory category) noexcept
{
    return category == AutostartCategory::user ? "user" : "system";
}

} // namespace orion::core
