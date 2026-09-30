# O.R.I.O.N. Native

Native C++20/Qt 6 migration of O.R.I.O.N. Monitoring. The repository has one
shared core and explicit Windows/Linux system backends. Python is not required
at runtime.

The source archive contains `ORION-source/` with `core/`, `ui/`, `platform/`,
`diagnostics/`, `storage/`, `tools/` and `tests/`; generated build trees and Qt
deployment plugin folders are not part of the sources.

## Stage 49 checkpoint

- Incident CPU saturation uses the reference peak >=95% and known-row fraction
  >=40% at >=90% CPU, not a substitute mean threshold; evidence explicitly
  distinguishes sample fraction from continuous time under load;
- incident OS events retain category-specific counts, exact timestamps, source
  (snapshot difference versus existing timed records), and incomplete-log quality;
- unrelated nearby records no longer elevate another category's confidence.
  Temporal coincidence does not establish the cause of a crash;
- 43 test targets; production UI checks include the qualified report and the
  independent-card/layout regressions. See `docs/STAGE49.md` and
  `docs/FINAL-CHECKLIST.md`. Application/stress rules and final QA remain open.

## Stage 48 checkpoint

- OS event collection bounds Windows XML/message allocations, per-channel input
  budgets and examined records; retained event lines are Unicode-safe and bounded;
- journalctl uses drained, capped stdout/stderr, a deadline, and bounded JSON-line
  parsing. Real Linux integration remains unverified; portable helpers run in tests;
- skipped/shortened/incomplete data is explicitly estimated, with limits, counters
  and warnings retained in JSON, UI/TXT and log comparison/correlation;
- 42 test targets. See `docs/STAGE48.md` and `docs/FINAL-CHECKLIST.md`.
  This is another Windows checkpoint, not the final migration.

## Stage 47 checkpoint

- Diagnostic dock layout is now saved with settings and restored on startup,
  including native version 1 and the Python reference's version 0;
- malformed/oversized states fall back to defaults; hidden panels are recovered,
  floating panels without a reachable title on an available screen are redocked;
- Settings → Cards and panels has a separate diagnostic reset that retains
  reports and leaves Overview alone. Full Exit still redocks floats before saving;
- 41 test targets, production restart/reset checks and native 100%/150% coverage.
  See `docs/STAGE47.md` and `docs/FINAL-CHECKLIST.md`. Migration is not complete.

## Stage 46 checkpoint

- Floating Overview cards and diagnostic panels no longer minimize with the
  Windows main window: native/transient ownership is detached, Qt docking and
  lifetime ownership are retained;
- floating telemetry remains live during Gamer Mode; global Pause, return/reset
  and full application Exit remain respected;
- native HWND regression and production UI checks cover minimize/restore,
  hide, updates, pause, redock and shutdown. 40 test targets; `docs/STAGE46.md`.
  Remaining release work: `docs/FINAL-CHECKLIST.md`. Migration is not complete.

## Stage 45 checkpoint

- Deep Telemetry freezes JSON at click time, adds a missing suffix, confirms the
  final overwrite target with No as default and verifies atomic full writes;
- the application-log viewer reads a bounded tail in the background, labels
  omitted/shortened text and decoding issues, and leaves the source file intact;
- tests cover cancel/overwrite, nested refresh, UTF-8, large files and close;
  39 test targets. See `docs/STAGE45.md`. Full migration remains incomplete.

## Stage 44 checkpoint

- Incident samples retain producer metric timestamps, source and quality; source
  age plus UI queue delay must be <=3 seconds and after the pause boundary;
- replayed/out-of-order frames and repeated network intervals cannot inflate
  coverage or sums; stale ping/process context is excluded;
- memory, application contribution and CPU thermal hints require same-row
  evidence with <=1 second source-time spread, not unrelated aggregate peaks;
- card/TXT/JSON expose rejected data and matched observations, with qualified
  causal wording. Legacy reports retain their prior path. 38 test targets;
  see `docs/STAGE44.md`. Full migration is not complete.

## Stage 43 checkpoint

- Incident capture uses an absolute steady-clock marker deadline; slow log reads
  consume the post interval, cancellation skips subsequent collection;
- sorted, bounded telemetry rejects malformed/conflicting timestamps, collapses
  exact duplicates and exposes missing window edges as incomplete coverage;
- unknown network counters remain null, partial sums are labelled lower bounds;
  event timestamps constrain correlation to the intended window;
