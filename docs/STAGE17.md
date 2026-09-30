# Stage 17 — native background ping telemetry

Stage 17 continues the Python/PySide6 to C++20/Qt 6 Widgets migration. It is a
tested checkpoint, not a claim of complete feature or visual parity.

## Cancellable native producer

The former hard-coded `net_ping_ms: null` placeholder is replaced by a
dedicated `PingWorker`. It performs the same bounded TCP connection check as
the Python reference: the persisted target, port 53, a one-second timeout and a
two-second interval. DNS and connect latency never run in the GUI thread or in
the main system-telemetry worker.

The producer has independent pause/resume, target-change wake-up and bounded
shutdown. A changed target invalidates an in-flight old-target result, so stale
identity cannot be published after settings are applied.

## Data-quality contract and propagation

A successful connection publishes measured milliseconds with `valid` quality,
the exact target, port, source and observation time. A timeout, resolution
failure or refused connection publishes no invented number: latency remains
null/`н/д`, quality is `unavailable`, and the source reason is retained.

The value and quality now reach:

- the Overview network card and the Networks page;
- the persisted, hot-applied ping-target setting;
- versioned telemetry JSON through the native `net_ping_ms` metric;
- every runtime-ring sample used by `Проблема сейчас`;
- system samples merged into problematic-application observation;
- diagnostic runtime and network snapshots.

Global monitoring pause also pauses the ping producer. Resume triggers a new
probe without waiting for the former periodic deadline.

## Verification boundary

The Release build completes 120/120 steps and all 17 CTest targets pass. The
new deterministic ping-worker test uses a loopback TCP server and covers valid
latency, unavailable quality, pause/resume and bounded shutdown. The report
contract verifies ping serialization with value and source; the UI contract
verifies that the target is enabled, hot-saved and the live ping field exists.

No CPU/GPU stress is executed by tests. Remaining migration work is the real
optional Servers and Terminal tabs, Gamer Mode overlay, and the behavioral
Quantum Cyan gauge / Slate Minimal dense-layout flags before final visual
polish.
