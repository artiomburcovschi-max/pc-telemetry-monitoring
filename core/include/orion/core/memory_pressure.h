#pragma once

#include <optional>
#include <string>

namespace orion::core {

struct WindowsPagingAssessment {
    std::string pagingActivity {"unknown"};
    std::string hardFaultActivity {"unknown"};
    std::string memoryPressure {"unknown"};
    std::string interpretation {"counter_data_unavailable"};
};

[[nodiscard]] WindowsPagingAssessment assessWindowsPaging(
    std::optional<double> availableMemoryPercent,
    std::optional<double> commitUsedPercent,
    std::optional<double> pagesInputPerSecond,
    std::optional<double> pageReadsPerSecond,
    std::optional<double> pagesPerSecond) noexcept;

} // namespace orion::core
