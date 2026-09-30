# Stage 12 checkpoint — problematic-application monitoring and incident capture

Stage 12 replaces the final main-window placeholder with working native
process-tree monitoring and turns the toolbar problem marker into a real
runtime incident capture. It is an intermediate migration checkpoint, not a
declaration of complete Python/PySide6 parity.

## Problematic-application monitor

The user can select a Windows executable, choose one of the original 1–480
minute durations and start it through O.R.I.O.N. The worker follows the root
PID and every discovered descendant, including children that outlive their
parent. Each sample includes aggregate and per-process evidence for CPU,
working set, private memory, handles, page faults, process/thread counts and
read/write rates and totals. Windows top-level windows are checked through
`IsHungAppWindow`; up to eight current process leaders are retained.

The samples are correlated with the main native telemetry stream: system CPU
and RAM, available memory, disk and network throughput, GPU load, temperature
and VRAM. Unsupported commit, paging and disk-busy counters remain explicit
NULL/UNKNOWN rather than being invented.

Sampling follows the Python contract: 1 second through 30 minutes, 2 seconds
through 120 minutes and 5 seconds for longer sessions. Global pause suspends
observation while leaving the application running and rebaselines cumulative
counters on resume. Manual stop also leaves the launched application running.
At a normal timeout the optional default action sends `WM_CLOSE`, waits seven
seconds, then terminates only surviving PIDs in the observed tree.

Read-only Windows Event Log snapshots are taken before and after the session.
The report is schema v3 and contains the complete samples, summary/trends,
peak moments, termination outcome, log difference, findings, verdict,
coverage, action plan and a human-readable form. The new tab exposes live
metrics, a bounded table and atomic JSON/TXT export plus clipboard copy.

## Runtime incident capture

The main telemetry path now retains a bounded five-minute/1200-sample ring.
`⚡ Проблема сейчас` marks the monotonic and wall-clock time, preserves up to
60 seconds before it, collects 15 seconds after it and compares read-only
system-log snapshots. The final summary uses non-overlapping baseline
(-60…-10 s), focus (-10…+5 s) and recovery (+5…+15 s) windows and publishes
valid/estimated/stale quality, gaps, missing metrics and app context no older
than three seconds.

The diagnostic engine handles incomplete-window coverage, WHEA, display/TDR
and AppHang correlation, confirmed/possible memory pressure, CPU thermal
throttling or saturation, GPU saturation, network degradation and active-app
CPU/I/O contribution. App-monitor rules also cover crash correlation,
sustained private-memory and handle growth, hung windows, resource exhaustion,
page-fault pressure, sustained CPU, disk I/O pressure and incomplete automatic
termination.

## Verification boundary

Stage 12 has 15 CTest targets. New deterministic tests cover duration-aware
sampling, app trends and incident window boundaries. A native lifecycle test
launches a dedicated fixture and proves that manual stop leaves it alive while
timeout auto-close terminates only the fixture tree. All 17 shared golden cases
continue to pass. Automated tests never start CPU or GPU stress.

The packaged GUI was checked at 1102×932. The new tab, controls, live table,
report panel and vertical scrolling were visible without clipping. A live
incident marker completed with 62 retained samples and `valid` quality.

## Remaining migration boundary

Deep Telemetry, more detailed Windows commit/paging/context-switch and disk
busy counters, timestamp-level Event Log correlation, richer report rendering
and the remaining Python screen and behavior details still need feature-by-
feature migration.
