# Stage 29 — first main-screen design pass

This checkpoint starts the promised visible design work after Network history
and startup. It does not claim complete migration or pixel-identical Python UI.
The Python MetricCardBody, DetailCard, CpuWidget and theme palette informed the
composition. Native movable/floating QDockWidgets and their persisted names and
layout state remain intact; no grid-based drag replacement was introduced.

## Overview and shared chrome

Labels inherit their card surface instead of painting separate black rectangles.
Card spacing, heading hierarchy and secondary-text contrast are improved.
Segoe UI is used for navigation/page headings/toolbar actions; metric text keeps
the theme font. Tab labels retain full names with scroll buttons when needed.
The eight canonical labels and two optional pages are unchanged.

Long values wrap inside narrow cards. An outer Overview scroll area makes the
minimum-height dock layout reachable in short windows. Sparkline painting no
longer hardcodes a dark blue background: its baseline, gentle fill and last-point
marker follow the graph color, including custom light themes. This is a drawing
change only; existing sample cadence/ranges and unavailable handling remain.

## Details

CPU, GPU, sensors and storage each have a named section. CPU keeps the full-width
chart; its summary is on a separate line so history controls fit at narrow widths.
GPU measurements use three columns under the adapter name. Tables have spaced
cells, left-aligned headings, right-aligned storage numbers and full-value
tooltips. Their height follows up to six rows; larger lists scroll internally.
The overall page remains vertically scrollable. Empty tables are replaced with
explicit unavailable/waiting text, not fake sensor or disk readings.

## Themes and validation

All five keys/settings remain compatible. Quantum still has three gauges;
Slate still hides decorative graphs and uses dense values. Custom panel/muted/
border colors blend the chosen background and text, supporting light as well
as dark configurations. This does not guarantee contrast for arbitrary user
choices such as identical foreground/background colors.

The UI contract covers the four section identities, scrollable containers,
full-name navigation, long values in a 300-pixel card, and transparent empty
sparkline rendering. Existing tests cover live theme changes, optional tabs,
card visibility/layout settings, telemetry, pause, Gamer Mode and startup.
Real Windows screenshots use isolated settings, loopback ping and disabled
public-IP lookup. No real stress, LAN scan or Internet speed test is required.
Final build, test, screenshot and archive results are in STAGE29-VERIFICATION.md
beside the delivered ZIPs.

## Next

Continue the visible pass on Networks, Hardware and Task Manager, then the
remaining dialogs. Overview/Details are a first reviewed pass, not the entire
design. Python's two-column Details composition and field-by-field parity,
cumulative network counters, scan presentation and Linux verification remain
tracked in STAGE21.md. High-DPI/manual floating-window interactions need a
broader final visual check. Previous stage builds and archives are preserved.
