# Stage 27 — native Network traffic history and crosshair

The Network tab now includes a C++/QPainter replacement for the original
Python pyqtgraph receive/transmit chart. No extra plotting runtime is required.
This is a migration checkpoint, not complete functional or visual parity.

## Data and interaction

Both series use the existing aggregate computer telemetry, not the adapter
selected for LAN scanning. The chart explicitly labels its scope and remains
separate from adapter link speed and the manual Internet speed test.

Samples use monotonic elapsed milliseconds, not an assumed one-second sample
index. The visible window is 60 seconds relative to the latest measurement,
with an additional 120-point memory cap. Startup has no invented zero history.
Negative/nonfinite rates are unavailable independently for each direction;
measured zero remains valid. Missing values, sampling gaps above 2.5 seconds
and even short manual pauses break the connecting line.

Both lines share one zero-based automatic scale, choosing KiB/s below 1 MiB/s
or MiB/s above it. The exact binary units are explained in the tooltip.
Hover snaps to the nearest sample within 1.5 seconds, draws a vertical cursor
and valid-value markers, and displays the sample's actual age and both rates.
No nearby measurement means no invented hover value. Leaving or hiding the
chart clears the crosshair. Tooltip placement stays inside the widget.

## Lifecycle and presentation

History is collected before main-window rendering is skipped for Gamer Mode.
Hidden tabs and Slate Minimal continue recording. Global Pause rejects queued
updates and freezes the historical view; Resume starts a new line segment and
ages old samples using elapsed wall duration. Theme changes do not clear data.

Slate Minimal hides the graph as in Python. Quantum uses the theme accent and
a denser grid; ordinary themes use green receive and red transmit lines. The
left Network panel now scrolls, keeping its graph and current values accessible
without forcing a taller main window. The right-hand scanner is unchanged.

## Verification hooks

A dedicated deterministic CTest covers zero/missing values, independent series,
timestamp rejection, monotonic positioning, time-window and memory bounds,
pause/gap geometry, nearest-sample units, leave/hide and themed rendering.
The main-window test checks early hidden collection, Gamer Mode, Quantum/Slate
visibility, continued hidden updates and real global pause/resume.

The test executable can save an explicitly synthetic render with
`--render-fixture <path>`; fixture samples are never injected into the app.
The app screenshot helper has `--screenshot-delay <seconds>` (bounded to 60)
to capture actual accumulated traffic. Screenshot/smoke mode still disables
automatic public-IP lookup. Neither helper starts a speed test or LAN scan.
Final build/test results and archive hashes are recorded in the output
STAGE27-VERIFICATION.md note.

## Remaining scope

Startup splash/hardware pre-scan is the next planned visible component, then
the main design pass. Python's cumulative receive/send and error/drop counters
were also found during this comparison and are now explicitly tracked in the
parity table. They are not implemented by this chart. Linux build verification
and the previously documented Hardware gaps remain separate work.
