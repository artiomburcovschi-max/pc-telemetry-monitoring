# Stage 25 — full native Hardware inventory

Stage 25 continues the Python/PySide6 to C++20/Qt 6 Widgets migration. It is a
tested checkpoint, not a claim of complete feature or visual parity.

## Full specification screen

The former live Hardware summary has been replaced with the Python screen's
scrollable specification: PC rating, operating system, motherboard, BIOS/UEFI,
CPU, RAM modules, GPU, NPU, physical disks and mounted volumes, displays, fans,
battery, network adapters and temperature channels. A single action copies the
complete rendered specification to the clipboard.

The report is collected once on first opening rather than recomputed on every
telemetry tick. Manual Refresh starts a new pass, duplicate clicks are ignored,
and a visible 15-step progress indicator names the current collector. Collection
runs outside the UI thread. Closing the app or activating global Pause requests
a stop; Resume restarts an unfinished pass. The first pass waits for the initial
telemetry sample so GPU, volumes and sensors are not frozen into an empty seed.
WMI enumeration has a five-second budget per query. A provider stuck inside
COM can still delay cancellation; a worker surviving the five-second close wait
is detached and retained until it exits, avoiding destruction of a running thread.

## Native and honest data sources

Windows uses COM/WMI directly from C++ for `Win32_BaseBoard`, `Win32_BIOS`,
`Win32_Processor`, `Win32_PhysicalMemory`, `Win32_DiskDrive` and the PnP device
inventory. It does not launch PowerShell. Battery state comes from
`GetSystemPowerStatus`; Qt supplies GUI-thread display and adapter snapshots.
Linux uses DMI/sysfs, CPU topology, power-supply and accelerator class data.

Missing values remain `н/д`. OEM placeholder strings are filtered. A generic
accelerator or NVIDIA GPU is never presented as an NPU; only explicit neural,
NPU, Intel AI Boost/VPU, Movidius, AMD XDNA or Qualcomm Hexagon identities are
accepted. NPU load testing remains disabled without a verified execution
provider. Qt refresh rate is visibly marked approximate, and generic ACPI
thermal zones are not renamed to CPU Package.

The PC class follows the Python thresholds: at least 16 GB RAM, six physical
cores and a qualifying discrete GPU for `игровой`; less than 8 GB RAM or no
more than two physical cores for `слабый`; otherwise `обычный`. Missing CPU/RAM
facts produce `недостаточно данных`. Known VRAM uses the 4000 MB threshold;
name-only GPU classification adds an explicit confidence warning.

## Verification boundary

The Release build completes all 192 steps and all 26 CTest targets pass. The
new deterministic test covers SMBIOS DDR/DDR2/DDR3/DDR4/DDR5 mapping, WMI and
ISO firmware dates, accepted and rejected NPU names, known-VRAM and name-only
GPU rating paths, weak hardware and integrated balanced hardware. The expanded
main-window contract verifies the initial lazy state, controls, 15-step progress
range, first-sample ordering, all 14 rendered cards and cached reopening. WMI
timeouts/failures mark the report partial, and a failed NPU query is not reported
as confirmed absence. Unknown battery power state remains unavailable.

A real Windows render completed the hardware pass and showed the machine's
actual ASUS motherboard, American Megatrends firmware, 14-core/20-thread Intel
CPU and LGA1700 socket. The 980×820 top viewport is readable and the remaining
sections are reachable through the page scroll bar.

## Remaining parity work

Hardware still needs physical-disk-to-volume mapping, all-GPU enumeration
(the current seed contains the telemetry backend's primary GPU), adapter link
speed and Linux parity for RAM modules, NPU driver identities and exact monitor
refresh rates. The Linux branch has not been built or exercised here. Physical
disks and volumes are shown separately; Windows Storage health is not SMART.
The startup splash/pre-scan, richer Network traffic history/crosshair and final
whole-application visual/lifecycle review also remain.
