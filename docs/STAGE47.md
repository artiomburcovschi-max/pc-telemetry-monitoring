# Stage 47 — persistent diagnostic panel layout

26 September 2026. Windows checkpoint, not the final migration.

## Gap and implementation

The Python diagnostics_hub_widget.py saves/restores three named dock panels and
redocks floating panels before full Exit/restart. The native storage already
contained diag_hub_dock_state, but MainWindow never used it. It now restores that
field during diagnostic-page construction and writes the current saveState(1)
alongside Overview whenever settings are saved. No panels or reports are recreated.

diagnostic_dock_layout.* centralizes defaults, validation and position recovery.
State is canonical strict Base64, at most 64 KiB of encoded text, then validated
by Qt restoreState. Version 1 is native; version 0 accepts the Python reference's
default Qt state version and matching dock names. Empty/malformed/truncated or
unsupported states produce the default layout. Failed Qt restore is followed by
an explicit reset, not a partially accepted layout. State is normalized in memory
for the next settings write. Invalid state is logged without logging its payload.
This is not a custom parser for untrusted Qt binary files or a security sandbox.

The three panels are intentionally not closable: stale hidden flags are cleared.
After restore, floating frame titles must intersect one available screen by at
least 160×24 logical pixels (or the smaller frame dimension), within a 32-pixel
top strip. Negative monitor origins are supported; taskbars use availableGeometry.
Unreachable panels return to their docked positions instead of remaining lost.
Qt/Windows may already relocate a restored off-screen window; reachable windows
are retained. This check runs at startup, not a new monitor-hotplug watcher.

Settings → Карточки и панели adds Сбросить панели диагностики. It reuses the same
three widgets, leaves report contents and Overview layout untouched, and saves
the reset. The Overview drag checkbox explicitly says it applies to Overview.

Ordinary settings saves do not redock live floating panels. Full non-tray Exit
retains the existing/Python policy: return floating panels, then save. Thus docked
placement survives normal restart; independently floating windows are not promised
to reopen floating after full Exit. A live floating snapshot (e.g. from a settings
save before an abnormal stop) can restore if reachable. Main minimize, Pause and
Gamer Mode remain covered by the Stage 46 regression tests.

## Checks

New diagnostic_layout_tests covers defaults, native/version-0 restore, changed
areas and split sizes, retained widget identity, bad Base64, oversize/unknown Qt
version/truncation, stale hidden flags, floating snapshots, synthetic unavailable
screens, negative origins, inaccessible titles and repeated reset without duplicate
panels. CTest uses offscreen; the same test is also run with real Windows Qt at
100% and 150% scale. This is not a physical multi-monitor/hotplug test.

Production UI contract additionally saves to a temporary settings file, constructs
a second MainWindow from it, checks restored areas, live save without redocking,
full Exit redocking, and isolated reset without losing the existing report. The
first draft of this new test dereferenced a SettingsApply button before opening
the lazily created settings dialog; the test now opens it first and checks pointers.
No production crash was reproduced by that test-driver error.

41 automated targets. Native main UI at 100%/150% re-runs the independent-card
regressions too. Layout screenshots label the sample as an educational layout,
not a PC diagnostic result. Exact final tests and archive hashes are recorded in
outputs/STAGE47-VERIFICATION.md outside the source tree.

No real CPU/GPU load, speed test, LAN scan or user-selected EXE was run. Settings
and loopback probes are isolated from the user's actual configuration. Physical
multi-monitor dragging/DPI changes, all-theme review, clean Windows and long-session
tests remain open in FINAL-CHECKLIST.md, along with diagnostic-rule/log limits.
