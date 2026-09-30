#include "orion/core/cpu_samples.h"

#include <algorithm>
#include <cmath>

namespace orion::core {

std::vector<CpuCoreLoad> normalizeCoreLoads(const std::span<const double> values)
{
    std::vector<CpuCoreLoad> normalized;
    normalized.reserve(values.size());
    for (std::size_t offset = 0; offset < values.size(); ++offset) {
        const double raw = values[offset];
        const double safe = std::isfinite(raw) ? std::clamp(raw, 0.0, 100.0) : 0.0;
        normalized.push_back({offset + 1, safe});
    }
    return normalized;
}

} // namespace orion::core

