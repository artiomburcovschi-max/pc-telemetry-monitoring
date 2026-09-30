#include "diagnostic_dock_layout.h"

#include <QByteArray>
#include <QDockWidget>
#include <QMainWindow>
#include <QRectF>
#include <algorithm>

namespace orion::app {

void resetDiagnosticDockLayout(QMainWindow& host, const DiagnosticDocks& docks)
{
    for (auto* dock : docks) {
        dock->setFloating(false);
        host.removeDockWidget(dock);
    }
    host.addDockWidget(Qt::LeftDockWidgetArea, docks[0]);
    host.addDockWidget(Qt::RightDockWidgetArea, docks[1]);
    host.splitDockWidget(docks[1], docks[2], Qt::Vertical);
    host.resizeDocks({docks[0], docks[1]}, {590, 400}, Qt::Horizontal);
    host.resizeDocks({docks[1], docks[2]}, {280, 430}, Qt::Vertical);
    for (auto* dock : docks) dock->show();
}

bool hasReachableDockTitle(const QRect& frame, const QList<QRect>& availableScreens)
{
    if (frame.width() <= 0 || frame.height() <= 0) return false;
    // Logical Qt coordinates (including negative monitor origins), not physical pixels.
    // A visible body is not sufficient: the user must be able to grab the title.
    const QRectF title(frame.x(), frame.y(), frame.width(), std::min(32, frame.height()));
    for (const auto& screen : availableScreens) {
        const auto visible = title.intersected(QRectF(screen));
        if (visible.width() >= std::min(160, frame.width())
            && visible.height() >= std::min(24, frame.height())) return true;
    }
    return false;
}

int recoverDiagnosticDockPositions(const DiagnosticDocks& docks, const QList<QRect>& availableScreens)
{
    int recovered = 0;
    for (auto* dock : docks) {
        if (dock->isFloating() && !hasReachableDockTitle(dock->frameGeometry(), availableScreens)) {
            dock->setFloating(false);
            ++recovered;
        }
        // These three panels are intentionally not closable; stale hidden state must not lose one.
        dock->show();
    }
    return recovered;
}

DiagnosticLayoutRestoreResult restoreDiagnosticDockLayout(QMainWindow& host,
    const DiagnosticDocks& docks, const QString& encodedState, const QList<QRect>& availableScreens)
{
    resetDiagnosticDockLayout(host, docks);
    // Three panels produce a small state. Bound decoding and reject permissive Base64 repairs.
    if (encodedState.isEmpty() || encodedState.size() > 64 * 1024) return {};
    const auto encoded = encodedState.toLatin1();
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.toBase64() != encoded) return {};
    // Native saves version 1; the Python reference used QMainWindow's default version 0.
    if (!host.restoreState(decoded.decoded, 1) && !host.restoreState(decoded.decoded, 0)) {
        resetDiagnosticDockLayout(host, docks);
        return {};
    }
    return {true, recoverDiagnosticDockPositions(docks, availableScreens)};
}

} // namespace orion::app
