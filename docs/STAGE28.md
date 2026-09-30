# Stage 28 — real native startup preparation

The Python startup has been ported as a compact native dialog, not a separate
dashboard. It has a standard Windows progress control, Details, Skip and a
read-only 240-line log. Client size is 430 x 96 collapsed or 430 x 310 expanded.
The current step is available through the progress tooltip/accessibility text.
The native build keeps its existing asInvoker policy; it does not newly require
administrator rights just because the Python startup did.

## Preparation and honest progress

StartupSequence coordinates the existing workers: first telemetry, 15 Hardware
checkpoints, autostart, SMART/system-log diagnostics and public IP/provider.
UI initialization occupies the first 10 percent; completed collectors determine
the remaining progress. Hardware substeps stop short of claiming report delivery.
Duplicate/out-of-order results cannot advance another phase. Missing sources
produce warning completion, not fabricated health or invented measurements.

The main window owns workers and cached results. Startup does not create a
second hardware or IP collector. Opening pages reuses their reports; lazy
collection remains a fallback when the startup sequence is not enabled.
The diagnostic snapshot records whether autostart was already collected, so
an honestly empty list is not mistaken for a request to scan again.

## Skip, pause and close

Skip opens the main window immediately and leaves preparation running. A
single-shot 15-second wait limit does the same automatically, without filling
progress to 100. Global Pause suppresses new phases; Resume advances completed
phases or retries the waiting phase. The existing Hardware pause behavior is
retained. Regular page activation does not launch parallel startup collectors.

The startup close button and Escape explicitly quit, even if tray mode is
enabled. Normal completion/Skip dismisses the dialog without requesting exit.
Sequence cancellation prevents queued phases from starting during shutdown.
For startup's telemetry, IP, autostart and diagnostic workers, a provider call
surviving its shutdown wait is disconnected and retained until thread completion,
rather than destroying a running QThread. Hardware already had this safeguard.
Synchronous vendor/OS calls still cannot be forcibly interrupted; retention may
last until process exit if the GUI event loop has already stopped.

## Verification boundaries

A dedicated startup test covers phase ordering, real progress, warning and
offline completion, pause/resume, cancellation, duplicate prevention, bounded
log, Details, Skip, timeout, close and Escape. Main-window integration checks
reuse of prepared reports and idempotent startup. Native cold-start logs verify
first telemetry, all 15 hardware steps, autostart and SMART/log preparation;
skipped-wait logs show collection continuing after the window opens.

`--screenshot-startup <path>` captures a real preparation checkpoint;
`--startup-details` expands its log. `--no-splash` skips waiting but still runs
preparation. Existing main-window screenshots/smoke timing starts after reveal.
Screenshot/smoke modes intentionally skip public-IP requests; tests use loopback
ping settings. Normal startup reuses the bounded public-IP worker. No CPU/GPU
stress, Internet speed test or LAN scan is part of startup.

Final CTest/runtime results, archive hashes and native images are documented in
the output STAGE28-VERIFICATION.md note. Linux runtime was not exercised here.

## Next

Proceed to the main whole-application design pass. Do not add more technical
prerequisites before showing visual progress. Remaining counter/scan-presentation
and Linux parity work is tracked in STAGE21.md; this checkpoint does not claim
the entire migration is finished.
