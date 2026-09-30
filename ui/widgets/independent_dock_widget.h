#pragma once

#include <QDockWidget>
#include <QPointer>
#include <QWindow>

namespace orion::app {

// Keep Qt ownership/docking/style inheritance, but not native window ownership:
// an owned HWND is hidden by Windows whenever its owner is minimized.
class IndependentDockWidget final : public QDockWidget {
public:
    explicit IndependentDockWidget(const QString& title, QWidget* parent = nullptr);
protected:
    bool event(QEvent* event) override;
private:
    void detachWindowOwner();
    QPointer<QWindow> observedWindow_;
    bool detaching_ {false};
};

} // namespace orion::app
