# Stage 45 — Deep Telemetry save workflow and bounded application logs

24 September 2026. Windows C++20/Qt6 checkpoint, not full migration completion.

Read Python hacker_window.py::_show_boot_log/_export_logs and core/boot_log.py,
native DeepTelemetryDialog, storage logging and system-error collector limits.
Python uses an in-memory initialization buffer and direct JSON writes; native
already reads the persistent application log. This stage keeps that distinction
explicit rather than calling that file a complete current-boot Windows log.

## Saving a snapshot

The payload is captured before the file chooser opens. Nested events (telemetry,
Clear, refresh, pause) cannot change the selected snapshot while choosing a file.
Its original quality, unknowns, source and capture states remain in JSON. Empty
selection cancels; no suffix adds .json, explicit suffixes are preserved.

Platform overwrite prompts are disabled for this workflow. One explicit prompt
checks the final resolved path, including a suffix-added collision. No is the
default. Refusal and cancellation do not open an output file. QSaveFile uses
binary UTF-8, direct-write fallback disabled, full byte-count verification and
commit verification. Failed/partial writes are not announced as saved. The UI
shows the path and filesystem error. This is normal single-user overwrite
confirmation, not protection against another process racing to change the path.

## Application-log preview

storage/telemetry_files.* reads at most the final 256 KiB plus one boundary byte
from the file size observed at open, retains the latest 2000 lines, and shortens
individual displayed lines after 4096 UTF-16 code units without splitting a
surrogate pair. It avoids introducing invalid UTF-8 merely by splitting a valid
prefix. Invalid/incomplete source UTF-8 is replaced and disclosed. Boundary,
empty, missing and directory paths have explicit outcomes. A changing file size
or short read is reported; this is not an atomic snapshot against external writes.

The preview shows the source path, observed file size, bytes read, omitted
prefix, shortened lines and incomplete first-line/decoding warnings. Text is
plain read-only text, not HTML. An empty log is not an error-free PC verdict.
The persistent log is never truncated, deleted or rotated. A bounded tail may
exclude initialization lines; the dialog says it shows the latest records.

BootLogWorker reads off the GUI thread, owns only its path/result and deletes
itself after completion. Closing disconnects the dialog and requests interruption
without waiting or destroying a running thread. An already blocked filesystem
call cannot be forcibly cancelled. Reopening collects a new bounded preview;
there is no live tail or guarantee of hard real-time completion on slow storage.
The Stage42 system-error Clear/pause/generation/close contracts are unchanged.
OS-event record limits are unchanged; large OS event-message byte bounds remain
a separate follow-up from the application-log preview implemented here.

## Verification

39 CTest targets, including telemetry_files_tests. Fixtures cover UTF-8 Russian
paths, JSON/null/zero roundtrip, complete replacement, empty/missing-directory/
directory failures and no leftover atomic temp files. Log tests cover empty,
missing, CRLF, valid/invalid/incomplete UTF-8, exact byte/line boundaries, 2001
lines, a 4 MiB log and one huge multibyte line. Source bytes are unchanged.

Deep Telemetry tests drive the real Qt save slots using a non-native Qt picker:
cancel, suffix addition, exact/suffix collision refusal, No default, confirmed
replacement and snapshot stability while Clear/new telemetry occurs in the
picker. Selection is checked to stay at the exact temporary fixture path before
acceptance. The first test-driver version retained Qt's old suggested filename
inside its temporary folder; corrected to edit and verify the visible filename.
No user report was overwritten. Native Windows Deep/boot views are captured at
100%/150% with visibly labelled synthetic data; immediate viewer close is checked.

Final run/archive results are in outputs/STAGE45-VERIFICATION.md outside this
source archive. These checks do not simulate an actual full disk, physical I/O
failure, concurrent adversarial path replacement or a hung network filesystem.
The platform-native file chooser itself is not automated. No real CPU/GPU stress,
speed test, LAN scan or user-selected executable is run.

Next useful slice: source-led incident CPU saturation thresholds/fractions and
OS-log category/confidence semantics (new events versus recent events). Keep
Stage44 freshness and same-observation memory/thermal/app constraints. Broader
themes, Gamer Mode, long-session performance and Linux safe runtime remain open.
