# Stage 42 — incident statistics and Deep Telemetry lifecycle

22 September 2026. Windows checkpoint, not full migration completion.

## Source-led statistics correction

Compared core/diagnostic_session.py::_stats and incident window boundaries.
Native statistics kept min/max iterators, then sorted the underlying vector for
p95, so the iterators could point at different values. Min/max now come from the
sorted endpoints; last remains the last chronological known value. A regression
was observed failing before the fix on [95,20,55,10,80], then passing afterward.
Both incident window statistics and nested application-context statistics share
the correction. A network-spike diagnostic test checks the downstream finding.

Added the reference above_90_fraction field (known samples >=90 / known sample
count); absent values are null. This is a sample fraction, not time weighted.
Baseline ends with a strict comparison at focus start instead of subtracting
an arbitrary microsecond. Sub-microsecond boundary points are not lost or shared.
No incident correlation thresholds, net-counter semantics or window-quality
heuristics are redefined in this stage.

## Deep Telemetry

Read the complete Python ui/widgets/hacker_window.py before changes. Retained
two independent-unit charts, session peaks from the main tracker, a local
telemetry pause, global monitoring pause and view-only log clearing.

- Finite nonnegative frequencies and per-core loads in [0,100] are displayed and
  exported. Missing/negative/nonfinite/out-of-range inputs become null rather
  than fake zero, full load or a fabricated frequency. Genuine zero is retained.
  Invalid peaks/uptime are also unknown. Rows retain core indices; mismatched
  vector lengths use unknown slots so per-core charts remain aligned.
- Aggregate-only frequency is one explicitly labelled 'общая' bar, not CPU1 or
  replicated cores. No usable per-core or aggregate value means 'unavailable'.
  Partial charts label unknown slots; zero and missing are distinguishable.
- A telemetry presentation timestamp and pause state are retained in export.
  The timestamp is UI consumption time, not new per-metric producer provenance.
- Log export retains the complete last report including note, source, quality,
  limit, groups and collection completion timestamp alongside legacy fields.
  Quality/source/time/record count/limit are visible. Partial-read notes stay
  visible even when some records were returned; missing coverage is not 'no errors'.
  Backend strings remain supported; structured entries are preserved in JSON
  and displayed as their line or compact JSON fallback, never blanked.
- Clear invalidates in-flight and already queued responses by request generation
  and clears a pending result. It never modifies Windows Event Log/journalctl.
  Refresh cannot start another collector until the prior completion is handled.
- A completed log snapshot received during global pause is held pending; Resume
  applies it unless Clear invalidated it. Local pause affects telemetry only,
  matching the original manual-log-refresh behavior. Export identifies pending/
  loading/paused state and retains the previous accepted snapshot until applied.
- The log worker owns only its collector, not the dialog. Closing disconnects
  callbacks and requests cooperative interruption without blocking the GUI.
  An already running OS query may finish in the background; no thread is killed.
  finished schedules worker deletion. Clear suppresses results, not OS records.
- Two header rows and wrapping labels keep minimum-width controls readable.
  Explicit light label text fixes black-on-dark standalone rendering. The existing
  terminal-green visual style is retained; all-theme parity is not claimed.

## Verification scope

36 automated targets, with expanded deterministic summary and dialog tests.
Injected bounded collectors exercise Clear before completion, Clear after a
result is queued, global-pause pending results, Clear while pending, refresh,
nonblocking close and worker cleanup without reading or clearing real OS logs.
Dialog tests also check unknown/zero/aggregate export and local/global pause
independence. Native fixture captures at 780 logical pixels wide are inspected
at 100% and 150% scale; fixtures are explicitly labelled synthetic.
The existing app-monitor lifecycle suite still uses only its own bounded EXEs.
No user-selected EXE, real CPU/GPU stress, LAN scan or speed test is launched.
Final build, suite, portable extraction/hash and Windows-only PATH results are
recorded separately in outputs/STAGE42-VERIFICATION.md outside the source ZIP.

## Remaining boundaries

The incident worker still waits its post interval after log collection instead
of using an absolute marker deadline. Incident input filtering/deduplication,
edge-of-window coverage, queued telemetry provenance and cross-sample causal
correlations need a separate source-led stage; this statistics fix is not a full
incident audit. Window quality and missing network deltas retain prior semantics.
Deep Telemetry's native file-save dialog/overwrite workflow and boot-log size
handling were not redesigned. Broader theme/high-DPI review, remaining auxiliary
window lifecycles, long sessions and Linux identity/safe runtime remain open.
