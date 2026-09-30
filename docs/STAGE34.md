# Stage 34 — confirmed native Full Scan

Stage 34 ports the separate `core/full_scan.py` workflow instead of extending
quick Diagnostics or quietly reusing cached Internet results. Full Scan is a
manual-only orchestration: a fresh local Deep Diagnostics pass, read-only
Windows antivirus inventory, an Internet speed estimate, public IP/provider
lookup, and explicit PC/Internet classifications.

## Consent and execution boundary

The new two-column action sits beside Deep Diagnostics. A default-No and
Escape-No warning lists the actual work before anything starts: CPU and GPU for
30 seconds each, a verified 200 MiB temporary disk file, a fixed 10 MB download,
a fixed 5 MB upload, public-IP providers and read-only Windows Security Center
access. Worker state is checked both before and after the modal confirmation.
Full Scan rejects concurrent hardware, diagnostic, stress, Internet, Deep Scan
or application-observation work; those entry points also reject a running Full
Scan. Global Pause and dialog Stop request cooperative cancellation.

The full worker composes the already-tested native DeepScanWorker and
InternetToolsWorker. It does not maintain a second stress implementation or a
different network measurement. Deep work remains sequential CPU, GPU and disk;
the online phases start only after it returns. Cancellation is checked between
phases and is forwarded while waiting for a nested deep or network worker.
Synchronous platform/provider calls still have to return before their phase can
finish, so Stop is not advertised as instantaneous.

Every expensive or online phase crosses one injectable step boundary. The
dedicated tests replace that complete boundary; an absent fixture cannot fall
through to real load, PowerShell or Internet activity.

## Antivirus and classifications

On Windows the antivirus step reads `AntiVirusProduct` from
`root\\SecurityCenter2`, the same registry used by Windows Security Center. It
does not enumerate process names or change protection settings. The known
community `productState` bytes are decoded into real-time protection and update
states; unfamiliar bytes remain null/`н/д`, never a guessed false value.
Malformed output, unavailable PowerShell and provider errors remain explicit
collector errors. Other platforms report this source as unsupported.

The PC rating reuses the existing source-compatible Hardware rating: physical
cores, RAM and discrete-GPU evidence produce `игровой`, `обычный`, `слабый` or
`недостаточно данных`, with name-only GPU confidence disclosed. Internet uses
the original 50/10/40 and 15/3/100 download/upload/ping boundaries for
`хороший`, `средний` and `слабый`; an absent speed result is `н/д`. Unknown ping
does not count as zero evidence and is called out in the explanation.

## Result and presentation

The separate 680 x 520 dialog contains status, monotonic progress, bounded log,
top findings, both classifications, Stop, Close and full-report copy. Closing a
running dialog first requests stop and defers destruction until the worker exits.
The main Diagnostics preview and TXT copy/export use the Full Scan formatter;
JSON retains antivirus, speed, public IP, classifications and scan metadata.
A later standalone speed test cannot silently mutate a completed full report.

`scan.kind=full` records fresh-result intent, combined Internet scope, exact
transfer sizes, deep-phase state and completed phases. Cancellation, collector
exceptions and a partial/safety-stopped deep phase stay visibly partial.
`complete` means every scheduled phase returned; individual unsupported or
offline sources still carry their own quality/error and are not claimed healthy.

## Verification boundary and remaining work

All 32 CTest targets pass. The Full Scan target checks default denial,
concurrent-start rejection, phase order, monotonic progress, fresh-result
replacement, antivirus decoding/unknown states, PC and Internet ratings, visible
report sections, cancellation before the next network phase, partial deep state
and collector exceptions. Main-window integration declines the real warning and
injects a synthetic partial result through the actual worker signal.

The inspected dialog screenshot is labelled TEST DATA. Verification did not run
CPU/GPU stress, the 200 MiB disk test, antivirus PowerShell, external HTTP, speed
test, public-IP lookup or LAN scan.

Full Scan follows the original independent workflow and therefore does not
silently launch Problem Application observation. A completed observation still
remains available through its dedicated tab and normal report snapshots. Deep
Diagnostics still needs final general-sensor refresh, tray-state text and exact
extended Python prose. Linux runtime, high-DPI/fractional scaling,
provider-hang cancellation and a manually confirmed real hardware-load/full-
scan validation remain unverified.

