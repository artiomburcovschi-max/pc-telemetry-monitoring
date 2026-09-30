# Stage 43 — incident deadlines and honest window coverage

23 September 2026. Windows checkpoint; full migration remains incomplete.

## Source-led scope

Read Python core/incident_capture.py and diagnostic_session.py window summarizer.
Restore the absolute post-marker deadline and 120-second log context. Strengthen
input coverage beyond the Python count/internal-gap heuristic: malformed time,
duplicates and missing edges must not masquerade as a complete temporal window.
Telemetry remains 60 seconds before / 15 after; focus is [-10,+5] clipped to the
requested window. Baseline ends strictly before focus, recovery starts after it.

## Capture lifecycle

The UI takes a steady-clock marker alongside its session-relative monotonic
marker and wall timestamp, before rendering. Clock domains remain separate.
IncidentOptions accepts an optional steady marker; older callers use capture-entry
time. A slow baseline consumes the requested post interval; no fresh full wait is
added afterward. Waiting rechecks the absolute deadline with bounded wakeups.
The report records timing_contract, marker_clock_source, actual capture elapsed,
post wait and after-query start delay. This is not a hard real-time deadline:
scheduling and OS log collection can finish later than the requested window.

Cancel wakes the wait, skips subsequent reads and discards an after-snapshot
completed after cancellation. It does not forcibly interrupt an OS call already
in progress. Main-window teardown waits at most 500 ms for this worker, then uses
the existing retain-until-finished path; no running QThread is destroyed.
CaptureReady is still emitted for cancellation. Busy starts are ignored, a new
run resets cancellation, stale/unidentified/duplicate completions cannot replace
the current incident. Finished restores controls only when not paused/running.
An older generic diagnostic report cannot replace the latest incident card.

Global monitoring pause does not suspend the incident wall-time deadline. The
missing telemetry is reflected as incomplete coverage; it is not backfilled.
Invalid numeric windows, negative values, windows over 300 seconds per side,
unrepresentable numeric boundaries, invalid wall timestamps and future steady
markers are rejected before system collection.

## Temporal normalization and counter semantics

- Accept only objects with numeric finite nonnegative monotonic timestamps
  within the inclusive requested interval, then stable-sort chronologically.
- Collapse identical complete rows at the same timestamp. If rows at that time
  disagree, reject the entire group rather than choose arbitrary evidence.
  A different observed_at is also a conflict. JSON retains filter counters.
- Preserve before/after count thresholds and max internal gap <=3 seconds;
  additionally require leading/trailing gaps <=3 seconds and no malformed or
  conflicting timestamp groups for valid temporal coverage. Dense central data
  cannot hide missing ends. Empty edge gaps are null, not fabricated observations.
- data_quality_scope=temporal_window_only and window_contract identify the new
  meaning. Valid temporal coverage does NOT mean fresh or complete metric data.
- Network error/drop sums accept finite nonnegative values, preserving real zero.
  Missing/invalid counters are null when none are known, never fake zero. Known
  counts and valid/partial/unknown quality are independent per channel. Sums are
  labelled known_intervals_lower_bound; partial totals display >= in the card.
  Finite sums avoid rounding overflow; overflowed sums become unknown.

Only timestamped events within [marker, marker+post] enter new_system_errors;
recent_system_errors covers [-120,+post]. Raw before/after and unfiltered new
records remain for inspection. Unparseable timestamps and out-of-window events
do not become time-correlated evidence. This does not prove causality or full log
coverage: the collector can return a limited/partial snapshot, and wall-clock
adjustments or delivery delays can affect timestamps. Such limitations remain.

## Presentation and tests

Collection completion no longer automatically turns the card green. The card
and text report show missing edges, duplicate/conflict/invalid-time counts,
known network samples and explicit unknowns. Log and telemetry window lengths
are distinct; completed collection and temporal coverage are not health verdicts.

The new orion_incident_tests target uses injected, bounded collectors, not actual
OS-log reads. Covers delayed baseline, explicit/default marker clock, deadline,
cancel during baseline/wait/after-read, busy/restart, invalid requests, timestamp
correlation boundaries, sorting/deduplication/conflict rejection, edge gaps,
empty/zero-duration windows, real-zero/partial/absent counters and TXT contracts.
UI tests use labelled synthetic incident data through the real report signal,
check stale/unidentified completion rejection and optional native card captures
via ORION_INCIDENT_UI_CAPTURE. The full suite has 37 targets. Final native,
archive hash and extracted-runtime results are recorded outside the source ZIP
in outputs/STAGE43-VERIFICATION.md.

## Deliberately remaining

MainWindow runtime rows still use a flat UI cache without original per-metric
producer provenance; app context can be stale and cached deltas can repeat across
otherwise distinct rows. Deduplicating identical timestamps does not solve this.
Incident memory/thermal/app-contributor rules still combine aggregate peaks from
different observations. A same-observation/freshness contract and compatible
rules are the next incident slice, not claimed solved here. Also outstanding:
Deep Telemetry file-save/boot-log bounds, broader auxiliary-window/theme/DPI and
long-session checks, and Linux process identity/safe runtime. No actual stress,
speed test, LAN scan or user-selected EXE is run for this checkpoint.
