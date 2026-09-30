#include "orion/core/metric_window.h"

#include <numeric>
#include <stdexcept>

namespace orion::core {

MetricWindow::MetricWindow(const double windowSeconds)
    : windowSeconds_(windowSeconds)
{
    if (windowSeconds_ <= 0.0) {
        throw std::invalid_argument("MetricWindow duration must be positive");
    }
}

void MetricWindow::add(
    const std::optional<double> value,
    const double monotonicSeconds)
{
    expire(monotonicSeconds);
    if (value.has_value()) {
        samples_.push_back({monotonicSeconds, *value});
    }
}

void MetricWindow::expire(const double monotonicSeconds)
{
    const double cutoff = monotonicSeconds - windowSeconds_;
    while (!samples_.empty() && samples_.front().timestamp < cutoff) {
        samples_.pop_front();
    }
}

void MetricWindow::clear() noexcept
{
    samples_.clear();
}

std::optional<double> MetricWindow::average() const noexcept
{
    if (samples_.empty()) {
        return std::nullopt;
    }
    const double total = std::accumulate(
        samples_.begin(),
        samples_.end(),
        0.0,
        [](const double sum, const Sample& sample) { return sum + sample.value; });
    return total / static_cast<double>(samples_.size());
}

std::optional<double> MetricWindow::lastValue() const noexcept
{
    return samples_.empty() ? std::nullopt : std::optional {samples_.back().value};
}

bool MetricWindow::hasEnoughHistory(const double monotonicSeconds) const noexcept
{
    return !samples_.empty()
        && monotonicSeconds - samples_.front().timestamp >= windowSeconds_ * 0.8;
}

std::size_t MetricWindow::size() const noexcept
{
    return samples_.size();
}

} // namespace orion::core

