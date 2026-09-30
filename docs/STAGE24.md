# Stage 24 — native multimode logical-CPU history

Stage 24 continues the Python/PySide6 to C++20/Qt 6 Widgets migration. It is a
tested checkpoint, not a claim of complete feature or visual parity.

## One comparative CPU chart

The Details page no longer renders one progress row per logical processor. It
now uses one large dependency-free QPainter chart with the Python contract's
shared 0–100% scale and three selectable views:

- a connected profile of every logical CPU's current load (the default);
- a classic histogram whose bars use the shared OK/warning/critical thresholds;
- rolling 60-second lines, paged in groups of at most eight logical CPUs with a
  visible legend and a stable one-based CPU identity.

The control reports the current average and busiest logical CPU. Tick labels
thin out deterministically on machines with many processors, while the final
CPU remains labelled. The Details page is scrollable so the 285-pixel chart
does not compress the existing GPU, sensor and storage sections at the minimum
supported window width.

## Honest history and lifecycle

Every telemetry delivery records a per-core sample regardless of which tab is
visible; a hidden chart schedules no painting but keeps its minute of context.
Real 0% values are retained. A missing core or unavailable tick appends NaN and
therefore produces a visible line gap instead of a fabricated idle value. A
newly appearing processor is aligned to the existing time axis with earlier
gaps, and disappeared processors remain identifiable with later gaps.

Each per-core ring is capped at exactly 60 samples. An already queued telemetry
delivery is ignored after global Pause, preventing the frozen history from
advancing by one stale tick. The selected `profile`, `bars` or `history` mode is
saved immediately through the existing atomic AppData settings contract and is
restored on the next launch. Chart colours update with all five native themes.

## Verification boundary

The Release build completes all 185 build steps and all 25 CTest targets pass.
The dedicated chart test covers mode controls, zero-load retention, clamping,
NaN gaps, summary text, eight-core paging, selection of a partial final group,
the 60-sample cap, invalid-mode fallback and an off-screen paint. The expanded
main-window contract verifies the three persisted mode keys and proves that the
sample count advances while another tab is active.

A real Windows-rendered 830×820 screenshot was inspected with 20 logical CPUs.
All processors, the shared scale, current summary and mode selector are readable;
GPU and sensor details remain reachable below through the page scroll bar.

## Remaining parity work

The next highest-value audited slice is the full Hardware inventory:
motherboard, BIOS, RAM modules, displays, battery, adapters, NPU and PC rating.
The startup splash and richer network-traffic history/crosshair remain later
audited slices.
