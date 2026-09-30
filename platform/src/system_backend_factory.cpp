#include "orion/platform/system_backend.h"

#include <memory>

namespace orion::platform {

#if defined(ORION_FORCE_LINUX_BACKEND)
[[nodiscard]] std::unique_ptr<SystemBackend> makeLinuxSystemBackend();
#elif defined(_WIN32)
[[nodiscard]] std::unique_ptr<SystemBackend> makeWindowsSystemBackend();
#elif defined(__linux__)
[[nodiscard]] std::unique_ptr<SystemBackend> makeLinuxSystemBackend();
#endif

std::unique_ptr<SystemBackend> makeSystemBackend()
{
#if defined(ORION_FORCE_LINUX_BACKEND)
    return makeLinuxSystemBackend();
#elif defined(_WIN32)
    return makeWindowsSystemBackend();
#elif defined(__linux__)
    return makeLinuxSystemBackend();
#else
#error Unsupported ORION platform
#endif
}

} // namespace orion::platform
