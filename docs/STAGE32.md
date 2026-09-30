# Stage 32 — native local Deep Diagnostics

Compared the original ui/widgets/diagnostics_widget.py, scan_dialog.py and
core/deep_scan.py; inspected the separate full_scan.py scope. This slice adds
the missing active local sequence and its visible dialog. It does not turn the
existing passive report into a falsely labelled deep scan or claim whole-app
functional/visual parity.

## Execution and safety boundary

The new Deep Diagnostics button checks that conflicting diagnostic, hardware,
stress, Internet and application-monitor work is idle. A default-No/Escape-No
warning describes all requested operations before starting any scan worker.
It rechecks worker state after confirmation. The dialog is window-modal;
ordinary telemetry remains independent. No Internet operation is scheduled by
DeepScanWorker; old speed/network/incident/application-monitor results are
excluded from this local report. Public-IP/speed actions reject a concurrent
deep scan. Normal background ping is not redefined as part of this scan.

DeepScanWorker performs fresh hardware inventory, SMART, OS logs, autostart,
memory-before, sequential stress, memory-after and OS-logs-after collection.
The original relative progress weights are retained for hardware/SMART/logs/
autostart/stress, with bounded post-processing progress. HardwareInventoryWorker
now exposes its synchronous background-only collector with external cancellation;
GUI-owned screen snapshots remain plain seed data captured on the GUI thread.

The stress phase reuses the existing native StressWorker: all CPU logical threads
for 30 seconds, then Direct3D 11 GPU compute for 30 seconds, then 200 MiB temporary
file write/flush/read/SHA-256 verification in AppData. Tests are sequential, not
simultaneous. The temporary file is automatically removed by the existing worker.
The original Python GPU workload is OpenGL; these are not equivalent GPU benchmarks.
Existing thermal limits require two measured hot samples (CPU 100 C/GPU 92 C).
Absent trustworthy sensors remain unverified, never a successful thermal check.

An atomic cancellation request works even immediately after start. Between
collectors and while waiting for stress completion it prevents later active
steps. Individual synchronous OS/SMART calls must return before cancellation
can finish; stopping is not advertised as instantaneous. Closing/Escape requests
stop and defers dialog deletion until the worker finishes, without blocking the
GUI thread. Global Pause also requests stop; Resume does not restart the scan.

## Report and presentation

The 640 x 480 dialog follows the original compact status/progress/verdict/log/
copy/close arrangement and adds an explicit Stop button. Logs are read-only,
bounded to 2000 text blocks. The complete final text is kept separately, so
copying does not lose text that scrolled out of the bounded log. A separate
banner shows the top three verdict titles and coverage; critical findings use
the semantic red accent. Missing coverage is not represented as healthy.

Schema-v3 diagnostic fields are retained. Additional scan metadata records
deep_local, complete/cancelled/failed/partial, error and completed steps.
Complete means the scheduled steps returned, NOT that every sensor/provider
was available. Cancellation, collector exceptions and safety stops produce
explicitly partial output, including on the main page and in TXT exports.
Hardware, autostart and memory-before data survive in the collected report;
the fresh OS-log difference is kept alongside the final log snapshot.

The main diagnostic action grid follows the original two-column arrangement.
The existing passive Collect Diagnostics button remains until Full Scan is
implemented; no nonfunctional Full Scan placeholder was added. The header
wraps across the panel width and finding titles have a usable scrollable column.
Each of the three docks has a scrollable body so narrow fixed-width windows
cannot overlap panels or leave their controls unreachable.
Copy, preview and Save TXT share the same deep-report formatter; Save JSON
preserves the full structured report. A later Internet result cannot silently
turn a deep-local report into an Internet scan.

## Known differences and next work

The main passive page still uses a findings table rather than the original
incident, CPU/GPU temperature, per-disk SMART, OS-log and public-IP cards.
Full Scan (Internet tests, antivirus, machine/Internet classifications and the
optional embedded problem-application step) is not part of Stage 32.

Memory collection records before/after endpoint snapshots, not a continuous
Python observation window. Current/sensor evidence in the diagnostic seed is
the start-of-scan snapshot; stress measurements add their own peaks, but a
fresh final general sensor snapshot and the original tray-state report section
remain to be ported. Extended hardware/memory TXT sections currently use readable
JSON, not the exact original prose formatter. Linux runtime, real hardware-load
execution, high-DPI and manual stop-during-provider-hang checks remain unverified.

## Verification

A fully injected StepFunction replaces the complete native execution boundary
in the new test target; missing fixture hooks cannot fall through to real load.
Tests cover default denial, concurrent-start rejection, phase order, monotonic
progress, fresh report data, visible critical verdict, copy availability, bounded
log, cancellation, retained memory-before, safety stop and collector exceptions.
Main-window tests decline the real confirmation and deliver a synthetic partial
report through the actual signal connection. Native dialog screenshots are
clearly labelled TEST DATA and do not constitute a real stress result.

Build/CTest, inspected Windows screenshots, archive integrity and extracted
runtime results are recorded in STAGE32-VERIFICATION.md beside the delivery ZIPs.
No real CPU/GPU load, full deep scan, LAN scan, speed test or public-IP lookup
is part of verification. The existing isolated 1 MiB disk fixture remains in
the regression suite. Original Python comparison was source-based, not a live
side-by-side pixel comparison.
