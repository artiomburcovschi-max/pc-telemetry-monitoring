# Stage 13 checkpoint — native Deep Telemetry

Stage 13 replaces the toolbar placeholder with the original independent Deep
Telemetry window. It is an intermediate migration checkpoint, not a claim of
complete Python/PySide6 parity.

## Window and telemetry contract

The toolbar action and the overview OS-error badge open one non-modal window;
repeated activation raises the existing instance rather than creating
duplicates. The window follows the Python layout: toolbar controls, a vertical
splitter, two separate per-core charts and the current-boot system-error view.
The removed historical "Kernel resources" panel has not been reintroduced.

Frequency and load are never scaled into one fake unit. Logical-CPU load is
drawn on its own fixed 0–100% chart. Windows reads each available processor's
`~MHz` registry point estimate; Linux reads each `cpu MHz` row. When the
platform only exposes one aggregate estimate, the window deliberately draws
one row and labels that limitation instead of cloning the value across cores.
Critical load turns the live bars red but does not freeze updates.

Session peaks are owned by the main window and updated for every telemetry
sample, including while Deep Telemetry is closed. The window therefore shows
the real whole-session CPU, RAM, GPU and temperature peaks plus session uptime.
Its local pause freezes only these charts. Global pause disables local control,
stops upstream telemetry and blocks a new Event Log refresh.

## Errors, logs and export

Critical/Error entries from the Windows System and Application channels since
the current boot are collected on a worker thread through the existing native
`wevtapi` collector, so opening the window does not block the GUI. Refresh and
session-view clear match the Python behavior; clear never mutates Windows Event
Log. The system-log button displays the active UTF-8 O.R.I.O.N. application log.

Atomic JSON export contains the captured timestamp, current per-core frequency
and load rows, explicit frequency sampling mode, session uptime and peaks, plus
the currently displayed system errors with source and data quality.

## Verification boundary

Stage 13 has 16 CTest targets. The dedicated deterministic dialog test checks
per-core transfer, export JSON, session peaks and local/global pause. The full
main-window contract opens the real non-modal window and checks both charts,
the error view and local pause. All 17 shared golden cases continue to pass;
automated tests do not start CPU or GPU stress.

## Remaining migration boundary

Windows commit/pagefile occupancy, hard-fault/paging rates, context switches,
disk busy/latency, timestamp-level Event Log correlation, richer report
rendering and the remaining Python screen/behavior details still need to be
migrated feature by feature.
