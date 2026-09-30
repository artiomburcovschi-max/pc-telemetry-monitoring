#include "independent_dock_widget.h"

#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QScopedValueRollback>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace orion::app {

IndependentDockWidget::IndependentDockWidget(const QString& title, QWidget* parent)
    : QDockWidget(title, parent)
{
    // Only the application's normal Exit path owns shutdown. A floating card
    // remains part of the same QObject tree and is deleted with the main window.
    setAttribute(Qt::WA_QuitOnClose, false);
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating) detachWindowOwner();
    });
}

bool IndependentDockWidget::event(QEvent* event)
{
    const auto type = event->type();
    const bool handled = QDockWidget::event(event);
    // WinIdChange also accompanies native destruction; never recreate a handle
    // while Qt is tearing it down.
    if (type == QEvent::WinIdChange && internalWinId() == 0) return handled;
    // Qt can recreate/reassign a native owner on show, restoreState or a flag
    // change. Apply after the base event, without reparenting or toggling flags
    // during a dock drag (which would disturb Qt's docking state).
    if (type == QEvent::Show || type == QEvent::WinIdChange || type == QEvent::ParentChange)
        detachWindowOwner();
    return handled;
}

void IndependentDockWidget::detachWindowOwner()
{
#ifdef Q_OS_WIN
    // This fixes Windows owner semantics only. In particular, never force
    // native child IDs in the offscreen plugin: its backing-store lifetime
    // does not support the same native reparenting path.
    if (QGuiApplication::platformName() != QStringLiteral("windows")) return;
    if (!isFloating() || detaching_) return;
    QScopedValueRollback<bool> guard(detaching_, true);
    const auto id = winId();
    auto* native = windowHandle();
    if (native) {
        if (observedWindow_ != native) {
            observedWindow_ = native;
            connect(native, &QWindow::transientParentChanged, this, [this] { detachWindowOwner(); });
        }
        native->setTransientParent(nullptr);
    }
    // Offscreen/minimal Qt IDs are not HWNDs. Only touch this process's actual
    // floating window on the Windows platform plugin; never a docked WS_CHILD.
    const auto handle = reinterpret_cast<HWND>(id);
    if (!IsWindow(handle) || (GetWindowLongPtrW(handle, GWL_STYLE) & WS_CHILD) != 0) return;
    if (GetWindow(handle, GW_OWNER) != nullptr) {
        SetLastError(ERROR_SUCCESS);
        const auto previous = SetWindowLongPtrW(handle, GWLP_HWNDPARENT, 0);
        const auto error = GetLastError();
        if (previous == 0 && error != ERROR_SUCCESS)
            qWarning() << "Cannot detach floating card owner:" << error;
    }
#endif
}

} // namespace orion::app
