# Stage 9 checkpoint — Diagnostics hub and report export

Stage 9 replaces the `Диагностика и тесты` placeholder with the original
three-dock composition used by the Python/PySide6 application. Diagnostics is
the large left panel; Stress Test and Report are stacked on the right. All
three panels can be moved or floated and cannot be accidentally closed.

## Working diagnostics

Opening the tab lazily builds a snapshot from the latest native telemetry and
runs the report-v3 diagnostic engine outside the GUI thread. A manual
`Собрать диагностику` button repeats the scan. Global pause prevents a new
scan and resume performs a deferred first scan.

The current checkpoint evaluates:

- temperature coverage and current CPU/GPU temperature levels;
- high current RAM occupancy without claiming that one sample proves a leak;
- low free space for every mounted volume;
- the number of enabled/unknown user autostart entries;
- the existing causal SMART, system-log, stress, memory, incident and
  application-monitor rules whenever their source objects are available.

Unavailable sources are explicit UNKNOWN findings. On the current Windows
machine the screen correctly reports that SMART and Windows Event Log
collectors have not yet been connected and that the CPU package sensor is not
published. These gaps are not presented as a healthy result.

## Findings and reports

- findings show localized severity/domain/confidence columns;
- selecting a finding shows its detail, evidence and suggested actions;
- the risk headline, critical/warning totals and coverage message come from
  the same schema-v3 contract used by golden tests;
- a detailed plain-text preview is generated from the report object;
- the preview can be copied to the clipboard;
- reports can be saved atomically as formatted JSON or UTF-8 TXT.

## Compatibility and boundary

All 11 CTest targets and all 17 Python-compatible diagnostic golden cases
pass. Stage 9 does not pretend that load generation is complete: its Stress
Test dock is restored in the correct layout but intentionally cannot start a
load yet. The next direct block is the safe native CPU/GPU/disk stress runner,
temperature safety stop, progress/cancel lifecycle and result integration into
the same report panel. After that come SMART/Event Log collectors and the
problematic-application observation screen.
