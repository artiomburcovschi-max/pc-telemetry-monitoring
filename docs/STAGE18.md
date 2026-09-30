# Stage 18 — native Servers and Terminal tabs

Stage 18 continues the Python/PySide6 to C++20/Qt 6 Widgets migration. It is a
tested checkpoint, not a claim of complete feature or visual parity.

## Servers

The optional Servers tab is now a real native screen. Server and database
profiles use the Python-compatible `server_profiles.db` SQLite schema in the
application data directory. Create, list, filter, update and delete operations
are implemented in `ServerProfileStore`; labels are sorted without exposing
the saved secret in the table.

The compatibility boundary remains explicit in the UI: as in the Python
reference, `secret` is stored locally without encryption. The screen warns not
to use production credentials. A later system credential-manager migration can
replace that field without changing the profile/status contract.

Each status action performs a real background TCP connection to the saved host
and port. It has a two-second bound, avoids duplicate checks for the same
profile and updates the existing row with measured latency or an honest offline
reason. A profile never appears online before a successful connection. Global
pause cancels new/status work, and tab removal or application shutdown waits
for the bounded workers.

## Terminal

The optional Terminal tab launches a genuine user shell through `QProcess`:
PowerShell with a `cmd.exe` fallback on Windows, and interactive bash elsewhere.
Output is streamed into a protected terminal editor; submitted lines go to the
child process stdin. Command history, Ctrl+L, shell clear-screen controls and
ANSI/OSC cleanup follow the Python behavior.

Turning the tab off removes it immediately and closes the shell through an
`exit` request, terminate grace period and final bounded kill fallback. Theme
changes preserve the shell session while updating terminal presentation.

## Safe dynamic navigation

Servers is inserted after Networks and Terminal at the end. Task Manager,
Autostart and Diagnostics activation no longer depend on numeric tab indexes;
they compare the actual page widgets. Therefore optional tab insertion/removal
cannot silently start the wrong worker.

## Verification boundary

The clean Release build completes 140/140 steps and all 19 CTest targets pass.
New tests cover SQLite profile round trips, a reachable loopback TCP status
check, pre-start cancellation, ANSI/clear-screen filtering, real shell startup
and bounded termination. The UI contract enables both tabs, checks their order
and objects, then removes them live and verifies persisted settings.

Automated screenshots of both screens were inspected at 1100×820. The terminal
showed a real PowerShell prompt and both optional pages fit without clipping.
No CPU/GPU stress was executed. Remaining migration work includes the Gamer
Mode overlay and the behavioral Quantum Cyan gauge / Slate Minimal dense-layout
flags before final visual polish.