- completed collection is not a healthy-PC indicator; gaps/counter coverage are
  visible in the incident card and report. 37 automated targets; `docs/STAGE43.md`.
  Per-metric freshness and same-observation incident causality remain open.

## Stage 42 checkpoint

- Incident/app-context extrema survive percentile sorting; known-value >=90
  fractions and strict non-overlapping incident boundaries match the source;
- Deep Telemetry distinguishes unknown cores from zero and labels aggregate
  frequency; log notes/quality/source/time survive display and JSON export;
- queued logs cannot undo Clear, global pause defers results, and closing the
  window no longer blocks on log collection or deletes a running worker;
- minimum-width native screenshots checked at 100%/150%; 36 automated targets
  with expanded coverage. See `docs/STAGE42.md`; migration remains incomplete.

## Stage 41 checkpoint

- Pause duration is recorded at each actual transition, including rapid toggles;
  observation time freezes before closure/report work and resets for a new run;
- transient pause/resume during autoclose grace cancels further forced closure;
- app reports restore known-value memory-growth fractions, constant-series R²
  and symbolic Windows exit codes, with explicit unknowns and interpretation limits;
- 36 automated targets; see `docs/STAGE41.md`. Full migration is not complete.

## Stage 40 checkpoint

- App-monitor system data retains original metric timestamps/quality and is
  rejected when stale, queued too long, or captured before the required boundary;
- process-tree I/O and fault rates use adjacent identified per-process deltas;
  initial lifetime activity, pause activity, gaps and recycled PIDs create no spikes;
- observed totals are explicitly lower bounds; missing endpoints/intervals are
  visible in live details, report text and diagnostic coverage;
- new reports use same-observation evidence for memory/hang/fault/I/O coincidences,
  not unrelated session maxima. 35 automated targets; see `docs/STAGE40.md`.
  Full migration, including Linux identity/runtime and auxiliary-window audit, remains open.

## Stage 39 checkpoint

- Problem Application restores bounded CPU and working-set/private-memory
  history, real elapsed-time axes, hover crosshair, pause/missing-data gaps;
- a verdict card explains reasons, key memory/handle statistics, next actions
  and incomplete coverage; the full report opens in a dedicated window;
- canonical TXT now includes detailed trends, peaks, hangs, process evidence,
  events and finding details; JSON export omits the duplicated report text;
- 34 automated test targets, including bounded chart and report/UI checks;
  see `docs/STAGE39.md`. The overall migration is still incomplete.

## Stage 38 checkpoint

- Problem Application pause no longer consumes session time; Stop/Pause during
  timeout grace cancels further forced closure, with explicit partial outcomes;
- Windows process sessions retain handles and verify creation/parent identity,
  preserving early exit codes and preventing stale-PID action targets;
- missing process metrics/window response remain unavailable; live thread,
  handle, page-fault, commit and system-GPU fields plus outcome-aware TXT restored;
  see `docs/STAGE38.md`. Graphs/verdict-card and full detailed-report parity remain.

## Stage 37 checkpoint

- source-led Task Manager/Autostart comparison restores confirmed Windows
  process termination, bound to PID plus creation time with critical/self guards;
- refresh preserves numeric header sorting and stable selection in both tables;
  removed/replaced rows clear selection, and TOP-15 search covers all processes;
- compact rows and wrapped metadata retain narrow-window usability; Autostart
  remains read-only. Native action tests target only their own fixture child;
  see `docs/STAGE37.md`. Linux termination remains unimplemented;

## Earlier checkpoints

- cancelled and failed Deep/Full scans cannot reuse cached temperatures,
  current metrics, hardware or autostart evidence as a fresh result;
- missing post-load collection is explicit, with before-only memory labelled
  accordingly; primary CPU/GPU channels remain owned by the platform backend;
- sensor groups retain independent quality, timestamps and provider reasons;
  stale readings are labelled and do not create current temperature findings;
  see `docs/STAGE36.md`;
- Stage 35 added fresh endpoint sensors and shared readable hardware, memory,
  autostart and tray sections to both Deep and Full reports (`docs/STAGE35.md`);

- the source-led separate Full Scan is now available beside Deep Diagnostics:
  it performs a fresh deep-local sequence, reads Windows Security Center
  antivirus registrations, runs the manual 10 MB download/5 MB upload estimate,
  resolves public IP/provider and adds PC plus Internet classifications;
- a default-No warning names every CPU/GPU/disk and Internet operation before
  start; Stop, close and global Pause propagate cancellation, and conflicting
  collectors cannot overlap the combined scan;
