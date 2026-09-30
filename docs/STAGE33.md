# Stage 33 — original passive Diagnostics cards

Compared the original `ui/widgets/diagnostics_widget.py` and its `DetailCard`
contract with the native Diagnostics dock. Stage 32 had the collectors, report
engine and active Deep Diagnostics workflow, but the normal page still reduced
the result to a findings table. This slice restores the source-led passive
presentation without duplicating collection or weakening UNKNOWN states.

## Presentation and data flow

The Diagnostics dock now presents the original sections in order: problem
moment, CPU/GPU temperature anomalies, per-device SMART, OS errors since boot,
public IP/provider and the native findings summary. The findings table and
evidence detail remain below the cards because they expose useful schema-v3
reasoning that the original card view did not replace.

The incident card uses the existing native runtime ring and incident worker.
The in-panel danger action and toolbar action both start the same 60-second
before/15-second after capture. Collecting, complete and unavailable states use
the same card; final findings, coverage and the first action-plan steps are
shown without starting a second diagnostic operation.

CPU and GPU temperature cards update on every accepted native telemetry sample.
They do not invoke a sensor provider themselves. Each card shows the current
measurement, the source-matched warning/critical thresholds and duration of the
current state. Missing or non-finite measurements remain UNKNOWN and accumulate
an explicit no-data duration instead of becoming zero or normal.

SMART cards are rebuilt only when a passive or deep report supplies a fresh
SMART object. Every detected device gets a status stripe and health, media type,
reallocated/pending/uncorrectable counts and temperature. NVMe cards also expose
Critical Warning, percentage used and media/data-integrity errors. Collector
notes, risk reasons and the smartmontools installation hint remain visible.

The OS-log card uses the collector's normalized groups, counts repeated messages
and keeps its own bounded scroll area. The source and data quality remain visible;
an empty valid result is distinct from permission denial or collector failure.
The public-IP card reuses the startup/manual `InternetToolsWorker` result and is
updated through the same signal path as Network. Opening Diagnostics never starts
a speed test, public-IP request, LAN scan or load test by itself.

All cards reuse the native `DetailCard` component and hot theme colors. They are
tagged with a Diagnostics-specific property so the Details-page layout contract
and its persistent card identities remain separate. The outer diagnostic dock
already scrolls; screenshot tooling now supports scrolling that dock for
reproducible visual review.

## Verification boundary

The UI contract checks that the five passive sections and the in-panel incident
action exist. It injects a warning NVMe SMART device, duplicate OS events and a
successful public-IP result through the real worker signal connections, then
checks rendered fields, warning status and duplicate grouping. No Internet
request or CPU/GPU/disk stress is involved. The normal live startup collector is
still allowed to provide read-only SMART/Event Log data to the existing UI test.

The final Stage 33 build/CTest, Windows screenshots, archive identity and
extracted-runtime results are recorded in `STAGE33-VERIFICATION.md` beside the
delivery archives.

## Remaining boundary

The separate Full Scan is still not implemented. It needs an explicit combined
Internet speed/public-IP phase, antivirus inventory, machine/Internet
classification and optional embedded application observation, with its own
confirmation and cancellation contract. Deep Diagnostics still needs a final
general sensor refresh, tray-state report and exact extended Python text
formatting. Linux runtime, high-DPI/fractional scaling, provider-hang cancellation
and manually confirmed real hardware-load validation also remain.
