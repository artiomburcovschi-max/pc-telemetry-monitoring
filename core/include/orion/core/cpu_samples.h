#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace orion::core {

struct CpuCoreLoad {
    std::size_t index;
    double percent;

    [[nodiscard]] bool operator==(const CpuCoreLoad&) const = default;
};

[[nodiscard]] std::vector<CpuCoreLoad> normalizeCoreLoads(
    std::span<const double> values);

} // namespace orion::core

