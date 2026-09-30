# Stage 19 — native Gamer Mode overlay

Stage 19 continues the Python/PySide6 to C++20/Qt 6 Widgets migration. It is a
tested checkpoint, not a claim of complete feature or visual parity.

## Session-only Gamer Mode

The Advanced settings page now activates a real native top-level overlay. It
uses the Python window contract: `Tool`, frameless and always-on-top flags,
fixed compact geometry, 0.88 opacity, mouse dragging, Escape/close handling and
a periodic topmost reassertion. Entering the mode hides the main window;
closing the overlay restores, raises and focuses it.

Gamer Mode is deliberately not written to `AppSettings`. A normal application
restart therefore always returns to the full main window, matching the Python
reference and avoiding an unexpected overlay-only startup.

## Honest live values

The overlay receives the existing native CPU, GPU, RAM and network telemetry.
Its ping remains independent from the main network page: a dedicated bounded
worker probes the configured host on TCP port 53 with a one-second timeout and
a two-second cadence. It runs only while the overlay is visible and monitoring
is not paused, and publishes unavailable state rather than invented latency.

`UiFpsCounter` measures monotonic deltas between actual overlay `paintEvent`
calls with a rolling 30-frame window. A 16 ms repaint request preserves the
original roughly 60 Hz intent; the label is updated separately so it does not
feed back into the measurement. The UI and tooltip explicitly say this is the
O.R.I.O.N. overlay render rate, never the frame rate of a game. As in Python,
topmost presentation over Exclusive Fullscreen is not guaranteed.

## Pause, alarm and lifecycle

Global pause stops repaint requests, topmost reassertion, ping probes and alarm
animation, and shows explicit paused values. Resume rebaselines FPS and ping.
Shared native thresholds drive separate critical colors for CPU, GPU and RAM;
an active alarm also pulses the overlay border over a 1.2-second cycle.

Shutdown is bounded: close stops timers and requests the ping thread to finish,
waiting no more than two seconds. A hidden inactive overlay performs no ping or
render work. While Gamer Mode is active, the hidden full dashboard skips its
heavy presentation refresh but continues telemetry, session peaks, runtime
ring capture, alarms and diagnostic data collection.

## Verification boundary

The Release build completes 151/151 steps and all 20 CTest targets pass. The
focused Gamer Overlay test covers rolling FPS, window flags/opacity, rendered
telemetry, per-metric critical styling, alarm state, global pause, visible-only
ping lifecycle, screenshot generation and bounded close. The full UI contract
also activates Gamer Mode through Settings and verifies main-window recovery.

A real Windows-rendered screenshot was inspected after headless testing. Text,
theme colors, critical states and the compact 280×250 layout are readable
without clipping. Automated tests do not run CPU or GPU stress.

Remaining migration work starts with the behavioral Quantum Cyan gauge and
Slate Minimal dense-layout flags, followed by final visual parity and a last
Python/C++ feature audit.
