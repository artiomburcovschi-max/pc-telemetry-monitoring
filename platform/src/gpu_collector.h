#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace orion::platform::detail {

struct GpuSample {
    std::string name {"Unknown GPU"};
    std::optional<double> usagePercent;
    std::optional<double> temperatureC;
    std::optional<double> fanPercent;
    std::optional<std::uint64_t> memoryTotalBytes;
    std::optional<std::uint64_t> memoryUsedBytes;
    std::string source;
    std::string reason;
};

[[nodiscard]] GpuSample readGpuSample();

} // namespace orion::platform::detail
