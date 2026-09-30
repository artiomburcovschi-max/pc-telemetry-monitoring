#pragma once

#include <string_view>

namespace orion::core {

enum class StatusLevel {
    Unknown,
    Ok,
    Warning,
    Critical,
};

[[nodiscard]] constexpr std::string_view toString(StatusLevel level) noexcept
{
    switch (level) {
    case StatusLevel::Unknown:
        return "unknown";
    case StatusLevel::Ok:
        return "ok";
    case StatusLevel::Warning:
        return "warn";
    case StatusLevel::Critical:
        return "critical";
    }
    return "unknown";
}

} // namespace orion::core

