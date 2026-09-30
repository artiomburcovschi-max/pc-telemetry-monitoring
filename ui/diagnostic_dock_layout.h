#pragma once

#include <QList>
#include <QRect>
#include <QString>
#include <array>

class QDockWidget;
class QMainWindow;

namespace orion::app {

// Order: diagnostics, stress test, report. Widgets and their contents are retained.
using DiagnosticDocks = std::array<QDockWidget*, 3>;
struct DiagnosticLayoutRestoreResult {
    bool restored { false };
    int redockedPanels { 0 };
};

void resetDiagnosticDockLayout(QMainWindow& host, const DiagnosticDocks& docks);
bool hasReachableDockTitle(const QRect& frame, const QList<QRect>& availableScreens);
int recoverDiagnosticDockPositions(const DiagnosticDocks& docks, const QList<QRect>& availableScreens);
DiagnosticLayoutRestoreResult restoreDiagnosticDockLayout(QMainWindow& host,
    const DiagnosticDocks& docks, const QString& encodedState, const QList<QRect>& availableScreens);

} // namespace orion::app
