# Stage 5 checkpoint — native GPU telemetry

Stage 5 ports the first GPU slice used by the Python Overview, Details,
Hardware and telemetry snapshot paths. It builds on the native CPU, memory,
network and storage collectors already present in Stage 4.

## Cross-platform data contract

The shared telemetry model now carries:

- adapter name;
- optional GPU utilization and temperature;
- optional total and used video memory;
- optional video-memory occupancy;
- explicit source, quality and reason whenever a live value is unavailable.

Unsupported counters are never presented as zero. A machine may therefore
have a valid adapter identity and VRAM capacity while load or temperature is
reported as unavailable.

## Windows collector

- DXGI enumerates graphics adapters without external programs or scripts;
- the hardware adapter with the largest dedicated memory is the inventory
  fallback;
- NVIDIA NVML is discovered dynamically from the driver installation;
- when available, NVML supplies the adapter name, load, temperature and VRAM;
- the application has no link-time dependency on NVIDIA software and remains
  usable on AMD, Intel and systems without a vendor telemetry library.

## Linux collector

- NVIDIA NVML is loaded dynamically when installed;
- `/sys/class/drm` is the vendor-neutral inventory fallback;
- supported sysfs drivers may also expose busy percentage, VRAM and hwmon
  temperature;
- vendor IDs are converted to readable NVIDIA, AMD and Intel identities.

## UI and compatibility

- the Overview GPU card now has live percentage history plus temperature,
  VRAM and adapter identity;
- Details shows stable GPU name, load, temperature and VRAM fields;
- Hardware contains the detected video adapter summary;
- telemetry JSON retains the existing Python-compatible GPU metric names;
- contract and platform smoke tests validate ranges and VRAM consistency.

## Live verification

The Windows verification machine reported an NVIDIA GeForce RTX 4060 Ti with
live utilization, temperature and 8 GiB VRAM through NVML. All 8 CTest targets
and all 17 diagnostic golden cases continue to pass.

The next direct migration block is sensor and fan inventory, followed by the
remaining diagnostic, stress-test and problematic-application workflows.
