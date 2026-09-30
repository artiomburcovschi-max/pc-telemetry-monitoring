# Stage 40 — trustworthy app-monitor intervals and system evidence

22 September 2026. Windows checkpoint, not completion of the full migration.

## Source-led changes

Compared reference core/app_monitor.py run/_collect_sample: the Python version
accumulates adjacent per-process deltas, resets baselines on pause, and captures
memory separately before/after. Stage 39 instead summed retained lifetime counters
and copied unversioned UI system values. Both semantics are corrected here.

- ProcessTreeCounters subtracts uint64 values before conversion to floating point.
  Per-process PID plus creation identity must match; adjacent complete tree
  observations are required for a tree rate. New/removed/missing processes,
  inaccessible values, reset/decreasing counters and unknown identities invalidate
  affected rates. Read/write/fault channels remain independent. No modulo wrap.
- Cumulative totals retain only measured per-process increments, including known
  increments during an otherwise partial tree interval. They are lower bounds,
  not lifetime counters or exact whole-session totals. Before-baseline activity,
  gaps, pause activity and the unobserved tail of exiting processes are excluded.
  Overflow is checked; no small wrapped total is reported.
- A pause generation forces rebaseline even if pause/resume occurs entirely during
  one collection. Samples crossing that generation are discarded. Existing active
  time accounting is still loop-based; extremely short toggles may not subtract
  their full wall duration, but cannot produce a resumed I/O/CPU baseline spike.
- TelemetryWorker builds an envelope in the producer thread. Metric source,
  quality, original UTC observation time and age at capture are preserved.
  UTC capture uses the same std::chrono clock as Metric, avoiding Qt/std clock
  precision differences observed in the Windows integration test.
- Consumption uses a shared monotonic-clock reference: source age plus delivery/
  cache age must be <=3000 ms. Stale, future, missing-timestamp, nonfinite and
  disallowed-quality measurements become null, including metadata values.
  Envelope republishing does not make old metric timestamps current.
- Pause/resume rejects pre-boundary measurements. Baseline is consumed just before
  launch, after log collection/pause. The endpoint after observation/closure must
  contain post-boundary metric data, not merely a newly delivered old envelope.
  A final background sample may be awaited up to1500 ms, or skipped when paused.
  There is no forced foreground hardware scan; unavailable endpoints remain unknown.
- Memory comparison records endpoint provenance and available deltas. Endpoint
  snapshots alone do not prove simultaneous application-induced memory pressure.
- New identified_adjacent_intervals_v1 reports use per-observation coincidences
  for I/O/disk, hangs/memory, faults/memory and app memory-pressure contribution.
  Older reports retain their compatibility path. Same-group findings may merge;
  related_findings retains the contributing IDs.
- The live card shows system-data availability. TXT and diagnostic coverage
  expose missing/stale endpoints and the I/O lower-bound interpretation.

## Verification scope

35 automated targets include new deterministic data contracts: uint64 precision
above2^53, overflow, membership changes, PID reuse, missing channels, reset,
pause/recovery, source age and queue delay, post-boundary timestamps, metadata,
memory deltas, unknown summary peaks and separated/coincident causal evidence.
Worker fixtures verify normal/manual/timeout/pause/cancel/exit lifecycle and
periodically refreshed before/after evidence. They launch and close only their
own bounded fixture executables. UI fixtures preserve chart/report behavior and
check the added freshness row and report limitation. No selected user EXE is launched.
Native screenshots are labelled synthetic data. Portable checks use isolated
settings and loopback ping, not LAN scans, speed tests or CPU/GPU stress loads.

## Deliberate remaining limits

- Polling is not an exhaustive process event trace; very short-lived descendants
  and final I/O tails can be missed. No extra termination authority is added.
- Linux ProcessCollector still supplies creationIdentity=0. The new app-monitor
  I/O/fault rates therefore remain unavailable there rather than infer identity
  from PID alone. Linux start-time identity/parser tests, safe process ownership
  and runtime validation are a required next stage; Windows runtime is the delivered build.
- Source summary positive-growth fraction, constant-series R² and symbolic
  exit-code labels remain to be ported; see STAGE39.md. The current throughput
  summary is not an elapsed-time-weighted average across irregular intervals.
- Native system metrics are asynchronous with an explicit3-second freshness
  bound, not perfectly simultaneous process/system measurements. Missing GPU/
  network/volume counters remain unavailable; disk throughput aggregation is over
  the backend's reported volumes, not proof of unique physical-device traffic.
- Very short pause duration accounting, long sessions, auxiliary windows,
  cross-screen lifecycle, all themes/high DPI and Linux are not fully audited.
