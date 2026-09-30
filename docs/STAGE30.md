# Stage 30 — restore the original Overview/Details contract

This checkpoint responds to the user's report that Stage 29 differed in both
appearance and behavior. It is not a claim of complete application parity.

## Reference

The supplied `C:/Users/ARTIOM/Desktop/My/_monitoring.py` did not exist. The actual
source is the directory `C:/Users/ARTIOM/Desktop/My_monitoring.py`. All 116 Python
files outside .venv match work/python-reference byte-for-byte. The original
source and settings were not modified. Comparison used source, not a live
side-by-side Python screenshot; the original virtual-environment interpreter
could not start during a read-only interpreter check.

The relevant originals are ui/window.py, ui/widgets/detail_card.py, the CPU,
GPU, RAM, OS and disk widgets, main_data_widget.py, sparkline_widget.py and the
theme/threshold definitions. Stage 29's full-width Details chart, tables and
filled sparklines were not the original composition and have been replaced.

## Restored design and behavior

- Details uses a scrollable 5:4 split: CPU and GPU on the left; operating system,
  RAM and individual mounted-volume cards on the right. Titles, labels, values,
  spacing, wrapping and four-pixel status stripes follow the original card.
- CPU shows load and temperature with session maxima, MHz and logical count;
  GPU shows load/temperature maxima and used/total VRAM; RAM shows percentage,
  session peak and used/total GiB. Windows displays its actual version instead
  of the generic string Windows. Unavailable readings remain explicit.
- Disk cards show occupied/total space, free space, read/write rates and total
  read/written GiB. Totals follow the provider's counter lifetime, not the app
  session. Cards represent mounted volumes, not necessarily physical disks.
  Identity combines name and mount point; updates preserve existing widgets,
  and missing/removed volumes are handled without guessed zero measurements.
- Extra native temperature/fan channels remain in a collapsible card, including
  source and available high/critical limits. No extra top-level Details header
  displaces the original composition.
- Component stripes use existing core thresholds: percent warning 76/critical
  95, generic temperature warning 68/critical 85, disk free warning below 15%
  and critical below 5%. Missing current values do not erase known session
  peaks or produce a healthy status. This does not change or prove parity of
  the application's complete alert confirmation/hysteresis lifecycle.
- Per-core history keeps collecting in Gamer Mode, as outside other hidden
  tabs. Global Pause still freezes collection. History controls and the legend
  wrap within the original narrow CPU column.
- Overview restores concise original descriptions, palette/font choices,
  compact headers/dock chrome and 30–40 pixel line-only mini graphs. Disk used
  percentage remains the primary metric; its type badge opens Details. Richer
  native telemetry is retained in its other consumers and reports.

Native movable/floating docks and persisted names remain; all five theme keys,
Quantum gauges, Slate graph hiding/dense mode and custom-theme blending remain.

## Verification and limits

A fresh Release build-stage30 uses Qt 6.9.3 and GCC 13.1 with Ninja (219 initial
actions), followed by changed-file builds. All 29 CTest targets pass. The new
Details test covers column membership, fields, peaks, status/theme changes,
stable disk widgets, distinct volumes, removed/empty disks, missing/NaN values
and vertical scrolling. The main-window contract additionally checks the disk
badge navigation, CPU history in Gamer Mode and original sparkline sizing.

Real Windows visual checks use isolated settings, Eclipse at 830 x 900, a
730 x 800 history layout, Quantum Cyan and Slate Minimal. Screenshot/smoke mode
skips public-IP lookup; ping is loopback. No live stress, LAN scan or speed test
is launched. Final archive hashes and extracted-runtime results are recorded
in STAGE30-VERIFICATION.md beside the delivery ZIPs, not inferred from tests.

Still outstanding: other screens' field/callback audit, cumulative network
receive/send/error/drop fields, quick/deep/full scan presentation, full alert
lifecycle comparison, high-DPI/manual floating-window checks and Linux runtime
and hardware gaps. Continue comparing the original before changing each screen.
Previous builds and archives remain available.
