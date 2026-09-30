#include "orion/core/cpu_samples.h"
#include "orion/core/metric_window.h"
#include "orion/core/metric.h"
#include "orion/core/memory_pressure.h"
#include "orion/core/session_peaks.h"
#include "orion/core/thresholds.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void checkNear(
    const double actual,
    const double expected,
    const double tolerance,
    const std::string_view message)
{
    check(std::abs(actual - expected) <= tolerance, message);
}

void testThresholds()
{
    using namespace orion::core;

    check(levelForPercent(std::nullopt, 95.0) == StatusLevel::Unknown,
          "missing percentage is unknown");
    check(levelForPercent(76.0, 95.0) == StatusLevel::Warning,
          "warning ratio matches Python contract");
    check(levelForPercent(95.0, 95.0) == StatusLevel::Critical,
          "critical percentage boundary is inclusive");
    check(levelForTemperature(std::nullopt, "cpu") == StatusLevel::Unknown,
          "missing temperature is unknown");
    check(levelForTemperature(50.0, "hdd") == StatusLevel::Warning,
          "HDD temperature profile is component-specific");
    check(levelForTemperature(50.0, "cpu") == StatusLevel::Ok,
          "CPU temperature profile differs from HDD");
    check(levelForTemperature(80.0, "nvme") == StatusLevel::Warning,
          "NVMe 80 C is warning");
    check(levelForTemperature(80.0, "ssd") == StatusLevel::Critical,
          "SSD 80 C is critical");
    check(worse(StatusLevel::Unknown, StatusLevel::Warning) == StatusLevel::Warning,
          "unknown never hides a known warning");
    check(worse(StatusLevel::Unknown, StatusLevel::Unknown) == StatusLevel::Unknown,
          "two unknown values stay unknown");
}

void testMetricWindow()
{
    orion::core::MetricWindow window(5.0);
    window.add(99.0, 0.0);
    window.add(99.0, 4.0);
    check(window.average().has_value(), "window has an average after samples");
    checkNear(window.average().value_or(0.0), 99.0, 0.001,
              "rolling average matches source behavior");
    window.add(std::nullopt, 100.0);
    check(!window.average().has_value(),
          "missing current sample still expires stale history");
    check(!window.lastValue().has_value(), "expired window has no last value");
    check(!window.hasEnoughHistory(100.0), "expired window has no history");

    bool rejected = false;
    try {
        [[maybe_unused]] orion::core::MetricWindow invalid(0.0);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "non-positive rolling window is rejected");
}

void testCpuSamples()
{
    const std::vector values {
        -5.0,
        0.0,
        0.4,
        37.5,
        140.0,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
    };
    const auto normalized = orion::core::normalizeCoreLoads(values);
    check(normalized.size() == values.size(), "every logical CPU is preserved");
    check(normalized[0].index == 1 && normalized[0].percent == 0.0,
          "negative CPU sample is clamped");
    check(normalized[1].index == 2 && normalized[1].percent == 0.0,
          "idle CPU sample is preserved");
    checkNear(normalized[2].percent, 0.4, 0.001, "sub-one-percent sample is preserved");
    checkNear(normalized[3].percent, 37.5, 0.001, "normal CPU sample is unchanged");
    check(normalized[4].percent == 100.0, "high CPU sample is clamped");
    check(normalized[5].percent == 0.0 && normalized[6].percent == 0.0,
          "non-finite CPU samples become zero");
}