- structured output records complete/cancelled/failed/partial state, exact
  transfer scope, antivirus quality, fresh online results and both ratings;
  preview/copy/TXT use the Full Scan formatter while JSON keeps the full schema;
- deterministic Full Scan and UI tests replace all live load, PowerShell and
  network boundaries; the labelled fixture screenshot and the full 32-test run
  did not start real stress or external requests; see `docs/STAGE34.md`;

- the original passive Diagnostics presentation is restored above the native
  findings table: a runtime-incident card, live CPU/GPU temperature cards,
  one status-striped SMART card per device, a bounded scrollable OS-error card
  and the cached public-IP/provider/location card;
- temperature cards update with the normal telemetry cadence, preserve honest
  unavailable state and show source-matched warning/critical thresholds plus
  the duration of the current level; no extra hardware polling is introduced;
- SMART/log cards reuse startup or manual passive results, preserve data quality,
  installation hints, grouped duplicate events and NVMe health/wear/media fields;
  the in-panel incident action reuses the existing 60-second-before/15-second-after
  capture, and the IP card reuses the existing explicit online lookup;
- deterministic UI integration injects SMART/log/IP fixtures through the real
  worker signal connections and checks card fields/status without accessing the
  Internet or starting hardware load; see `docs/STAGE33.md`;

- a source-led native Deep Diagnostics action: fresh local hardware, SMART,
  system logs, autostart and memory snapshots, followed by sequential CPU/GPU/
  disk checks only after a default-No warning; no Internet tools in this scan;
- original-style compact progress/log dialog with a visible top-findings banner,
  cooperative Stop/close, and copy/export of complete or explicitly partial
  reports; deterministic tests never execute its native load boundary;
  see `docs/STAGE32.md` for limits and remaining Python parity;

- original Network current-state card with cumulative receive/send GiB and
  interval/total errors and discards; integer counters retain precision, missing
  data remains explicit, and pause/reset/interface-set changes rebaseline deltas;
- Network's original headings, local splitter reset, scan width priority and
  adapter-row controls restored; global traffic is clearly separate from the
  selected scanner adapter; see `docs/STAGE31.md`;

- source-led correction of Overview/Details after comparing the original Python
  widgets: original compact headings, theme fonts/colors and line-only sparklines;
- original 5:4 two-column Details composition: CPU/GPU on the left, OS/RAM and
  mounted-volume cards on the right, with session peaks, VRAM, frequency and
  cumulative disk I/O fields; stable live cards and explicit unavailable values;
- CPU history now continues in Gamer Mode; narrow history controls wrap, extra
  native sensors remain accessible in a collapsible card, and the Overview disk
  type badge opens Details;
- five existing themes retained, including Quantum gauges, Slate's hidden
  graphs and custom light/dark surface blending; see `docs/STAGE30.md`;

- pure C++ telemetry model with explicit value/time/quality/source/reason,
  thresholds, rolling windows and session peaks;
- real Windows telemetry through WinAPI, per-logical-processor system times and
  IP Helper;
- real Linux telemetry through `/proc` and `/etc/os-release`;
- a compact native startup dialog with real collector progress, a bounded
  expandable log and Skip; hardware, autostart, SMART/system logs and public IP
  are prepared once, then reused by the main pages;
- first-telemetry gating, warning-aware completion and a 15-second reveal
  fallback; skipping reveals immediately while collection continues, whereas
  closing startup exits even when tray mode is enabled;
- embedded native local-network scanner with explicit IPv4 adapter selection,
  local IP/MAC/subnet context, bounded parallel ICMP/TCP probing, cancellation,
  progress and a five-column live device table;
- explainable advanced device identity through source-bound SSDP/UPnP,
  NetBIOS, mDNS, system-DNS PTR, HTTP/HTTPS fingerprints and a bounded
  19-port service pass, capped at 64 confirmed targets;
- cached public IP, provider and location lookup on normal startup plus an
  explicit refresh action, with a bounded primary/fallback request chain and
  honest offline, timeout and malformed-response states;
- a manual-only, cancellable single-connection Internet speed estimate using
  three latency probes, a fixed 10 MiB download and a fixed 5 MiB upload; the
  result is session-cached and included in schema-v3 reports without silently
  running during quick or deep diagnostics;
- deterministic scan-scope safety: networks above 1022 usable hosts are limited
  to the selected computer's local `/24`, and an interrupted scan keeps the
  already confirmed devices instead of inventing completion;
