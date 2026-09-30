# Stage 48 — bounded OS event-log collection

26 September 2026. Windows checkpoint; migration remains unfinished.

## Scope and reference

Read Python core/system_errors.py collector/parser/comparison paths. Preserve
the current-boot filter, one output entry per real event, first-message-line
presentation, and exact multiset comparison. Do not modify the OS logs. The native
collector previously bounded only accepted record count: an arbitrary XML/message
size could be allocated, repeated rejected events could keep enumeration going,
and journalctl output was accumulated until process exit.

The Windows API explicitly uses bytes for
[EvtRender buffers](https://learn.microsoft.com/en-us/windows/win32/api/winevt/nf-winevt-evtrender)
and WCHAR counts for
[EvtFormatMessage buffers](https://learn.microsoft.com/en-us/windows/win32/api/winevt/nf-winevt-evtformatmessage).
The implementation widens message counts before multiplication and tests the
result before allocating. Bounded searches replace unbounded null-string reads.

## Bounds and incomplete-data contract

- Windows XML buffer: at most 64 KiB per event, formatted message: 32 KiB.
- Each System/Application query: at most 2 MiB of admitted caller buffers across
  XML and messages, and at most the requested number of examined events (1–500),
  including rejected events. The combined displayed list also respects the limit.
- Retained lines: at most 2048 UTF-16 units, with a visible ellipsis and no cut
  surrogate pair. Provider/channel are bounded to 256 units, timestamp to 64,
  Event ID text to 32. Existing first-line/first-three-Data fallback is retained.
- Oversized raw XML is rejected before parsing. DTD/entity expansion is not allowed.
- journalctl: drained stdout at most 2 MiB, stderr at most 16 KiB, 6-second deadline
  including startup, stop helper on excess output, no shell or detached helper.
  Read newest first (`-r`) so a bounded prefix prioritizes recent events. JSON lines
  above 64 KiB are skipped, later complete records retained; a cut final fragment
  is not accepted as an event. Successful final JSON without newline is supported.

Bounds apply to retained/application-owned data, not an absolute whole-process
RSS cap. OS/Qt internal allocations and native API blocking are not bounded by
these numbers. QProcess termination uses the normal Qt kill/wait cleanup path;
it is not a guarantee against a broken OS or unkillable process. SMART's separate
subprocess path is unchanged; this stage is specifically the OS event-log reader.

Reports include collection_limits, read_stats and collection_limited. Skipped,
oversized, shortened or unformatted fragments and budget/count cutoffs degrade
quality to estimated; API failures preserve permission/error quality. Hitting
the requested count is conservatively limited even when no older event is proven
to exist. Empty malformed/partial results cannot claim an error-free system.
Missing native message metadata uses the bounded XML fallback and discloses that
the full message was unavailable. Native raw input is not exported as unlimited XML.

The diagnostic log card and TXT now retain notes even with nonempty entries;
Deep Telemetry shows notes first, wraps long lines within the viewer, and exports
the entire limits/stats report without adding visual wrap breaks to stored entries.
Diff/merge/time-window correlation retain the limited flag and warning. Comparisons
remain comparisons of the available strings only: shortening can hide differences,
so partial data is never upgraded to valid. Original legacy reports without new
metadata retain their existing compatibility path. Incident raw snapshots retain
their full metadata; this stage does not finish every diagnostic inference rule.

## Verification and boundaries

New system_log_limits_tests exercises pre-allocation size/overflow/budget guards,
Unicode boundaries, large/invalid/DTD XML, record and total JSON limits, malformed
JSON, multiline messages as one event, missing fields, cutoff tails, comparison
provenance, TXT notes and diagnostic coverage. The test executable launches only
itself as a finite stdout/stderr/timeout fixture; caps, exact boundaries, missing
program and complete-prefix retention are checked. No writes to Windows Event Log.
The actual Windows oversized-allocation API case was not injected: preallocation
guards are tested independently and the bounded parser receives synthetic data.

Live Windows contract checks limits, counters and retained line lengths. Main UI
checks a labelled partial-log fixture through the real worker signal. Deep Telemetry
checks top-of-view warnings and JSON metadata through pause/resume. Native 100%/150%
screenshots are synthetic and labelled accordingly. Stage 46 independent-card and
Stage 47 layout regressions remain part of the same main UI test.

42 automated targets; final results and archive hashes are recorded separately in
outputs/STAGE48-VERIFICATION.md. Actual journalctl/systemd operation is not verified
here: shared parser/process helpers run on Windows fixtures, Linux remains open.
No real CPU/GPU stress, speed test, LAN scan or user-selected executable was run.
See FINAL-CHECKLIST.md for remaining diagnostic rules, window/theme/tray matrix,
physical multi-monitor testing, long sessions, real hardware and clean Windows.
