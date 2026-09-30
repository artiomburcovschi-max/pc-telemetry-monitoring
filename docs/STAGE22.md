# Stage 22 — explainable native network identity

Stage 22 continues the Python/PySide6 to C++20/Qt 6 Widgets migration. It is a
tested checkpoint, not a claim of complete feature or visual parity.

## Advanced identity pipeline

The embedded scanner now follows discovery with the same bounded identity
signals used by the Python reference:

- two source-bound SSDP M-SEARCH requests collect advertisements only from the
  selected IPv4 subnet;
- an advertised UPnP description is accepted only when its HTTP/HTTPS URL host
  is exactly the device being identified, preventing the response from turning
  the scanner into an arbitrary URL fetcher;
- short source-bound NetBIOS node-status and direct unicast mDNS PTR queries
  provide local names without shell commands;
- reverse DNS uses a bounded UDP PTR query to the operating system's configured
  DNS server rather than an unbounded resolver call;
- a 19-port second pass records common SSH, DNS, web, SMB, RTSP, IPP, MQTT,
  RDP, NAS, Cast, JetDirect and Plex services;
- HTTP and HTTPS fingerprints use a capped response size and extract only the
  Server header and a non-generic page title.

Names preserve their source and use the Python priority order: local/UPnP,
NetBIOS, mDNS, reverse DNS, HTTP title, UPnP model and finally an honest unknown.
Device types combine metadata, services and the offline MAC vendor result. The
UI keeps one IP-keyed row and enriches it live instead of creating duplicates.
Its tooltip now includes confidence, name source, services and evidence.

## Runtime and safety boundaries

Discovery still limits large interface scopes to the selected local `/24` and
runs at most 32 reachability probes concurrently. Advanced identification is
strictly capped at 64 remote devices and 16 simultaneous identity jobs. Every
socket operation has a short timeout, response bodies are capped, cancellation
is checked between operations, and no external scanner or resolver process is
launched. SSDP responders can establish liveness even when ICMP/TCP discovery
is filtered, but responses from outside the chosen subnet are ignored.

The local adapter's default gateway is now read through the native Windows IP
Helper API or Linux route table, allowing high-confidence router classification.
Locally administered MAC addresses are labelled as private/randomized rather
than assigned a guessed manufacturer.

## Verification boundary

The Release build and all 23 CTest targets pass. The new deterministic identity
test covers case-insensitive SSDP headers, compressed DNS PTR records, NetBIOS
node status, HTTP titles/Server headers, namespace-qualified UPnP XML, service
labels, name precedence, classification confidence and the fixed 64-target
limit. Existing scanner tests still inject their probes, so automated checks do
not scan the verification machine's LAN. The full main-window contract also
passes unchanged.

A real Windows-rendered Network screenshot was inspected. Adapter context,
scan controls and all five columns remain readable without clipping. The scan
button was not activated during visual verification.

## Remaining parity work

The next highest-value Network slice from the Stage 21 audit is cached public
IP/ISP information and an explicit speed-test flow. CPU-history presentation,
the full Hardware inventory and the startup splash remain later audited slices.
