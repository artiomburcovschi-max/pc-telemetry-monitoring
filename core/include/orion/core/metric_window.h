#pragma once

#include <cstddef>
#include <deque>
#include <optional>

namespace orion::core {

class MetricWindow final {
public:
    explicit MetricWindow(double windowSeconds);

    void add(std::optional<double> value, double monotonicSeconds);
    void expire(double monotonicSeconds);
    void clear() noexcept;

    [[nodiscard]] std::optional<double> average() const noexcept;
    [[nodiscard]] std::optional<double> lastValue() const noexcept;
    [[nodiscard]] bool hasEnoughHistory(double monotonicSeconds) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Sample {
        double timestamp;
        double value;
    };

    double windowSeconds_;
    std::deque<Sample> samples_;
};

} // namespace orion::core

