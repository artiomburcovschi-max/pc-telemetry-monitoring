# Stage 8 checkpoint — native read-only Autostart

Stage 8 replaces the Autostart placeholder with the same read-only inventory
contract used by the Python/PySide6 reference. The implementation contains no
registry, Startup-folder, `.desktop` or systemd mutation path.

## Shared contract

Every entry contains a display name, launch command/path, human-readable
source, `user` or `system` category and a tri-state enabled value. Unknown is
kept distinct from false: a file present in a Windows Startup folder is shown
with a neutral status because the folder format has no separate reliable
enabled flag.

## Windows sources

- `HKCU` and `HKLM` `Run` keys;
- `HKCU` and `HKLM` `RunOnce` keys;
- the current user's Startup folder;
- the common Startup folder for all users.

Registry and filesystem enumeration use native Unicode Windows APIs and the
standard C++ filesystem library. Values are only read.

## Linux sources

- `~/.config/autostart/*.desktop` as user entries;
- `/etc/xdg/autostart/*.desktop` as system desktop components;
- `systemctl --user list-unit-files --type=service` as user-service entries.

`Hidden=true` and `X-GNOME-Autostart-enabled=false` are recognized as disabled
for XDG files. systemd `enabled` and `enabled-runtime` states are enabled; the
other returned states are disabled, matching the Python reference.

## UI and lifecycle

- the scan starts lazily on first opening the tab;
- scanning runs outside the GUI thread;
- the global pause prevents a new scan and resume starts the deferred first
  scan when the tab remains selected;
- «Обновить список» performs another read-only scan;
- search covers name, command and source;
- a category selector filters all, user or system entries;
- all columns are sortable and the totals remain visible.

## Verification

All 10 CTest targets and all 17 diagnostic golden cases pass. The Windows live
probe found 17 entries on the verification machine: 13 user and 4 system items.
The next direct migration block is the Diagnostics and Tests screen: wiring the
already ported diagnostic engine into the GUI, report preview and export.
