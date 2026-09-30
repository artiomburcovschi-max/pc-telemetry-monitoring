# Stage 41 — pause timing and app-report summary parity

22 September 2026. Windows checkpoint; the overall migration remains open.

## Changes and boundaries

- ObservationClock accumulates steady-clock durations at each actual pause/resume
  transition, not when the sampling loop notices the new state. Worker access is
  serialized under one dedicated mutex; timestamps are taken inside that lock.
  Duplicate Pause/Resume calls are idempotent. Sub-millisecond durations are kept
  until conversion to report seconds. Reset clears prior-run pause state/generation.
- Observation starts after a successful launch and collector construction.
  Manual stop freezes time immediately; timeout/early-exit freeze it when the
  worker detects the endpoint. Preparation, closure grace and report construction
  are excluded from both active_seconds and paused_seconds. A pause already in
  effect when observation starts is preserved, without counting pre-start time.
  observation_wall_seconds equals active + paused; timing_contract identifies
  this new scope. Sampling/OS scheduling can still delay detection of timeout or exit.
- Sample timestamps use active time after process collection. Samples spanning
  pause transitions during collection/assembly are discarded, and the next
  counters/CPU baseline is reset. Waits use real active-time deadlines rather than
  assuming each condition-variable wake consumed a nominal sleep duration.
- Autoclose cancellation now latches any pause transition after observation ends,
  including pause+resume entirely between grace-loop checks. Stop is still latched.
  Already sent WM_CLOSE cannot be undone. No new process-termination authority;
  all existing identity/ownership checks remain in AppProcessSession.
- Re-read Python core/app_monitor.py::_summarise_samples, _trend and
  _positive_fraction. Constant series with varying times have slope 0 and R² 1;
  a single point or zero time variance still has no fitted R². R² uses the
  residual-based formula, clamps to [0,1] and keeps 4 decimals in JSON.
- ram_growth_positive_fraction counts strictly increasing transitions between
  known, timestamped working-set values; equal values are not increases. The
  denominator is exposed as ram_growth_step_count. Like the reference, missing
  intermediate values are skipped, so this is neither a time fraction nor proof
  of continuous growth. Deliberate missing-data improvement: fewer than two
  known points produce null, not the reference's 0.0. Empty summary stays count 0.
- Ported the 10 exact symbolic Windows exit-code mappings/categories from
  Python core/process_memory.py::decode_windows_exit_code. Signed and unsigned
  32-bit JSON integers are supported; invalid, fractional, string, out-of-range
  and missing values are not coerced to status codes. Unmapped codes retain
  numeric/hex form with null name/category. Native Windows reports supply
  exit_details only for an observed exit code; no Windows decoding is imposed
  on Linux. A matching code is not evidence of a proven crash cause, and this
  addition does not change diagnostic verdict rules.
- Detailed text exposes time scope, growth fraction/denominator and known exit
  symbols with a caveat. The existing shared preview/copy/TXT/dialog report path
  remains intact. Stage40 freshness and observed-I/O lower bounds are preserved.

## Verification scope

36 automated targets. New deterministic clock tests cover short/zero-length
pauses, idempotent calls, start-while-paused, stop-while-paused, post-stop freeze,
new-run reset and 10,000 sub-millisecond pause pairs without sleeping.
Worker integration exercises rapid real transitions, paused manual stop and
transient grace cancellation using only its own bounded fixture executable.
Summary tests cover constant/linear/nonlinear/single/missing/equal-time series,
known-value growth fractions and all 10 exit mappings, signed normalization,
unknown/zero and malformed codes. UI tests assert the same additions in the
report dialog and preview; captured examples are explicitly synthetic data.
No real CPU/GPU stress, speed test, LAN scan or user-selected executable is run.
Portable delivery is expanded and checked against the staged files; runtime
launch is checked with only Windows folders in PATH and isolated settings.
See the separate release verification document for final run results and hashes.

## Remaining work

Full app-summary/diagnostic rule parity is not claimed; this closes the three
specific summary omissions recorded in Stage40. Weighted temporal statistics,
long sessions and additional summary edge cases still need source-led review.
Polling can miss short-lived descendants and final I/O. System observations
remain asynchronous with the existing explicit 3-second freshness bound.
Linux creation identity (starttime/parser tests), safe ownership/runtime,
auxiliary windows, cross-screen lifecycle and all themes/high-DPI remain open.
