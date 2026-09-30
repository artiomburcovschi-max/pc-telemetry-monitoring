# Stage 21 — native local-network scanner and parity audit

Stage 21 continues the Python/PySide6 to C++20/Qt 6 Widgets migration. It is a
tested checkpoint, not a claim of complete feature or visual parity.

## Audited Python/C++ boundary

The current Python navigation, settings panel, secondary windows and active
feature modules were compared with the native implementation before choosing
this slice. The resulting boundary is explicit:

| Area | Native state (updated through Stage 49) | Remaining parity work |
|---|---|---|
| Main navigation | Eight canonical and two optional tabs; full-name scrollable tab bar and clearer chrome | Cross-screen visual review remains |
| Overview/themes | Original typography/colors, metrics, disk navigation and sparklines; five themes/gauges/dense mode; Stage 46 independent floating HWNDs, minimize/hide/update/pause/reset/exit tests | Wider visual review, multi-monitor/DPI transitions and saved offscreen positions; see STAGE46.md |
| Details | Original 5:4 CPU/GPU vs OS/RAM/disk columns restored; session peaks, frequency/core count, VRAM, free space and cumulative I/O; stable volume cards, source-matched status thresholds and Gamer Mode CPU history | Pixel/high-DPI review, full end-to-end alarm lifecycle and remaining field audit across other screens |
| Networks | Original current-state card with cumulative receive/send and interval/total errors/discards; source-labelled system scope, gap/pause/reset rebaseline; splitter reset/scan sizing restored; adapter selection, scan, IP/ISP, identity, speed test and traffic chart retained | Broader scan-result/interactive/high-DPI visual review; Linux runtime and platform aggregation differences |
| Task Manager | Source-led Stage 37 adds confirmed Windows termination, creation-identity/critical/self guards, stable refresh sorting/selection, full-inventory search and narrow layout | Linux identity/termination implementation and runtime checks; wider theme/high-DPI review |
| Hardware | Startup-preloaded/cached specifications, motherboard/BIOS/RAM, copy/rating, Windows disk mapping, all-DXGI-GPU enumeration and link speeds work | Linux disk/multi-GPU/RAM/NPU inventory parity, exact display refresh rate and visual review |
| Autostart | Source-led Stage 37 confirms read-only behavior, restores refresh sort/selection, and verifies combined search/category filters and unknown state | Linux runtime and wider theme/high-DPI review |
| Diagnostics/stress/report | Native engine and schema-v3 reports; original passive incident/temperature/per-disk SMART/OS-log/public-IP cards; fresh local deep scan and confirmed Full Scan with antivirus, speed/IP and PC/Internet ratings; progress/log/verdict dialogs, Stop and explicit partial exports | Deep final sensor/tray/exact-text parity, optional cross-workflow application observation design and real confirmed hardware-load/full-scan validation; see STAGE32.md, STAGE33.md and STAGE34.md |
| Problem application | Stage 38 safe identity/closure lifecycle; Stage 39 charts/verdict/report; Stage 40 freshness and identity-bound deltas; Stage 41 transition-based pause accounting, transient closure cancellation, growth-fraction/R² conventions and Windows exit symbols | Polling gaps/final tails are explicitly unmeasured; broader summary/rule parity, long sessions, Linux identity/safe auto-close/runtime and broad visual checks remain; see STAGE41.md |
| Problem now / incident | Stage 43 deadlines/window coverage; Stage 44 freshness and same-observation evidence; Stage 49 reference CPU sample-fraction criterion, per-category new/recent log provenance and partial-log confidence | Backend timestamps remain the source of truth; indirect evidence and temporal links are not causal proof. Remaining application/general-log/stress rules and long sessions still need audit; see STAGE44.md and STAGE49.md |
| Deep Telemetry/Gamer Mode | Stage 42: data provenance and lifecycle; Stage 45: frozen atomic JSON and bounded app-log preview; Stage 48: bounded OS-log collection, partial-read warnings and exported limits/statistics | Broad theme parity; real Linux journalctl integration; Gamer Mode wider lifecycle/visual audit; see STAGE45.md and STAGE48.md |
| Settings/tray | Core settings persist; Stage 46 floats survive main hide/tray restore; Stage 47 wires diagnostic dock save/restore/reset, native/Python state and safe position fallback | Actual tray interaction and physical multi-monitor/DPI-transition review remain; full Exit redocks floats before saving |
| Startup | Compact native progress/log dialog; first telemetry, hardware, autostart, SMART/logs and IP; Skip/background continuation and 15-second reveal fallback work | Linux runtime verification and final native appearance review |

The next stages must use this table rather than assuming that overview parity
means whole-application parity. The earlier audit was primarily navigation and
feature-contract level; Stage 30's original-widget comparison found missing
fields and behavior despite passing tests. Rows saying "final visual comparison
only" describe the previous audited boundary, not proof that all original
callbacks, dialogs and alarm lifecycle paths are identical. Continue field and
interaction comparisons alongside visual work; see STAGE30.md for this slice.

## Embedded network scanner

The Network tab no longer stops at current throughput and ping. It lists active,
running non-loopback IPv4 adapters and lets the user choose the exact source
interface. The left panel shows the selected local IP, hardware MAC and effective
subnet. The right panel provides start/stop controls, progress, status and a live
five-column table: IP, MAC, name, type/vendor and latency.

Scanning runs in a dedicated native `QThread`; it does not launch the packaged
`orion_netscan` process. Windows probes bind ICMP to the selected source address
and fall back to short TCP probes when ICMP is blocked. Linux uses the same TCP
fallback and reads the kernel neighbor table for available MAC addresses. At
most 32 short probes run concurrently. Cancellation is atomic and is observed
inside every probe batch; global monitoring pause requests cancellation too.

The selected subnet is derived deterministically from IPv4 address/prefix. A
scope above 1022 usable hosts is reduced to the selected computer's local `/24`,
matching the Python safety boundary. The local computer is always represented
honestly. Remote devices appear only after ICMP response, TCP connection or TCP
refusal proves that an address is active. Missing MAC, name or vendor remains
`н/д`/unknown instead of being guessed.

This stage includes a small offline OUI map and evidence/confidence tooltips. The
Python scanner's deeper UPnP/SSDP, NetBIOS, mDNS, reverse-DNS, HTTP fingerprint
and service classification is deliberately recorded as remaining work.

## Verification boundary

The clean Release build completes 165/165 steps and all 22 CTest targets pass.
The new deterministic scanner test covers `/30` planning, safe `/16` to local
`/24` reduction, local/remote rows, MAC normalization/vendor lookup, progress,
completion and bounded cancellation through an injected probe. It never scans
the verification machine's LAN. The expanded main-window contract verifies the
adapter selector, scan control, progress and exact five table columns.

A real Windows-rendered Network screenshot was inspected at 830×820. The active
Ethernet adapter, local IP/MAC, effective `/24`, scan control and all columns are
readable without clipping. Visual and automated checks did not start a LAN scan
or CPU/GPU stress.
