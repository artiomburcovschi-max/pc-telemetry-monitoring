# Stage 10 checkpoint — Native stress-test runner

Stage 10 turns the restored `Стресс-тест` dock into a working native runner
and feeds its structured result back into the existing report-v3 diagnostic
pipeline. This is an intermediate migration checkpoint, not a declaration of
complete Python/PySide6 parity.

## Native workloads

- CPU load uses every logical processor. Live native telemetry is sampled
  during the run, and two consecutive CPU temperature samples at or above
  100°C trigger an immediate safety stop.
- On Windows, GPU load is a Direct3D 11 compute shader with result readback and
  verification. Two consecutive GPU temperature samples at or above 92°C
  trigger an immediate safety stop. An unavailable D3D11 backend remains an
  explicit UNKNOWN coverage result rather than a successful test.
- The disk phase writes a bounded temporary file, flushes it to storage, reads
  it back, verifies SHA-256 and removes the file automatically. The UI offers
  50, 100, 200 and 500 MiB sizes.

CPU, GPU and disk phases run sequentially. The user selects a 30, 60, 120 or
180 second duration for each enabled compute phase. GPU and disk workloads
require their own explicit acknowledgement, followed by a final warning. The
progress bar, current phase, emergency stop and final result stay visible in
the Diagnostics hub. Global monitoring pause also cancels an active run.

## Diagnostic integration

The completed stress object is injected into the next diagnostic snapshot.
The engine now emits focused findings for:

- a thermal safety stop (`critical` / `fail`);
- an unavailable GPU backend (`coverage` / `unknown`);
- a failed GPU output verification (`critical` / `fail`).

The human-readable report includes the selected workloads, early-stop reason,
CPU/GPU measurements and disk verification result.

## Automated safety boundary

The default CTest suite never starts CPU or GPU load. Its stress-worker test
sets `runCpu=false`, `runGpu=false`, `runDisk=true`, writes only 1 MiB inside a
temporary directory and checks that no file remains. Real CPU/GPU validation
must be started interactively and only with explicit permission from the
machine owner.

Stage 10 passes 12 CTest targets and all 17 Python-compatible diagnostic golden
cases. The UI contract covers the restored controls and Diagnostics layout.

## Remaining migration boundary

The native program still does not claim full parity. Richer SMART and Windows
Event Log collection, problematic-application monitoring and incident capture,
Deep Telemetry and remaining screen/behavior details must still be migrated
from the Python source feature by feature.