- cancellable background TCP ping to the persisted user target, with explicit
  valid/unavailable quality and no blocking of the main telemetry cadence;
- a native Network traffic chart with separate receive/transmit lines,
  monotonic 60-second history, shared automatic KiB/s or MiB/s scale and
  nearest-sample crosshair; missing data and pauses leave explicit gaps;
- Network history continues across hidden tabs, Slate Minimal and Gamer Mode;
  Slate hides the chart, Quantum applies accented lines/grid, and global Pause
  freezes both collection and the historical view;
- native `orion-probe` executable for backend verification;
- existing native network scanner integrated into the new build;
- Qt Widgets overview with live CPU/RAM/network cards;
- the original main-window navigation shell with toolbar actions, eight
  canonical tabs and hot insertion/removal of the optional Servers and
  Terminal tabs;
- SQLite-backed server/database profiles plus real two-second TCP status
  checks that never invent an online state;
- a real embedded PowerShell/bash session with command history, ANSI cleanup,
  clear-screen handling and bounded process termination;
- native session-only Gamer Mode with a compact frameless always-on-top overlay,
  draggable positioning, Escape/close recovery and correct restoration of the
  main window;
- live CPU/GPU/RAM/network values, an independent bounded TCP ping producer and
  an explicitly labelled UI-render FPS counter that never claims game FPS;
- global-pause propagation, visibility-aware worker/timer lifecycle, alarm
  border pulse and per-metric critical highlighting in the overlay;
- five movable/floating overview cards with persisted layout and native
  sparkline history;
- native Windows/Linux mounted-volume collection with space, filesystem,
  SSD/HDD/NVMe classification and disk I/O rates;
- live storage overview, Details cards, full lazy Hardware inventory and telemetry JSON;
- native GPU inventory on Windows through DXGI and on Linux through sysfs;
- optional NVIDIA NVML telemetry loaded at runtime: GPU load, temperature,
  total/used VRAM and VRAM occupancy, without a hard vendor-library dependency;
- live GPU overview card, Details fields, Hardware inventory and quality-aware
  telemetry JSON;
- native temperature/fan inventory shared by telemetry, diagnostics and UI;
- Windows LibreHardwareMonitor/OpenHardwareMonitor WMI integration, ACPI
  thermal-zone inventory and NVIDIA fan percentage through NVML;
- Linux `/sys/class/hwmon` temperatures, limits and fan RPM channels;
- CPU temperature remains explicitly unavailable when Windows exposes no
  trustworthy CPU package channel; generic ACPI zones are never relabelled;
- full scrollable Hardware specification with native motherboard/BIOS and RAM
  module discovery, CPU sockets, physical disks, Qt display snapshots, battery,
  adapters, conservative NPU detection, clipboard export and PC rating;
- hardware collection runs once on first opening in its own cancellable thread;
  a manual refresh repeats the 15 isolated steps and global Pause stops it;
- Windows Hardware associates volumes with actual physical-disk numbers through
  native volume extents, preserving spanned and unmapped volumes; storage bus,
  SSD/HDD type, serial, firmware and GPT/MBR metadata are read without writes;
- the Hardware GPU list enumerates all DXGI hardware adapters by distinct LUID,
  with dedicated VRAM separate from shared system RAM, and adapter link speeds
  use interface-index-matched receive/transmit rates rather than live throughput;
- collapsible live sensor/fan fields on Details plus complete channels on Hardware;
- native Windows process inventory through Tool Help, process times, PSAPI and
  process I/O counters;
- working problematic-application monitor that launches a selected EXE and
  follows its complete descendant tree with CPU, working set, private memory,
  handles, page faults, I/O, thread/process counts and hung-window checks;
- native Windows commit used/limit/peak counters through `GetPerformanceInfo`
  and pagefile occupancy through `EnumPageFilesW`, kept as separate concepts;
- interval hard-fault disk-read and paging-family rates through native system
  performance deltas, with the Python rule that hard faults without low RAM or
  high commit are unconfirmed rather than false pagefile pressure;
- system context-switch rates plus per-process rates aggregated from native
  thread performance counters for the monitored process tree;
- per-volume disk busy percentage and read/write latency from
  `IOCTL_DISK_PERFORMANCE`, propagated into live cards and long-session reports;
- original duration-aware 1/2/5 second sampling, pause/rebaseline behavior,
  manual observation stop that leaves the app running, and bounded WM_CLOSE /
  seven-second grace / tree-only forced termination at an enabled timeout;
