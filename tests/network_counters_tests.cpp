#include "orion/core/network_counters.h"

#include <cstdlib>
#include <iostream>
#include <limits>

int main()
{
    using namespace orion::core;
    using Counter = Metric<std::uint64_t>;
    bool ok = true;
    const auto check = [&ok](bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            ok = false;
        }
    };
    NetworkCounterTracker tracker;
    TelemetryData data;
    auto time = std::chrono::system_clock::now();
    const auto set = [&](std::uint64_t errors, std::uint64_t drops)
    {
        time += std::chrono::seconds(1);
        data.totalErrors = Counter::valid(errors, "fixture", time);
        data.totalDrops = Counter::valid(drops, "fixture", time);
    };
    data.netTotalReceivedBytes = Counter::valid(4ULL * 1024 * 1024 * 1024, "fixture", time);
    data.netTotalSentBytes = Counter::valid(0, "fixture", time);
    set(10, 20);
    auto result = tracker.update(data);
    check(result.receivedBytes.value == data.netTotalReceivedBytes.value && result.sentBytes.value == 0,
        "Cumulative bytes or genuine zero lost.");
    check(!result.intervalErrors.hasValue() && !result.intervalDrops.hasValue(), "First interval invented.");
    set(12, 25);
    result = tracker.update(data);
    check(result.intervalErrors.value == 2 && result.intervalDrops.value == 5, "Consecutive deltas wrong.");
    set(12, 25);
    result = tracker.update(data);
    check(result.intervalErrors.value == 0 && result.intervalDrops.value == 0, "Zero delta lost.");
    result = tracker.update(data);
    check(!result.intervalErrors.hasValue(), "Duplicate timestamp produced an interval.");
    set(1, 26);
    result = tracker.update(data);
    check(!result.intervalErrors.hasValue() && result.intervalErrors.reason == "counter_reset"
            && result.intervalDrops.value == 1,
        "Reset underflowed or erased independent counter.");
    set(3, 28);
    data.totalErrors.quality = DataQuality::Stale;
    result = tracker.update(data);
    check(!result.intervalErrors.hasValue(), "Stale sample became a current interval.");
    set(100, 29);
    result = tracker.update(data);
    check(!result.intervalErrors.hasValue() && result.intervalDrops.value == 1, "Gap was bridged.");
    tracker.reset();
    set(200, 100);
    result = tracker.update(data);
    check(!result.intervalErrors.hasValue() && result.errors.value == 200,
        "Pause reset lost total or bridged interval.");
    set(201, 101);
    data.totalErrors.source = "other provider";
    result = tracker.update(data);
    check(!result.intervalErrors.hasValue(), "Provider change was bridged.");
    tracker.reset();
    set(std::numeric_limits<std::uint64_t>::max() - 2, 1);
    (void)tracker.update(data);
    set(std::numeric_limits<std::uint64_t>::max(), 2);
    result = tracker.update(data);
    check(result.intervalErrors.value == 2, "64-bit precision lost above double integer range.");
    data.netCounterScope = "new interface set";
    set(std::numeric_limits<std::uint64_t>::max(), 9);
    result = tracker.update(data);
    check(!result.intervalDrops.hasValue() && result.drops.value == 9, "Interface change was bridged.");
    data.totalErrors = Counter::unavailable(DataQuality::PermissionDenied, "fixture", "denied");
    result = tracker.update(data);
    check(!result.intervalErrors.hasValue() && result.intervalErrors.quality == DataQuality::PermissionDenied,
        "Unavailable quality lost.");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
