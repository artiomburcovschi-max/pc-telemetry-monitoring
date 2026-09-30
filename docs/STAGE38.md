# Stage 38 — observed application lifecycle and honest results

Source comparison of the original AppMonitorWidget/AppMonitorPanel and the
ProcessMonitorThread launch/pause/stop/timeout collection paths found missing
live fields and graphs, plus more urgent native lifecycle problems. This stage
addresses the lifecycle and result quality; it does not claim full screen parity.

## Changes

- Paused time no longer consumes the requested observation duration. Resume
  rebuilds the CPU baseline and resets rate baselines; no paused-period spike.
  A pause during startup holds launch, and Stop before launch prevents it.
- Windows launch now retains the process handle returned by CreateProcessW.
  Observed descendants are matched by creation timestamp and parent lifetime,
  and their handles remain owned for this observation. Sampling rejects a
  mismatched identity. Forced closure rechecks identity and critical-process
  status on the same handle used for termination. Missing identity fails closed.
- Timeout performs fresh descendant discovery before closing. Stop or global
  Pause during the seven-second grace period cancels further forced closure.
  Already delivered WM_CLOSE/termination requests cannot be recalled. Reports
  preserve cancellation reason, survivors, errors and incomplete tracking.
- Retaining the root handle preserves its actual exit code even after early
  exit. Destroying the session releases handles only; it does not kill processes.
  The window retains a worker if a provider call outlives the shutdown wait.
- Missing CPU, memory, handles or rates remain null when the complete tracked
  tree cannot be measured. A windowless application has unknown responsiveness,
  not a fabricated healthy window. Summary fractions exclude unknown response
  samples, and gaps break a hung-window streak. The UI preserves nulls as н/д.
- Launch/browse/duration/auto-close controls obey pause and running state. Live
  values reset for a new session, late queued samples cannot defeat Pause, and
  the source-style duration-aware interval hint is visible. Restored fields:
  threads/handles, page faults, system commit and explicitly system-wide GPU.
- UI and readable report share outcome text: failed launch, manual stop, early
  exit, timeout without closure, closed tree or cancelled/incomplete closure.
  TXT/copy additionally include launch errors, exit code, pause time, closure
  errors/survivors, verdict findings, action plan and coverage.

Implementation references: Microsoft [CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
and [process handles](https://learn.microsoft.com/en-us/windows/win32/procthread/process-handles-and-identifiers).

## Verification design

The expanded native lifecycle test launches only its own bounded fixture:
manual stop leaves it alive; timeout closes root plus child; a 2.2-second pause
does not consume a two-second session; Stop/Pause during grace leaves survivors
and no forced kill; a fast exit preserves code 37; missing executable appears in
TXT. Two owned fixture sessions test rejection of a forged descendant identity.
No existing user applications are launched, closed or terminated by these tests.

UI checks use worker signals only, never launch the supplied executable path.
They cover nulls versus true zero, >100% multicore CPU, restored details, pause
gating, result-during-pause, interval hints and cancelled-closure presentation.
Summary/TXT regression tests cover unknown window response and omitted errors.
Executed outcomes are recorded in the delivered STAGE38-VERIFICATION.md.

## Remaining work

- Restore the original bounded CPU and working-set/private memory history graphs,
  crosshair and verdict card; complete detailed TXT/JSON field-by-field parity.
- Audit system snapshot freshness and the exact sampling/IO semantics further;
  long sessions and all themes/high-DPI still need broader validation.
- Windows discovery remains polling-based: a very short-lived launcher can exit
  and spawn/lose descendants between snapshots. Do not claim job-object-level
  exhaustive ownership. Changes after the timeout discovery snapshot may remain
  outside this tracked set. Unknown ownership is never permission to close.
- Linux retains its previous read-only process-tree sampling path; native safe
  auto-close and handle-equivalent ownership/exit tracking are not implemented
  or runtime-validated there. The Windows release is the verified target.

Next stage: graphs/verdict/detail-report parity on this same screen, then other
secondary windows and end-to-end review. The migration remains unfinished.
