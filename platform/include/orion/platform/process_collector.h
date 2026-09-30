#pragma once

#include "orion/core/process_info.h"

#include <memory>

namespace orion::platform {

class ProcessCollector {
public:
    virtual ~ProcessCollector() = default;
    [[nodiscard]] virtual orion::core::ProcessSnapshot sample() = 0;
};

[[nodiscard]] std::unique_ptr<ProcessCollector> makeProcessCollector();

} // namespace orion::platform
