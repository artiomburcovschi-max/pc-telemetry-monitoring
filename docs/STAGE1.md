# Stage 1 — native foundation and behavior contracts

Stage 1 establishes the parts of O.R.I.O.N. that later collectors and UI pages
must share. The Python/PySide6 project remains the functional reference; this
repository does not claim that the complete application has already been
ported.

The source archive intentionally uses a flat, human-readable project layout:

```text
core/          telemetry models and platform-neutral algorithms
ui/            Qt 6 Widgets application
platform/      Windows/Linux collectors
diagnostics/   findings, report v3 and golden runner
storage/       settings, application paths and logging
tools/         native probe and network scanner
tests/         native unit, contract, golden and smoke tests
```

Qt runtime plugin folders are deployment details and never appear in the
source tree.

## Delivered in this stage

- C++20/CMake module layout with optional Qt 6 Widgets GUI;
- isolated Windows and Linux telemetry backends;
- telemetry metrics that carry `value`, `observed_at`, `quality`, `source` and
  `reason` instead of turning missing data into zero;
- diagnostic statuses `pass`, `warning`, `fail`, `unknown`, `skipped`;
- data qualities `valid`, `stale`, `estimated`, `unsupported`,
  `permission_denied`, `collector_error`;
- finding creation, causal merge, verdict selection, coverage, risk summary and
  action-plan ordering;
- report envelope compatible with Python report schema version 3;
- a native rule slice that passes all 17 shared golden cases, including memory
  pressure, UNKNOWN sensor coverage, disk-type thresholds, SMART, WHEA,
  user-marked incidents and application crashes;
- user data paths through `QStandardPaths::AppDataLocation`, one-time legacy
  settings copy, atomic JSON settings and UTF-8 file logging;
- CTest coverage for core models, the real platform backend, diagnostic schema,
  report/telemetry JSON, golden compatibility and storage.

## Compatibility boundary

The canonical golden fixture is `tests/fixtures/diagnostic_golden_cases.json`.
The native runner compares every expected field present in a fixture and also
rejects every `forbidden_ids` match. Passing therefore means actual rule output
compatibility, not merely successful JSON parsing.

The stable report keys are:

```text
schema_version, generated_at, diagnostics, stress_test, speed_test,
runtime, incident, app_monitor, findings, verdict_findings, coverage,
risk_assessment, action_plan
```

## Data locations

Qt chooses the normal per-user application-data directory:

- Windows: `%APPDATA%/ORION/ORION`
- Linux: `~/.local/share/ORION/ORION`

`user_settings.json`, `logs/orion.log` and `reports/` live below that root.
For tests and portable launches only, `ORION_DATA_DIR` can override the root.
No normal application setting is written beside the executable.

## Intentionally deferred

Stage 1 is the foundation, not the complete migration. GPU/SMART/sensor/disk
collectors, all production UI pages, tray and pause behavior, stress workers,
long application monitoring and full text-report rendering remain later stages.
The current GUI is a telemetry smoke surface used to verify the architecture.
