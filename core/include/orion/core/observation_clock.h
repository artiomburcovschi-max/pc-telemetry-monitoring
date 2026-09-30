#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace orion::core {

// Explicit time inputs keep the arithmetic testable without sleeping. The owner
// serializes calls and takes steady_clock::now() inside the same lock.
class ObservationClock {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    struct Snapshot {
        Clock::duration active {};
        Clock::duration pausedTime {};
        std::uint64_t generation {0};
        bool paused {false};
        bool running {false};

        [[nodiscard]] double activeSeconds() const { return std::chrono::duration<double>(active).count(); }
        [[nodiscard]] double pausedSeconds() const { return std::chrono::duration<double>(pausedTime).count(); }
    };

    void reset() { state_ = {}; last_ = {}; }
    void start(TimePoint now)
    {
        state_.active = {};
        state_.pausedTime = {};
        state_.running = true;
        last_ = now;
    }
    bool setPaused(bool paused, TimePoint now)
    {
        if (state_.paused == paused) return false;
        accumulate(now);
        state_.paused = paused;
        ++state_.generation;
        return true;
    }
    Snapshot finish(TimePoint now)
    {
        accumulate(now);
        state_.running = false;
        return state_;
    }
    [[nodiscard]] Snapshot snapshot(TimePoint now) const
    {
        auto result = state_;
        if (result.running) {
            const auto elapsed = std::max(now - last_, Clock::duration::zero());
            (result.paused ? result.pausedTime : result.active) += elapsed;
        }
        return result;
    }

private:
    void accumulate(TimePoint now)
    {
        state_ = snapshot(now);
        last_ = std::max(last_, now);
    }
    Snapshot state_;
    TimePoint last_ {};
};

} // namespace orion::core
