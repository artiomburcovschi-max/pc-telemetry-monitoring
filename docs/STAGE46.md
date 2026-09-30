# Stage 46 — floating cards independent of the minimized main window

25 September 2026. User-reported Windows regression fix; not final migration.

## Cause and source comparison

Read Python main_data_widget.py, diagnostics_hub_widget.py and dock_utils.py.
Both Python dock hosts explicitly detach the floating HWND owner. The native
port created ordinary QDockWidgets without this operation. A native Windows
regression fixture reproduced actual hiding on main-window minimization, not
just a Qt visibility flag. A whole-application crash was not reproduced in that
original scenario; the confirmed defect was hidden owned windows.

Windows hides an owned window when its owner is minimized; simply calling show
or changing a Qt window-type bit does not remove that relationship. References:
[Windows owned windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#owned-windows),
[Qt 6.9 transient parent](https://doc.qt.io/archives/qt-6.9/qwindow.html#transientParent-prop).

## Implementation

IndependentDockWidget is now used for all five Overview cards and all three
diagnostic docks. It retains QWidget/QObject parenting, docking layout and theme
inheritance, but clears QWindow transient ownership and any remaining Windows
GWLP_HWNDPARENT on floating HWNDs. It handles topLevelChanged, Show, WinIdChange,
ParentChange and reassignment of the QWindow transient owner, with a reentrancy
guard and pointer-width-safe WinAPI. It neither reparents the QWidget nor flips
window flags during a drag. A destroyed native ID is not recreated on teardown.
An API failure is logged, not treated as success. Docked WS_CHILD windows are not
modified. This path applies only to the real Windows Qt platform plugin.

The first draft forced native IDs on the offscreen plugin as well, exposing a
backing-store crash at fixture destruction. Debugger confirmed the offscreen
path; gating before winId() fixed it. Offscreen lifecycle now passes, and native
ownership tests intentionally run on Windows, not as a misleading offscreen test.
Linux ownership behavior is unchanged and not claimed verified.

Main minimize, hide-to-tray operation and tab changes leave floating cards
visible. A visible floating overview card also keeps overview telemetry updating
during Gamer Mode. Global Pause still freezes readings. Explicitly hidden cards
are not resurrected. Ordinary redocking, layout reset and actual application
Exit remain in control: independence from main visibility is not independence
from the ORION process. Full exit still reclaims floating windows; QObject
destruction deletes their content. No extra process or timer is created.

## Verification

The plain-Qt baseline native test failed on retained GW_OWNER and actual
IsWindowVisible; the fixed test passes. ORION_DOCK_BASELINE opts into that
deliberately failing control for investigation only. New independent_dock_tests
covers two floats, repeated minimize/restore, main hide, tab switch, explicit
hide, geometry stability, repeated redock/float, saveState/restoreState, native
flag recreation and parent destruction (QPointer and HWND no-orphan checks).
Windows CTest uses QT_QPA_PLATFORM=windows for this target.

Production UI checks cover CPU/RAM/report, confirm all eight docks use the new
class, verify real native visibility while main is minimized, and publish
labelled fixture telemetry through the real worker signal. Pause/resume,
hide/tray restore, Gamer Mode, reset and normal non-tray exit are checked. Native
UI at 100%/150% passes; floating-card screenshots are labelled synthetic and
visually inspected. The screenshots show widget contents; native ownership and
main minimization are asserted separately, not inferred from a widget grab.

40 test targets. Final totals and archive verification are recorded separately
in outputs/STAGE46-VERIFICATION.md. No real stress/speed/LAN or user EXE runs.

## Remaining boundaries

No manual multi-monitor drag/snap/DPI-transition matrix, actual tray-icon click
automation or long-duration soak is claimed. Float/redock is driven through Qt's
real dock API. Broader original-feature completion remains tracked in
FINAL-CHECKLIST.md. The diagnostic dock-state settings field exists but is not
wired into the native host's save/restore; this is a separately recorded gap,
not silently fixed in the floating-visibility patch.
