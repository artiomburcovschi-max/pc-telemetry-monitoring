# Stage 6 checkpoint — native sensor and fan inventory

Stage 6 ports the unified temperature/fan inventory used by the Python
hardware report, telemetry and future stress-test workflows.

## Shared contract

Every temperature channel contains a component classification, label, current
value, optional high/critical limits, source and stable identifier. Every fan
channel contains RPM and/or percentage together with the same attribution
fields. The whole inventory retains metric quality, source and an explanation
when channels are unavailable.

Component attribution is deliberately conservative. A generic firmware or
ACPI thermal zone is a system sensor, not a CPU package temperature. Missing
CPU temperature therefore remains unknown rather than becoming a plausible
but incorrect number.

## Windows collector

- NVIDIA NVML supplies the primary GPU temperature and optional fan percent;
- LibreHardwareMonitor and OpenHardwareMonitor WMI namespaces are consumed
  when either monitor is already running;
- WMI temperature, fan RPM and fan-control channels are classified by their
  published identifiers;
- `MSAcpi_ThermalZoneTemperature` contributes generic system zones only;
- nothing is installed, downloaded or run automatically.

Standard Windows APIs do not expose desktop CPU/package and motherboard fan
telemetry. On such systems O.R.I.O.N. explains the missing capability. Running
LibreHardwareMonitor or OpenHardwareMonitor can publish the additional
channels without changing the application package.

## Linux collector

- `/sys/class/hwmon/hwmon*` is walked directly;
- `temp*_input`, labels, max and critical limits are retained;
- `fan*_input` supplies native tachometer RPM;
- well-known CPU, GPU, storage and system chips/labels are classified without
  assigning an arbitrary sensor to the CPU.

## UI and JSON

- the CPU Overview card now includes CPU temperature or an explicit `н/д`;
- the GPU card includes live fan percentage/RPM when available;
- Details contains the full temperature/fan inventory with component and
  source;
- Hardware reports temperature and fan channel counts;
- telemetry JSON includes `temperature_sensors` and `fan_sensors` with the
  Python-compatible inventory field names.

## Live verification

The Windows verification machine exposed one NVIDIA GPU temperature channel
and one GPU fan-percentage channel. The GPU was in its zero-RPM idle mode, so
the valid measured fan value was 0%. No trustworthy CPU package provider was
running; O.R.I.O.N. correctly reported CPU temperature as unavailable.

All 8 CTest targets and all 17 diagnostic golden cases continue to pass.

The next migration block will restore the native process/task-manager slice,
which is also required by problematic-application monitoring.
