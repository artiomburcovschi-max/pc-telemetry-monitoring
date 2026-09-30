# Stage 15 checkpoint — complete diagnostic report presentation

Stage 15 ports the complete user-facing report structure from the Python
`core/report_snapshot.py` reference into the native C++20/Qt 6 report-v3
pipeline. It remains an intermediate checkpoint rather than a claim of full
Python/PySide6 parity.

## Report snapshot parity

The native TXT report now renders the complete diagnostic snapshot rather than
a short summary. It includes SMART devices and risk signals, grouped Event Log
entries and the post-stress comparison state, temperature capability and sensor
inventory, the live telemetry snapshot, detailed CPU/GPU/disk stress results,
runtime memory and paging interpretation, network-test state, incident
correlation, problematic-application observation, risk, coverage, verdict,
prioritized actions and every structured finding.

Missing values render as `н/д` or `НЕ ПРОВЕРЕНО`; UNKNOWN remains a coverage
gap and is never treated as component health. The same text is used by the UI
preview, clipboard and atomic UTF-8 TXT export. JSON remains schema version 3.

## Risk and counter-evidence

`buildRiskAssessment` now receives the stress and runtime objects, matching the
Python contract. It retains positive evidence when supported by measurements:
ample available RAM, hard faults without memory shortage, a stable measured CPU
load, absence of indirect throttling, verified GPU work, no new post-stress
system errors and verified disk-test integrity. The strongest actionable
finding supplies the risk summary; coverage-only findings do not become faults.

## Live-data propagation

The diagnostic snapshot now receives the current native runtime-pressure
object instead of rebuilding a minimal RAM-only approximation. Commit,
pagefile, paging-family counters, context switches, disk busy/latency and their
quality fields therefore reach the JSON/TXT report. The current temperature
and fan inventory, including native sources, is also preserved.

## UI and verification boundary

The report dock identifies the complete schema-v3 report, disables rich-text
interpretation and uses widget-width wrapping so the long report remains
readable in the narrow dock. A live 1102x932 Windows pass verified the Overview
and Diagnostics pages, real SMART/Event Log/sensor/runtime content, the long
report preview and its scrolling behavior.

All 16 CTest targets pass. The shared 17-case golden runner now also validates
required and forbidden report text fragments; the hard-fault golden case proves
that ample RAM does not become a false pagefile-pressure diagnosis. Automated
tests do not start CPU/GPU stress.

## Remaining migration boundary

Continue the remaining Python screens and detailed behavior feature by feature.
Final visual polish is still pending, while layout and readability continue to
be checked at every checkpoint.