- live app-monitor screen plus schema-v3 JSON/TXT report, trends, peak moments,
  Event Log before/after diff, findings, verdict, coverage and action plan;
- five-minute runtime ring and real `⚡ Проблема сейчас` incident capture with
  60 seconds before, 15 seconds after, baseline/focus/recovery windows and
  valid/estimated/stale quality;
- timestamp-level system-log windows around user markers and app observation,
  including exact UTC/offset parsing, closest-event offset and explicit
  coverage when legacy lines have no parseable timestamp;
- native Linux process inventory through `/proc/*/{stat,status,io}`;
- lazy process worker that runs only while Task Manager is visible and obeys
  the global monitoring pause;
- working Task Manager with TOP-15 CPU view, full-list mode, search by name or
  PID, sortable numeric columns, RAM/private memory, cumulative disk I/O and
  thread counts;
- read-only Windows autostart inventory from HKCU/HKLM Run and RunOnce keys,
  the current-user Startup folder and the common Startup folder;
- read-only Linux autostart inventory from user/system XDG `.desktop` files
  and `systemd --user` service states;
- lazy Autostart scan, explicit user/system categorization, enabled/disabled/
  unknown status, live totals, search and category filtering;
- restored three-panel Diagnostics hub matching the Python layout:
  `Диагностика`, `Стресс-тест` and `Отчёт` movable/floating docks;
- background native diagnostic snapshot analysis through the shared report-v3
  engine, with current RAM/temperature/disk-space and autostart rules;
- native Windows Event Log collection through `wevtapi`, limited to Critical
  and Error events from the current boot in the System and Application logs;
- optional SMART collection through `smartctl --scan -j` / `-a -j`, with ATA
  sector counters, NVMe health/wear/media fields, drive-aware temperature
  thresholds and explicit unavailable/partial quality instead of invented OK;
- system-event normalization, duplicate grouping and Python-compatible
  categories for WHEA, display driver, storage, memory exhaustion and hangs;
- findings table with severity, domain, confidence, evidence and suggested
  actions; missing SMART, system-log and temperature data remains UNKNOWN;
- complete Python-compatible report snapshot with SMART/log/temperature,
  stress, runtime memory/paging, incident, app-monitor, risk, coverage,
  verdict, action-plan and full-finding sections;
- the same wrapped plain-text report in the UI preview, clipboard and atomic
  UTF-8 TXT export, plus the unchanged schema-v3 JSON export;
- working native CPU stress on every logical processor with live telemetry and
  a two-sample 100°C safety stop;
- working Windows GPU stress through a Direct3D 11 compute shader, verified
  result readback and a two-sample 92°C safety stop;
- bounded disk write/flush/read/SHA-256 verification with automatic temporary
  file removal;
- explicit GPU/disk acknowledgements, final warning, phase progress, emergency
  stop, global-pause cancellation and result injection into report schema v3;
- before/after Event Log snapshots around a stress run with multiset diff, so
  repeated events that appeared during the run are retained in its result;
- diagnostic findings for thermal safety stop, unavailable GPU backend and
  failed GPU output verification;
- a scrollable CPU details page with registry frequency and one large native
  QPainter chart on a shared 0–100% scale: current-load profile, threshold-
  coloured histogram or rolling 60-second history paged by eight logical CPUs;
- per-core history records every telemetry tick even while Details is hidden,
  retains idle 0% cores, uses gaps for missing samples, stops exactly with the
  global pause and persists the selected chart mode;
- working non-modal Deep Telemetry window with separate honest frequency and
  load charts, per-logical-CPU frequency snapshots, continuous session peaks,
  local/global pause, background system-error refresh and session-log viewer;
- Deep Telemetry JSON export retains current per-core rows, sampling semantics,
  session uptime/peaks and the currently displayed Windows Event Log entries;
- the overview error badge now opens the same Deep Telemetry error view as the
  original Python UI, and repeated toolbar clicks raise the existing window;
- global telemetry pause/resume shared by the window and tray menu;
- tray lifecycle, high-load notifications and AppData-backed settings page;
- the four-part Python-compatible settings structure: Appearance, Window,
  Overview cards and Advanced;
- five persisted themes with hot application to the main window, settings
  dialog and sparkline graphs, including the editable four-colour Custom theme;
- behavioral Quantum Cyan presentation with animated 270-degree CPU, GPU and
  RAM gauges, honest unavailable state and theme-specific cyan/orange progress;
- behavioral Slate Minimal presentation that removes decorative gauges,
  sparklines and toolbar icons, groups each card's live value with its detail,
  and applies compact square-edged controls without hiding telemetry;
