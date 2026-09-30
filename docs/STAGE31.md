# Stage 31 — original Network fields and panel behavior

Compared ui/widgets/net_widget.py and core/telemetry.py in the original Python
reference. The native backend already collected cumulative bytes/errors/discards,
but the worker and page omitted these fields. This stage connects that data,
restores the original current-state card and related panel behavior. It does
not establish whole-app parity or live side-by-side Python pixel equivalence.

## Data and meaning

The card order follows Python: download, upload, total received, total sent,
ping, errors, dropped packets, local IP, MAC, public IP/provider, Internet speed
test. Totals are OS/provider lifetime counters, not ORION session totals. GiB
uses the original Russian ГБ label with 1024³ bytes explained in the tooltip.
Errors/discards show interval count followed by cumulative count, not events/s,
ping loss percentage or a latency measurement. Counter integers retain uint64
precision; unavailable/stale current measurements show н/д rather than zero.

The native system scope is explicitly labelled: Windows sums active non-loopback
GetIfTable2 rows; Linux sums non-loopback /proc/net/dev rows. Selecting an adapter
affects local IP/MAC and scanner scope, not the system traffic aggregation. This
is not a claim of exactly identical psutil aggregation across every platform.

NetworkCounterTracker compares consecutive published samples independently for
errors and discards. First sample, pause/resume, invalid/stale sample, changed
source/interface set, duplicate/backward timestamp or decreasing counter cannot
produce an invented delta or unsigned underflow. The next usable sample establishes
a new baseline. Estimated input remains estimated. Windows LUID/Linux interface
sets also guard the short rate probe against topology changes between reads.
This cannot detect an unseen reset-and-recovery fully between two samples.

The telemetry worker transports counters with their source/quality and resets
interval baselines on pause transitions, including a transition during sampling.
Current-state fields update across hidden tabs/Gamer Mode; global Pause freezes
them. Existing 60-second graph history/pause behavior remains. Real interval
errors/discards now also feed the incident runtime ring; the former constant
zero placeholders are replaced by numeric values or JSON null for missing data.

## Presentation and interaction

Reuses Stage 30 DetailCard without a status stripe. Restores NET and Устройства
в сети compact headings, adapter-row refresh, local panel-reset button and
original splitter size requests (360/470 default, 210/620 scanning when free
resize is disabled). Reset changes only this splitter, not the main window or
Overview docks. Minimum widths and scrollbars keep narrow content reachable.
Subnet context remains on the scanner side. The five-column read-only device
table, cancellation, bounded probes, cached public-IP result and manual-only
speed test remain. Opening the page never starts scanning or a speed test.

Native adaptive KiB/s/MiB/s rate formatting and the existing chart are retained
instead of forcing tiny readings to 0.00 MB/s. All five themes remain, including
Quantum graph coloring and Slate graph hiding. The screenshot scroll option now
also supports the Network left column for reproducible narrow-window review.

## Verification boundary

New deterministic counter tests cover real zeroes, uint64 precision, independent
deltas, missing/stale samples, first samples, resets, source/interface changes
and timestamps. Expanded worker tests check actual transport and pause/resume
baselines. UI integration injects deterministic telemetry into the real signal
connection and checks labels, missing values, pause and local splitter reset.
Final build, CTest, Windows screenshot and extracted-ZIP results are recorded
in STAGE31-VERIFICATION.md beside the delivery ZIPs. Tests use loopback/injected
network fixtures; no live LAN scan, Internet speed test or CPU/GPU stress runs.

Linux collection changes still require Linux runtime validation. Remaining work
includes quick/deep/full diagnostic scan presentation, other screens' original
field/callback audit, full alert lifecycle and high-DPI/manual window checks.
