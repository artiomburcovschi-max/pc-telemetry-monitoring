# Stage 44 — incident producer freshness and same-observation evidence

23 September 2026. Windows C++20/Qt6 checkpoint; not full migration completion.

Read Python diagnostic_session.py recording/normalization/summary and
insights.py::analyze_incident before changing the native path. Python accepts
recent UI receipt as app freshness and combines window peaks. This stage retains
the feature and thresholds but deliberately tightens evidence for new native
reports. Historical reports without the new contract keep compatibility behavior.

## Producer data, not UI receipt time

incident_data.* adds a pure envelope/builder layer. TelemetryWorker supplies
original Metric value/quality/source/observed_at, age at producer capture, and a
QElapsedTimer reference timestamp. Source UTC uses the same std::chrono clock
as Metric timestamps. Disk sums reuse the all-volumes-known app-envelope helper.
Network deltas retain tracker quality/time and system counter scope.

The UI builder accepts only increasing, nonnegative, session-relative producer
captures, no later than receipt and no older than 3000 ms. It rejects captures
before a real pause/resume boundary; per-metric source time must also be after
that boundary. Source age + queue delay is checked for every field, not just the
envelope. Valid/estimated finite nonnegative metrics are usable, percentages
must be <=100 and frequency >0. Rejected values are null even in metadata;
source, time, age, effective quality and acceptance remain inspectable.
Rows use producer capture time within the session, not delayed UI callback time.
Old/malformed/future/replayed frames leave a gap rather than creating new rows.

Ping now carries an absolute monotonic timestamp from probe start. A target
different from current settings, stale/future/pre-pause probe is not included.
Application samples carry collection-start monotonic and wall timestamps from
the process worker. The incident builder accepts only a running monitor's
recent snapshot not later than the system frame, not a renewed UI receipt time.
RAM share is recomputed from known process RAM and current accepted system RAM
total; stale context and unknown RAM do not invent an application contribution.
These process timestamps describe a collection request, not atomic per-process
reads. Collection duration is included in its age, conservatively.

The builder tracks network interval end timestamps by channel/scope/source.
Replayed or older interval ends become unknown rather than being added twice;
zero remains zero. Replayed whole captures are rejected independently. Scalar
cached metrics may remain usable in multiple rows within the declared freshness
interval; statistics and match counts are row-based, not independent sensor
observations or time-weighted duration. Repeated rows do not prove causality.
The ring still contains at most 1200 rows / 300 seconds.

## New observation contract

Rows and summaries use measurement_contract=fresh_incident_observations_v1,
independent of Stage43 temporal window quality. With any new-contract rows,
legacy rows retain only temporal positions, not unprovenanced metric evidence.
Within baseline/focus/recovery, a metric whose own source time lies outside that
partition is excluded; a cached pre-focus value cannot leak across the boundary.

New findings use accepted source times within 1000 ms of each other in one row:

- Memory pressure: available RAM <10% plus at least one active endpoint counter
  (pages input >=100/s, page reads >=5/s or pages >=1000/s) in that observation.
  Low RAM and a later hard-fault burst no longer combine into confirmation.
- Application contribution: the same pressure observation plus RAM share >=15%,
  fresh process context and accepted RAM total, all within the time-spread bound.
  An unrelated app peak does not receive blame. The finding says RAM was occupied
  during the shortage, not that a leak or the cause of the freeze was proven.
- CPU thermal hint: usage >=60%, elevated component-threshold temperature and
  frequency >=10% below accepted pre-focus average in that same observation.
  Critical severity requires critical temperature and >=20% drop in that SAME
  row. Source-time skew or separate temperature/frequency peaks are insufficient.
  Confidence remains medium: baseline conditions differ and no hardware throttle
  flag/PROCHOT is read. Wording explicitly distinguishes a hint from proof.

Counts include every match; detail arrays are bounded to 40 observations each.
Metric timestamps remain in samples for inspection. Partial coverage, stale
metric occurrences, repeated network intervals and stale app contexts are
reported separately; temporal coverage alone never means healthy hardware.
Card and TXT show source exclusions and matched counts. TXT includes CPU and RAM
matched observations; JSON retains full source metadata and app-match details.

## Verification

38 automated targets including new incident_data_tests. Deterministic fixtures
cover source age plus queue delay, future/old/replayed frames, pre-pause values,
zero/invalid ranges, stale source quality, duplicate network intervals, stale
ping/app context, future app samples, partition source-time boundaries and legacy
isolation. Negative cases deliberately separate high temperature/frequency drop,
low RAM/hard faults and an application's RAM peak. Positive and exact 1000 ms
boundary cases retain findings, evidence and qualified report wording.
Telemetry, ping and real bounded app-worker tests check producer timestamps.
UI tests check the source counts/unknowns in labelled synthetic incident cards.
Final CTest/native 100%/150%/archive/portable results are in the separately
delivered outputs/STAGE44-VERIFICATION.md, outside the source archive.

## Remaining boundaries

This does not upgrade legacy imported reports to new evidence semantics. It
trusts backend Metric timestamps and qualities; it cannot detect an upstream
collector that falsely restamps cached sensor data. Asynchronous measurements
within one second are not truly simultaneous. Network sums remain observed
lower bounds; delivery loss and incomplete intervals are not repaired.
Windows hard-fault counters do not identify pagefile reads uniquely; thermal
inference is not a hardware throttling flag. No new Linux paging collector.
Whole frames rejected before buffering appear as gaps, not retained raw payloads.
Broader CPU saturation/rule parity and source log-category confidence remain.

Next bounded work: Deep Telemetry file-save/overwrite behavior and boot-log size
limits/lifecycle. Other windows, themes/DPI, long-session performance and Linux
process identity/safe runtime still need verification. Tests do not run real
stress/speed/LAN scans or a user-selected executable.