- opacity, minimum/maximum geometry, always-on-top and unconstrained resize
  controls with immediate application;
- hot Overview-card visibility and movable/floating feature controls plus an
  in-session reset to the canonical dock layout;
- independent tray/notification controls and the original five-second
  post-alarm banner hold option;
- language-neutral diagnostic finding schema, causal merge, coverage,
  risk summary with measured positive evidence and action-plan ranking;
- report schema v3 and versioned telemetry JSON;
- AppData paths, legacy settings migration, atomic JSON settings and UTF-8 logs;
- a native rule runner that passes the same 17 JSON golden cases as Python and
  also checks required/forbidden user-report fragments;
- thirty-two CTest targets covering core, platform, diagnostic, report, golden,
  storage, telemetry pause/resume, process-worker lifecycle and the restored
  main-window UI contract, autostart transfer, background diagnostic report
  and a disk-only 1 MiB stress-worker contract that never starts CPU/GPU load,
  plus deterministic SMART/Event XML parsers, runtime-window/trend contracts,
  a read-only live log contract and a real app-monitor lifecycle fixture.
  The dedicated Deep Telemetry test verifies per-core JSON, session peaks and
  both local and global pause behavior without reading the live OS journal.
  The dedicated ping-worker test uses a local TCP fixture and verifies valid
  latency, honest unavailable state, pause/resume and bounded shutdown. Server
  profile/status and terminal lifecycle tests cover the optional tabs. The
  dedicated Gamer Overlay test covers flags, telemetry, UI FPS, pause, alarm
  styling, independent ping lifecycle and bounded close recovery. The dedicated
  gauge test covers unavailable state, clamping and the 400 ms animated value;
  the UI contract verifies exactly three Quantum gauges and five graph-free
  dense Slate cards after a live theme switch. The network-scanner test covers
  subnet planning, `/24` truncation, vendor classification, deterministic live
  results and bounded cancellation without probing the test machine's LAN. The
  Internet-tools test covers both provider response formats, ASN normalization,
  download/upload calculations, ping sanity, injected worker results and
  cancellation without making any external request. The dedicated CPU-chart
  test covers all three modes, idle-core retention, missing-sample gaps,
  eight-core paging and the fixed 60-sample bound; the main-window contract
  verifies hidden-tab collection and immediate mode persistence. The Hardware
  inventory test fixes SMBIOS memory mappings, firmware-date normalization,
  conservative NPU matching and all four Python-compatible rating outcomes,
  physical-disk identity/grouping, spanned/unmapped volumes and link-rate units.
  The Network chart test covers timestamp ordering, a real 60-second window,
  memory bounds, independent missing directions, gap geometry, hover values,
  binary units and pause/resume. UI integration checks collection in hidden
  tabs/Gamer Mode, live theme behavior and the global pause lifecycle.
  Startup tests cover monotonic completed-work progress, phase ordering,
  warning/offline completion, pause, cancellation, duplicate suppression,
  skip, deadline, close/Escape and compact/expanded bounded-log presentation.
  Details tests cover original column membership, restored fields/peaks,
  status/theme changes, stable volume cards and explicit missing readings;
  UI integration also verifies disk-badge navigation and Gamer Mode CPU history.
  Deep-scan fixtures cover confirmation, stage ordering, monotonic progress,
  cancellation, safety stop, collector failure, visible verdicts and retained
  partial reports without CPU/GPU load or Internet requests. Full-scan fixtures
  additionally cover the combined scope, antivirus state decoding, both ratings,
  fresh Internet results, partial/cancelled paths and the default-No boundary.

This is a tested migration foundation plus the first production UI slice, not a
claim that all O.R.I.O.N. features have already moved to C++. The current
boundary and the audited remaining work are listed in
[`docs/STAGE21.md`](docs/STAGE21.md).

## Configure and build

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.x/mingw_64
cmake --build build
ctest --test-dir build --output-on-failure
```

Without a Qt SDK, CMake deliberately builds the portable core, platform tests,
telemetry probe and network scanner. Qt enables the GUI, JSON diagnostics,
settings and their additional tests. Presets are also available:

```powershell
cmake --preset native-debug -DCMAKE_PREFIX_PATH=C:/Qt/6.x/mingw_64
cmake --build --preset native-debug
ctest --preset native-debug
```

The main executables are `ORION`, `orion-probe`, `orion-golden-contract` and
`orion_netscan`.
