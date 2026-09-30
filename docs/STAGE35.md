# Stage 35 — fresh sensors and readable extended reports

Stage 35 closes the remaining report gap in the native Deep Diagnostics and
Full Scan workflows. Both scans now use one shared human-readable formatter for
the extended machine, autostart, post-load sensor, memory and tray sections.
The structured schema-v3 JSON is still retained for machine processing; only
the copied/saved TXT representation changed.

## Fresh post-load evidence

Deep Diagnostics now performs one ordinary native telemetry sample after the
CPU, GPU and disk sequence and after the second Event Log snapshot. This is a
new explicit `SensorsAfter` step, not the sensor state cached when the dialog
was opened. Full Scan inherits the same result through its Deep phase.

The result records capture time, collection quality, unique providers, every
published temperature and fan channel, and a compact CPU/GPU temperature state
used by diagnostics. CPU/GPU values may fall back to a matching general sensor
channel when the backend did not publish a separate primary metric. Missing,
unsupported, permission-denied and collector-error evidence remains explicit;
an absent sensor is never converted to zero or normal.

The new sample is not an additional stress phase. It uses the normal read-only
system backend and does not contact the Internet. Cancellation still prevents
all later steps from starting.

## Shared readable TXT sections

Deep and Full reports no longer append raw indented hardware or runtime JSON.
Their common formatter includes:

- OS, motherboard/BIOS, CPU topology/socket, RAM and modules, GPU/NPU;
- physical disks with identity, bus/media, firmware and mapped volumes;
- displays, battery and network adapters when published;
- autostart totals with system entries collapsed and user entries listed;
- the fresh post-load sensor time, quality, sources, CPU/GPU state, temperature
  channels, thresholds and fan readings;
- before/after RAM, commit, pagefile and paging snapshots with an explicit note
  that these are endpoints rather than continuous observation;
- configured tray icon, threshold notifications, actual tray availability and
  visibility, and current pause state.

Unknown values are rendered as `н/д` with their evidence quality or reason.
The same formatter is called by both workers, so Full Scan cannot drift back to
a different extended-report layout.

## Verification boundary

All 32 CTest targets pass. The Deep fixture owns every active boundary,
including the new post-load sensor step, and verifies exact order, monotonic
progress, replacement of a cached sensor timestamp, tray preservation and the
absence of raw hardware JSON in TXT. The Full fixture verifies that the same
sensor, memory and tray sections survive composition with antivirus and
Internet results. Cancellation and safety-stop tests remain active.

The reviewed dialog screenshots are explicitly labelled TEST DATA. Verification
did not run CPU/GPU load, the 200 MiB disk phase, antivirus PowerShell, a speed
test, public-IP lookup or LAN scan. The normal read-only telemetry probe was
allowed. A manually confirmed real-load run, Linux runtime, high-DPI/fractional
scaling and cancellation of a hung external provider remain separate checks.
