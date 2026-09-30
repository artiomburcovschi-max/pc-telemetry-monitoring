# Stage 4 checkpoint — native storage telemetry

Stage 4 directly ports the storage slice used by the Python Overview, Details,
Hardware and telemetry snapshot paths.

## Native data model

Each mounted volume carries:

- name, mount point and filesystem;
- storage type (`NVMe SSD`, `SSD`, `HDD`, `USB` or `Unknown`);
- total, used and free bytes plus used percentage;
- optional current read/write bytes per second;
- optional cumulative read/write bytes.

Rates and totals are optional because an operating system may allow space
queries while denying performance counters. Missing I/O is never converted to
a zero-speed claim.

## Windows collector

- `GetLogicalDriveStringsW` and `GetDriveTypeW` enumerate mounted volumes;
- `GetDiskFreeSpaceExW` supplies capacity and occupancy;
- `GetVolumeInformationW` supplies filesystem and label;
- `IOCTL_STORAGE_QUERY_PROPERTY` detects NVMe/SSD/HDD without PowerShell;
- `IOCTL_DISK_PERFORMANCE` supplies cumulative read/write counters;
- rates are calculated from two snapshots over the normal telemetry interval.

## Linux collector

- `/proc/mounts` enumerates mounted block devices;
- `statvfs` supplies capacity and occupancy;
- `/sys/class/block/*/queue/rotational` identifies SSD/HDD;
- `/sys/class/block/*/stat` supplies sector counters for I/O rates.

## UI and contracts

- the Overview storage card now shows the main volume percentage, aggregate
  read/write rates, capacity and media type;
- free-space status follows the Python thresholds: warning below 15%, critical
  below 5%;
- Details shows one stable row per volume with type, filesystem, space and I/O;
- Hardware shows a concise inventory summary;
- telemetry JSON includes a quality-aware `disk_volumes` metric;
- the platform smoke test validates every returned volume.

## Live verification

The Windows verification machine returned two volumes (`C:` and `D:`), both
identified as NVMe SSD, with valid occupancy and native I/O counters. All 8
CTest targets and all 17 diagnostic golden cases continue to pass.

GPU is the next direct collector/UI block.
