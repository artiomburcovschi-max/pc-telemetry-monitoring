# Stage 2 — overview, CPU details and runtime controls

Stage 2 keeps the Stage 1 contracts intact and ports the first user-facing
runtime slice to native C++20 and Qt 6 Widgets.

## Source layout

The source archive opens directly into the project. There is no generated
wrapper directory and no deployed Qt plugin tree:

```text
core/          shared telemetry models and algorithms
ui/            Qt Widgets window, pages and reusable widgets
platform/      Windows and Linux system collectors
diagnostics/   finding model, report v3 and golden runner
storage/       AppData paths, settings and logging
tools/         telemetry probe and network scanner
tests/         unit, contract, golden, platform and worker tests
docs/          migration notes
packaging/     runtime notices only
```

## Added in Stage 2

- overview cards for CPU, RAM, network and the current monitoring session;
- CPU details page with a live row for each logical processor;
- current Windows CPU frequency estimate from the processor registry;
- native per-processor utilization from Windows system processor times;
- equivalent per-core `/proc/stat` and `/proc/cpuinfo` support on Linux;
- one global pause state for the toolbar and tray menu;
- a worker lifecycle that blocks while paused and resumes without recreation;
- tray show, pause and quit actions, close-to-tray behavior and threshold
  notifications;
- editable tray, notification, always-on-top, opacity, ping target and CPU-view
  settings saved atomically below AppData;
- a seventh automated test for the pause/resume contract.

## Verified contracts

- all 7 native CTest targets pass;
- all 17 shared diagnostic golden cases pass;
- report schema version 3 and stable top-level keys remain unchanged;
- the native Windows probe reports CPU, every logical processor, frequency,
  RAM and network throughput;
- the Qt application starts, receives live data, renders and exits cleanly.

## Still deferred

The functional reference remains the Python/PySide6 project. GPU, temperature,
fan, SMART and full disk collectors; diagnostic workflow pages; stress tests;
reports; long-running application monitoring; incident marking; and the full
theme/card customization surface remain later migration stages.

The Windows runtime archive intentionally omits the optional Qt `generic`,
`networkinformation` and `tls` plugin groups. `platforms/qwindows.dll` remains
because it is the required Qt Windows display backend, not project source code.
