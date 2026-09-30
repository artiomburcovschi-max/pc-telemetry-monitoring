# Stage 39 — Problem Application charts, verdict and detailed report

20 September 2026. Native C++20 / Qt 6.9.3 Windows checkpoint, not completion
of the whole Python migration.

## Source-led scope

Compared the graph/verdict/report presentation in the reference
`ui/widgets/app_monitor_panel.py` and summary/text sections of
`core/app_monitor.py`. Stage 38's process-ownership and closure safeguards
remain; this stage does not broaden termination authority.

- Two native charts retain up to 6000 points each. CPU is the process-tree sum
  and can exceed 100%; WS/private share a memory axis. Original blue/orange/purple
  series colors are retained across themes. Time is active observation time.
- Missing/negative/non-finite metrics break only their own series. Invalid,
  duplicate/out-of-order times are ignored. Pause deliveries are ignored;
  resume and large sampling gaps break paths. Hidden tabs retain history.
- Nearest-time hover, vertical/horizontal crosshair, auto-scaling, reset on new
  monitoring run and a collapsible last-50 table. A deque bounds storage;
  rendering walks at most 6000 vertices per series, without per-point rescans.
  There is no pixel-bucket downsampling; spikes remain actual vertices.
- Verdict card: status stripe, reasons, duration/count, WS/private and
  faults/handles, up to four actions, incomplete coverage. Labels are plain text.
- Detailed read-only report dialog with copy/TXT/JSON and a stable report
  snapshot. Preview, clipboard and TXT share a canonical formatter. JSON omits
  redundant report_text. Atomic saves preserve errors; appended extensions
  require default-No confirmation if the final filename already exists.
- TXT includes outcome/closure errors, numeric/hex exit code, trends and R²,
  quarters/tails/recovery, handles/faults/commit/paging, I/O and system GPU,
  seven peak moments, recorded hangs, last available top processes, events,
  findings with detail/evidence, actions and coverage. Unknowns remain n/д.
- Largest processes include private memory and handles and are sorted by
  private memory (WS fallback). A final empty process sample no longer erases
  the last available list. Paging fractions use only known active/idle samples.
- No-findings is not described as proof of health. Event comparison availability
  is explicit; GPU/disk scope is explicitly system-wide. Event ID zero survives.

## Verification

Release incremental build in build-stage37-mingw. Full suite: 34 targets.
New chart contracts check bounds, spikes, invalid/duplicate times, >100% CPU,
missing-vs-zero memory, independent series gaps, pause, hover, hiding and clear;
20 paints of a 6000-point chart have a conservative 5-second guard.
Report contracts check details/evidence, event fallback and zero IDs, hex exit,
closure fields, peak timestamps, memory trends and unknown paging.
UI signal fixtures check wiring, 50-row cap, 80-point history, pause, verdict
and equality of preview/full-dialog text without launching a selected EXE.
Native Windows screenshots of charts/verdict/dialog are explicitly fixture
data. Existing lifecycle tests launch/close only their own bounded processes.

## Remaining audit, deliberately not claimed complete

Cached system-before/after freshness and I/O attribution across tree changes
still need source-led review. Polling can miss very short-lived descendants;
safe closure targets only verified tracked processes. Linux safe termination
and runtime verification remain. The source summary's positive-growth fraction,
constant-series R² convention, and symbolic exit-code descriptions are not
fully mirrored here (raw numeric/hex codes are retained). Long-session timing,
all themes/high DPI and the other auxiliary dialogs/lifecycle paths still need
broader validation. No real CPU/GPU stress, speed test or LAN scan was started.
