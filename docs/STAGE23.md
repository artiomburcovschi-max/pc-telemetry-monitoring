# Stage 23 — public network identity and explicit speed test

Stage 23 continues the Python/PySide6 to C++20/Qt 6 Widgets migration. It is a
tested checkpoint, not a claim of complete feature or visual parity.

## Public IP and provider lookup

On a normal interactive startup O.R.I.O.N. performs one bounded lookup and
caches the result for the session. It first requests `https://ipinfo.io/json`
and falls back to `http://ip-api.com/json` only after a transport, timeout or
response error. Each response is limited to 256 KiB and each attempt to six
seconds. The parser retains public IP, provider, location, source and check
time; an ASN prefix is removed from the provider exactly as in the Python
reference. Offline, timeout and malformed responses remain explicit errors.

The Network page displays the cached identity, while Diagnostics provides a
manual refresh action. Automated smoke, screenshot and UI-contract modes do not
start an external lookup, so repeatable verification never depends on Internet
access or exposes the verification machine's public address.

## Manual speed estimate

The speed action is deliberately manual-only and cancellable. It uses the
official Cloudflare speed-test download and upload endpoints with three small
latency probes, a fixed 10 MiB download and a fixed 5 MiB upload. Both transfer
phases use one connection, bounded response sizes and 30-second deadlines. The
UI therefore labels the numbers as an estimate rather than presenting them as
an exhaustive line-capacity benchmark.

The session-cached result contains download/upload Mbit/s, median ping, method,
timestamp and an explicit error when incomplete. It is shown on Network and is
copied into the existing schema-v3 diagnostic snapshot and TXT report. Quick
and deep diagnostics never launch the speed test themselves. Global pause or
application shutdown requests cancellation and waits for the worker to stop.

## Verification boundary

The Release build completes all 178 build steps and all 24 CTest targets pass.
The new deterministic worker test covers both public-IP response formats, ASN
normalization, fallback failure, transfer calculations, ping sanity, injected
public/speed results and cancellation. It performs no external requests. The
full main-window contract verifies both Network summaries and all Diagnostics
controls, including the documented fixed transfer sizes.

## Remaining parity work

The next audited slices are the CPU-history presentation, the full Hardware
inventory and the startup splash. The richer network-traffic chart and its
inspection interaction also remain outside this checkpoint.
