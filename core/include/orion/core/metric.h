#pragma once

#include "orion/core/data_quality.h"

#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace orion::core {

template<typename T>
struct Metric {
    std::optional<T> value;
    std::optional<std::chrono::system_clock::time_point> observedAt;
    DataQuality quality {DataQuality::Unsupported};
    std::string source;
    std::string reason;

    [[nodiscard]] bool hasValue() const noexcept
    {
        return value.has_value();
    }

    [[nodiscard]] bool usable() const noexcept
    {
        return value.has_value() && isUsable(quality);
    }

    [[nodiscard]] T valueOr(T fallback) const
    {
        return value.value_or(std::move(fallback));
    }

    [[nodiscard]] static Metric valid(
        T metricValue,
        std::string metricSource = {},
        std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now())
    {
        return {
            std::move(metricValue),
            timestamp,
            DataQuality::Valid,
            std::move(metricSource),
            {},
        };
    }

    [[nodiscard]] static Metric estimated(
        T metricValue,
        std::string metricSource = {},
        std::string metricReason = {},
        std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now())
    {
        return {
            std::move(metricValue),
            timestamp,
            DataQuality::Estimated,
            std::move(metricSource),
            std::move(metricReason),
        };
    }

    [[nodiscard]] static Metric unavailable(
        const DataQuality metricQuality,
        std::string metricSource = {},
        std::string metricReason = {})
    {
        return {
            std::nullopt,
            std::nullopt,
            metricQuality,
            std::move(metricSource),
            std::move(metricReason),
        };
    }
};

} // namespace orion::core
