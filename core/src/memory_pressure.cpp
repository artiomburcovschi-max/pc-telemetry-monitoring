#include "orion/core/memory_pressure.h"

#include <algorithm>

namespace orion::core {

WindowsPagingAssessment assessWindowsPaging(
    const std::optional<double> availableMemoryPercent,
    const std::optional<double> commitUsedPercent,
    const std::optional<double> pagesInputPerSecond,
    const std::optional<double> pageReadsPerSecond,
    const std::optional<double> pagesPerSecond) noexcept
{
    constexpr double pagesInputActivePerSecond = 100.0;
    constexpr double pageReadsActivePerSecond = 5.0;
    constexpr double pagesHighPerSecond = 1000.0;
    constexpr double availableLowPercent = 15.0;
    constexpr double commitHighPercent = 85.0;

    const bool countersKnown = pagesInputPerSecond.has_value()
        || pageReadsPerSecond.has_value() || pagesPerSecond.has_value();
    const bool hardFaultActive = pagesInputPerSecond.value_or(0.0) >= pagesInputActivePerSecond
        || pageReadsPerSecond.value_or(0.0) >= pageReadsActivePerSecond
        || pagesPerSecond.value_or(0.0) >= pagesHighPerSecond;
    const bool shortage = availableMemoryPercent.value_or(100.0) < availableLowPercent
        || commitUsedPercent.value_or(0.0) >= commitHighPercent;

    WindowsPagingAssessment result;
    result.hardFaultActivity = countersKnown
        ? hardFaultActive ? "active" : "idle"
        : "unknown";
    if (hardFaultActive && shortage) {
        result.pagingActivity = "active";
        result.memoryPressure = "confirmed";
        result.interpretation = "hard_faults_with_memory_shortage";
    } else if (shortage) {
        result.pagingActivity = "unknown";
        result.memoryPressure = "possible";
        result.interpretation = "memory_shortage_without_confirmed_paging";
    } else if (hardFaultActive) {
        result.pagingActivity = "unconfirmed";
        result.memoryPressure = "not_observed";
        result.interpretation = "hard_fault_reads_without_memory_shortage";
    } else if (countersKnown) {
        result.pagingActivity = "idle";
        result.memoryPressure = "not_observed";
        result.interpretation = "no_memory_shortage_or_hard_fault_burst";
    }
    return result;
}

} // namespace orion::core
