#pragma once

#include "orion/core/autostart_entry.h"

#include <memory>

namespace orion::platform {

class AutostartCollector {
public:
    virtual ~AutostartCollector() = default;
    [[nodiscard]] virtual orion::core::AutostartSnapshot scan() = 0;
};

[[nodiscard]] std::unique_ptr<AutostartCollector> makeAutostartCollector();

} // namespace orion::platform
