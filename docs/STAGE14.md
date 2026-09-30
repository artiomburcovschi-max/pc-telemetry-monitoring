# Stage 14 checkpoint — native runtime pressure telemetry

Stage 14 completes the next runtime-telemetry slice from the Python reference.
It is an intermediate checkpoint, not a claim of full Python/PySide6 parity.

## Memory, paging and quality

Windows commit used/limit/peak comes directly from `GetPerformanceInfo`.
Pagefile occupancy and peak occupancy come from `EnumPageFilesW`; they are not
used as proof of active paging. Native `NtQuerySystemInformation` deltas expose
pages read, read operations, output/write activity and system context switches.

The deterministic `assessWindowsPaging` rule mirrors the Python contract:
`Pages Input/sec >= 100`, `Page Reads/sec >= 5` or `Pages/sec >= 1000` marks a
hard-fault burst, but confirms memory pressure only with available RAM below
15% or commit at/above 85%. With ample RAM and commit it returns
`paging_activity=unconfirmed` and
`hard_fault_reads_without_memory_shortage`. Windows endpoint rates stay
`estimated` because file-backed hard faults are not pagefile-only traffic.

## Context switches and storage latency

System context switches use cumulative kernel performance deltas. The Windows
process collector uses the native Thread performance object to map thread
context-switch rates to PID; the application monitor aggregates only the
launched process tree. Missing counters remain null rather than becoming zero.

`IOCTL_DISK_PERFORMANCE` now retains read/write time and operation counts for
every readable volume. Consecutive samples produce busy percentage and average
read/write latency. The system view follows the Python disk-busy rule by using
the busiest sampled volume; aggregate latency is throughput-weighted. The live
storage card, telemetry JSON, app-monitor samples, summary and TXT report all
receive these values.

## Timestamp-level Event Log correlation

The system-log layer parses Windows fractional UTC timestamps and ISO offsets,
merges before/after snapshots without duplicates and filters an asymmetric
window around a user marker. Incident capture preserves both the exact
before/after multiset diff and the independent timestamp window, so an event
that already existed in the first snapshot can still correlate with a freeze.
Unparseable legacy rows become explicit coverage UNKNOWN. App-monitor reports
also retain a timestamp-filtered session window.

## UI and verification boundary

The RAM card now shows commit and pagefile occupancy. The storage card shows
busy and latency on a dedicated I/O line, with capacity/type on a second line.
The final 1102x932 visual pass verified the overview and the full problematic-
application page; the first pass exposed an orphaned wrapped storage type and
the two-line layout corrected it.

Stage 14 keeps 16 CTest targets and all 17 shared golden cases. Deterministic
tests cover false-positive-resistant Windows paging interpretation, timestamp
window selection/UTC conversion, telemetry serialization and disk/context
summary fields. Live platform smoke verifies commit, paging-family and system
context-switch counters. Automated tests do not start CPU/GPU stress.

## Remaining migration boundary

Continue richer report presentation and the remaining Python screens and
behavior details feature by feature. Final styling can be polished later, but
layout, readability and interaction contracts remain checked at every stage.