void testSessionPeaks()
{
    using Clock = orion::core::SessionPeaksTracker::Clock;
    const auto start = Clock::time_point {};
    orion::core::SessionPeaksTracker tracker(start);

    orion::core::TelemetryData first;
    first.cpuUsagePercent = orion::core::Metric<double>::valid(42.0, "test");
    first.ramUsagePercent = orion::core::Metric<double>::valid(67.0, "test");
    first.gpuUsagePercent = orion::core::Metric<double>::valid(15.0, "test");
    first.cpuTemperatureC = orion::core::Metric<double>::valid(60.0, "test");
    tracker.update(first);

    orion::core::TelemetryData second;
    second.cpuUsagePercent = orion::core::Metric<double>::valid(20.0, "test");
    second.ramUsagePercent = orion::core::Metric<double>::valid(70.0, "test");
    second.gpuUsagePercent = orion::core::Metric<double>::valid(90.0, "test");
    second.cpuTemperatureC = orion::core::Metric<double>::valid(55.0, "test");
    second.gpuTemperatureC = orion::core::Metric<double>::valid(75.0, "test");
    tracker.update(second);

    check(tracker.peaks().cpu == std::optional {42.0}, "CPU peak never moves backwards");
    check(tracker.peaks().ram == std::optional {70.0}, "RAM peak updates");
    check(tracker.peaks().gpu == std::optional {90.0}, "GPU peak updates");
    check(tracker.peaks().cpuTemperatureC == std::optional {60.0}, "temperature peak updates safely");
    check(tracker.peaks().gpuTemperatureC == std::optional {75.0}, "optional GPU temperature peak appears");

    orion::core::TelemetryData unavailable;
    unavailable.cpuUsagePercent = orion::core::Metric<double>::unavailable(
        orion::core::DataQuality::CollectorError,
        "test",
        "collector failed");
    tracker.update(unavailable);
    check(tracker.peaks().cpu == std::optional {42.0},
          "unavailable value never turns into a zero peak");
    checkNear(
        tracker.uptimeSeconds(start + std::chrono::seconds {12}),
        12.0,
        0.001,
        "session uptime uses monotonic time");
}

void testMetricContract()
{
    using orion::core::DataQuality;
    using orion::core::Metric;

    const auto missing = Metric<double>::unavailable(
        DataQuality::PermissionDenied,
        "sensor-provider",
        "access denied");
    check(!missing.hasValue(), "unavailable metric has no invented value");
    check(!missing.usable(), "permission-denied metric is not usable");
    check(orion::core::toString(missing.quality) == "permission_denied",
          "quality string matches Python diagnostic contract");

    const auto estimated = Metric<double>::estimated(12.5, "fallback", "derived");
    check(estimated.usable(), "estimated value remains usable with explicit quality");
    check(estimated.observedAt.has_value(), "available metric has an observation time");
}

void testWindowsPagingInterpretation()
{
    const auto ample = orion::core::assessWindowsPaging(
        51.6, 44.0, 1533.0, 12.0, 1533.0);
    check(ample.hardFaultActivity == "active", "Windows hard-fault burst is detected");
    check(ample.pagingActivity == "unconfirmed",
          "hard faults with ample RAM are not called pagefile pressure");
    check(ample.memoryPressure == "not_observed",
          "ample RAM and commit prevent a false pressure alarm");
    check(ample.interpretation == "hard_fault_reads_without_memory_shortage",
          "Windows file-backed fault ambiguity is explicit");

    const auto pressured = orion::core::assessWindowsPaging(
        18.0, 91.0, 500.0, 10.0, 500.0);
    check(pressured.pagingActivity == "active"
              && pressured.memoryPressure == "confirmed",
          "high commit plus hard faults confirms pressure");

    const auto quiet = orion::core::assessWindowsPaging(
        40.0, 50.0, 0.0, 0.0, 0.0);
    check(quiet.pagingActivity == "idle" && quiet.memoryPressure == "not_observed",
          "occupied pagefile without I/O is not active paging");
}

} // namespace

int main()
{
    testThresholds();
    testMetricWindow();
    testCpuSamples();
    testSessionPeaks();
    testMetricContract();
    testWindowsPagingInterpretation();

    if (failures != 0) {
        std::cerr << failures << " native core test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All ORION native core tests passed.\n";
    return EXIT_SUCCESS;
}
