#include "orion/core/observation_clock.h"

#include <cstdlib>
#include <iostream>

int main()
{
    using orion::core::ObservationClock;
    using namespace std::chrono_literals;
    const ObservationClock::TimePoint origin {};
    ObservationClock clock;
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::cerr << message << '\n'; }
    };
    clock.setPaused(true, origin + 1s);
    clock.start(origin + 10s);
    check(clock.snapshot(origin + 12s).pausedTime == 2s, "Pre-launch pause leaked into observed time.");
    clock.setPaused(false, origin + 12s);
    check(clock.snapshot(origin + 13s).active == 1s, "Start while paused lost the initial pause state.");
    clock.reset();
    clock.start(origin);
    check(!clock.setPaused(false, origin + 100ms), "Repeated resume changed generation.");
    clock.setPaused(true, origin + 120ms);
    check(!clock.setPaused(true, origin + 125ms), "Repeated pause changed the transition timestamp.");
    clock.setPaused(false, origin + 130ms);
    clock.setPaused(true, origin + 140ms);
    clock.setPaused(false, origin + 140ms + 250us);
    const auto state = clock.snapshot(origin + 200ms);
    check(state.active == 189750us && state.pausedTime == 10250us && state.generation == 4,
        "Short pauses between observations were missed or rounded away.");
    check(clock.snapshot(origin + 200ms).active == state.active, "Read-only snapshot advanced accounting.");
    clock.setPaused(true, origin + 210ms);
    const auto stopped = clock.finish(origin + 510ms);
    check(stopped.active == 199750us && stopped.pausedTime == 310250us && !stopped.running,
        "Stopping while paused lost the open pause interval.");
    clock.setPaused(false, origin + 2s);
    clock.setPaused(true, origin + 3s);
    const auto report = clock.finish(origin + 10s);
    check(report.active == stopped.active && report.pausedTime == stopped.pausedTime,
        "Closure/report work or repeated finish changed frozen observation time.");
    check(report.generation == stopped.generation + 2, "Closure missed a quick pause/resume cancellation.");
    clock.reset();
    clock.start(origin + 100s);
    check(clock.snapshot(origin + 100s).active == 0s && clock.snapshot(origin + 100s).generation == 0,
        "New run inherited prior timing or pause state.");
    for (int i = 0; i < 10000; ++i) {
        clock.setPaused(true, origin + 100s + std::chrono::microseconds(i * 10));
        clock.setPaused(false, origin + 100s + std::chrono::microseconds(i * 10 + 3));
    }
    const auto rapid = clock.finish(origin + 100s + 100ms);
    check(rapid.active == 70ms && rapid.pausedTime == 30ms && rapid.generation == 20000,
        "Repeated sub-millisecond transitions accumulated rounding error.");
    if (failures) return EXIT_FAILURE;
    std::cout << "Transition-based observation clock contracts passed.\n";
    return EXIT_SUCCESS;
}
