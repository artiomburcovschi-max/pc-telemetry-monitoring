# Stage 26 — disk identity, GPU inventory and adapter link rates

This checkpoint closes three Windows Hardware gaps from Stage 25. It does not
claim complete Python or visual parity. The Python reference remains available.

## Physical disks and volumes

The native read-only collector enriches WMI disks with storage descriptors,
seek-penalty classification and drive-layout metadata. Volume GUID handles and
`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` associate each mounted volume with its
actual physical-disk number. Matching never depends on array order, identical
model names or guessed drive letters. A spanned volume appears under each of
its backing disks. Missing disk identity has an explicit placeholder; failed
mapping remains in an unmapped section and marks the collection partial.

The screen shows bus type, SSD/HDD, serial, firmware, GPT/MBR, partition number,
Windows-volume role and space. Windows disk status is labelled as not SMART.
Handles request no data-write access, buffer growth is capped at 1 MiB, and
returned sizes/counts are checked before parsing. Cancellation is checked
between devices; synchronous driver calls cannot be forcibly interrupted.

## GPUs and network adapters

Windows enumerates DXGI hardware adapters, skips software renderers and retains
distinct LUID identities even when model names match. Dedicated VRAM uses the
64-bit descriptor field and is shown separately from shared system RAM. The
source is visible: DXGI inventory sizes need not equal NVML telemetry sizes.
A failed enumeration retains the available telemetry seed and marks it partial.
This machine has one GPU; real multi-GPU hardware was not available for testing.

Adapter snapshots now include interface indices. `GetIfEntry2` supplies matching
operational state, MTU and independent receive/transmit link speeds. Decimal
Mbps is distinct from traffic throughput; zero/unknown sentinel values and
disconnected links remain unavailable. Linux has a guarded sysfs link-speed
reader, but the Linux branch has not been built or exercised here.

## Verification

The active Windows Release build passes all 26 CTest targets. Added deterministic
contracts cover out-of-order disk numbers, identical model names, missing disk
indices, duplicate extents, spanned volumes, unmapped/failed mappings, unknown
physical identities, 2.5 Gbps conversion and unavailable link speeds. The UI
contract checks unique GPU identities, grouped disk data and both link-rate
fields alongside the existing lazy collection and refresh lifecycle checks.

A read-only real Windows pass completed with collection_partial=false: disk 0
is Samsung SSD 990 PRO 1TB with C:, disk 1 is Western Digital SN730E with D:,
both NVMe SSD/GPT; Ethernet reports 1000 Mbps in both directions. DXGI reports
the RTX 4060 Ti with dedicated and shared memory in separate fields. This full
WMI check ran outside the execution sandbox, whose WMI access is restricted.
No CPU/GPU stress, Internet speed test or LAN scan was run for this checkpoint.

The screenshot helper accepts `--screenshot-scroll 0..100` for inspecting lower
sections of the selected scrollable page. Final archive hashes, clean extracted
source tests and runtime checks are recorded in the output verification note.

## Next work

Network traffic history/crosshair and startup splash/hardware pre-scan precede
the whole-application design pass. Linux physical mapping, complete GPU and RAM
module inventory, NPU driver identities and exact display refresh-rate parity
remain separate work. Stage 25's bounded WMI enumeration and safe retention of
a worker stuck in a provider call remain unchanged.
