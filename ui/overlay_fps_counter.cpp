#include "overlay_fps_counter.h"

#include <algorithm>
#include <utility>

namespace orion::app {

UiFpsCounter::UiFpsCounter(const int windowSize)
    : windowSize_(std::clamp(windowSize, 2, 240))
{
    reset();
}

void UiFpsCounter::reset()
{
    deltasNanoseconds_.clear();
    fps_ = 0.0;
    timer_.restart();
    lastNanoseconds_ = 0;
    primed_ = false;
}

double UiFpsCounter::tick()
{
    const qint64 now = timer_.nsecsElapsed();
    if (!primed_) {
        lastNanoseconds_ = now;
        primed_ = true;
        return fps_;
    }
    const qint64 delta = now - lastNanoseconds_;
    lastNanoseconds_ = now;
    if (delta > 0) {
        deltasNanoseconds_.enqueue(delta);
        while (deltasNanoseconds_.size() > windowSize_) {
            deltasNanoseconds_.dequeue();
        }
    }
    if (!deltasNanoseconds_.isEmpty()) {
        qint64 sum = 0;
        for (const qint64 value : std::as_const(deltasNanoseconds_)) {
            sum += value;
        }
        const double average = static_cast<double>(sum)
            / static_cast<double>(deltasNanoseconds_.size());
        fps_ = average > 0.0 ? 1'000'000'000.0 / average : 0.0;
    }
    return fps_;
}

double UiFpsCounter::fps() const noexcept
{
    return fps_;
}

} // namespace orion::app
