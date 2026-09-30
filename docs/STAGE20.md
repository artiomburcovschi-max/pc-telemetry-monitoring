# Stage 20 — behavioral Quantum Cyan and Slate Minimal themes

Stage 20 continues the Python/PySide6 to C++20/Qt 6 Widgets migration. It is a
tested checkpoint, not a claim of complete feature or visual parity.

## Quantum Cyan gauges

Quantum Cyan now changes behavior as well as colors. The overview presents CPU,
GPU and RAM through a native `GaugeWidget`: a 270-degree arc, centered current
value, metric label and an honest unavailable state. New values animate for
400 ms with an OutCubic curve and remain clamped to the 0–100 percent domain.
The track, active arc, glow and text follow the active theme.

Disk and network keep their text-card presentation because their primary values
are not percentage-only gauges. Existing telemetry quality remains authoritative:
an unavailable sample is displayed as unavailable rather than converted to zero.
Quantum progress bars use the intended cyan-to-orange gradient and rounded
geometry.

## Slate Minimal dense layout

Slate Minimal now applies the Python dense-layout intent. Overview gauges and
sparklines are removed, while every card keeps its actual value and explanatory
detail together in one compact summary. Card minimum heights, margins, table row
heights, tabs, docks and toolbar controls are tightened. Decorative toolbar and
problem-action icons are omitted, progress bars become flat, and control/card
corners are square.

This is a presentation change only. Telemetry producers, quality values, alarms,
pause state, navigation and settings remain shared with every other theme. A hot
theme switch immediately updates the existing widgets without an application
restart.

## Verification boundary

The clean Release build completes 158/158 steps and all 21 CTest targets pass.
The new focused gauge test covers initial unavailable state, animated arrival at
73 percent and reset to unavailable. The main-window UI contract hot-switches to
Quantum Cyan and verifies exactly three visible gauges, then switches to Slate
Minimal and verifies five dense cards with every gauge and sparkline hidden.

Real Windows-rendered screenshots were inspected at 830×820 for Quantum Cyan and
at the supported 730×800 minimum for Slate Minimal. Gauges, text, tabs and live
values remain readable without clipping. Automated and visual checks did not run
CPU or GPU stress.

Remaining migration work starts with a complete Python/C++ navigation, settings
and feature audit, followed by cross-screen visual parity review and closure of
any gaps found by that audit.
