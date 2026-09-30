#pragma once

#include "orion/core/process_info.h"
#include <QSet>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

namespace orion::app {
// Retains native process objects through the observation. Destruction only closes
// handles: stopping observation must never terminate the launched application.
class AppProcessSession final {
public:
    AppProcessSession();
    ~AppProcessSession();
    bool launch(const QString& executablePath);
    void observe(const orion::core::ProcessSnapshot& snapshot);
    QSet<quint32> alivePids() const;
    bool matches(const orion::core::ProcessInfo& process) const;
    int requestClose(const std::function<bool()>& cancelled);
    bool forceTerminate(quint32 pid);
    std::optional<int> rootExitCode() const;
    quint32 rootPid() const;
    QStringList errors() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
