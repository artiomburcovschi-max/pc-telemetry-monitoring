#pragma once

#include <optional>
#include <string_view>

namespace orion::core {

enum class DataQuality {
    Valid,
    Stale,
    Estimated,
    Unavailable,
    Unsupported,
    PermissionDenied,
    CollectorError,
};

[[nodiscard]] constexpr std::string_view toString(const DataQuality quality) noexcept
{
    switch (quality) {
    case DataQuality::Valid:
        return "valid";
    case DataQuality::Stale:
        return "stale";
    case DataQuality::Estimated:
        return "estimated";
    case DataQuality::Unavailable:
        return "unavailable";
    case DataQuality::Unsupported:
        return "unsupported";
    case DataQuality::PermissionDenied:
        return "permission_denied";
    case DataQuality::CollectorError:
        return "collector_error";
    }
    return "collector_error";
}

[[nodiscard]] constexpr std::optional<DataQuality> dataQualityFromString(
    const std::string_view value) noexcept
{
    if (value == "valid") {
        return DataQuality::Valid;
    }
    if (value == "stale") {
        return DataQuality::Stale;
    }
    if (value == "estimated") {
        return DataQuality::Estimated;
    }
    if (value == "unavailable") {
        return DataQuality::Unavailable;
    }
    if (value == "unsupported") {
        return DataQuality::Unsupported;
    }
    if (value == "permission_denied") {
        return DataQuality::PermissionDenied;
    }
    if (value == "collector_error") {
        return DataQuality::CollectorError;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool isUsable(const DataQuality quality) noexcept
{
    return quality == DataQuality::Valid
        || quality == DataQuality::Stale
        || quality == DataQuality::Estimated;
}

} // namespace orion::core
