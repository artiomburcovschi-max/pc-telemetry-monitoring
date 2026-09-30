# Stage 11 checkpoint — SMART and system-event diagnostics

Stage 11 replaces the diagnostic placeholders for SMART and Windows system
events with working collectors. It is an intermediate migration checkpoint,
not a declaration of complete Python/PySide6 parity.

## SMART collection

The background diagnostic worker now discovers `smartctl` and uses its JSON
interface (`--scan -j` followed by `-a -j`). The parser retains the original
Python contract for:

- overall SMART health and device/model/type identity;
- ATA reallocated, pending and uncorrectable sector counters;
- NVMe Critical Warning, available spare/threshold, percentage used, media
  errors, error-log entries and unsafe shutdowns;
- HDD/SSD/NVMe-aware temperature level;
- structured risk reasons and explicit data quality.

Pending or uncorrectable sectors, failed overall health, NVMe Critical Warning
and media errors are critical. Reallocated sectors, high NVMe wear and low
available spare are warnings. Missing `smartctl` or incomplete device data is
UNKNOWN and never silently converted to a healthy result. The report includes
an installation hint but does not install or bundle smartmontools.

## Native Windows Event Log

On Windows the program calls the Event Log API (`wevtapi`) directly; no
PowerShell or Python process is used. It queries Level 1/2 events from the
System and Application channels, limited to the current boot, and records ISO
timestamps, channel, Event ID, provider and one stable message line. Collection
quality, source and access errors remain explicit in the report.

Repeated messages are normalized and grouped for readable output. The
diagnostic engine follows the source Python thresholds for WHEA/Machine Check,
display-driver, disk/controller, memory-exhaustion and application-hang event
families. Known BcastDVR DistributedCOM background noise is suppressed before
the generic high-volume warning is considered.

The stress worker also captures read-only system-event snapshots before and
after its phases. A multiset difference preserves repeated new events and
feeds them into the existing report-v3 stability rules.

## Verification boundary

Stage 11 has 13 CTest targets. New coverage includes deterministic ATA/NVMe
SMART JSON parsing, Windows Event XML parsing, normalized grouping, duplicate-
preserving before/after comparison, diagnostic category thresholds, background
noise suppression and a read-only live system-log contract. The existing 17
Python-compatible golden cases continue to pass. Automated tests still never
start CPU or GPU load.

## Remaining migration boundary

The native program still does not claim full parity. Problematic-application
monitoring and incident capture, Deep Telemetry, richer report/UI details and
the remaining Python screen and behavior details must still be migrated feature
by feature.
