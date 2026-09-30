# Stage 49 — incident CPU and event evidence

26 September 2026. Windows checkpoint, not a completed migration.

## Reference and scope

Compared Python core/insights.py incident CPU and new/recent-event branches with
the native engine, IncidentWorker, log diff/window helpers and runtime statistics.
This stage changes incident findings. It does not finish application-monitor,
general-log confidence, storage-causal or stress-session rules. Existing Stage
40/44 freshness, sample normalization and same-observation protections remain.

## CPU criterion

Restore the reference condition: focus peak >=95% and >=40% of known CPU rows
at >=90%. Previously C++ used mean >=85% instead of the fraction. That missed
[95,95,10,10,10] and incorrectly accepted [100,89,89,89,89]. Unknown values are
excluded by the existing summarizer, not filled with zero. Unknown/invalid
fractions and contradictory zero counts cannot trigger the finding. Evidence
includes mean, peak, known count and fraction. A single/missing count or incomplete
window caps confidence at medium. Thermal-coincidence findings retain precedence.

These are row-weighted observations, not time-weighted duration or independent
measurements. Accepted reused scalar caches within the freshness budget remain
possible. The finding explicitly does not prove continuous sustained load,
overheating, or the cause of a symptom. The application monitor's separate
app.cpu.sustained rule remains to be audited.

## Incident system-log evidence

- General and incident log classification share the existing first-match
  categories and background-noise suppression. Add the machine-check spelling;
  incident recognition now also covers existing general keywords such as mce/hung.
  These keyword heuristics are not a new structured Event ID/provider classifier.
- Count exact unique strings for each category, distinguishing snapshot-diff
  strings from other timestamp-related strings. Do not count unrelated categories
  or double-count an exact string present in both inputs. These are unique lines,
  not guaranteed distinct OS events; normalized signatures remain separate in the
  general-log path. Stage48 shortening can hide distinctions and remains estimated.
- Use each record's own timestamp relative to marker_timestamp. Default window is
  -120/+15 seconds, inclusive; supported post_seconds values extend up to 300 as
  in the worker. A new modern row must be in [0, post_seconds]. Existing rows near
  the mark (<=15 seconds absolute) have high confidence only with valid log quality;
  older records in the window are medium. No global nearest offset from another
  category can elevate the finding.
- Diff-based high confidence requires valid aggregate quality. Estimated/unknown
  data caps at medium; collector/permission errors cap at low. Explicitly
  unsupported comparisons or unavailable snapshots cannot label rows as new.
  valid+collection_limited is estimated, and subsequent unknown metadata cannot
  mask an earlier collector error. Incompleteness emits a coverage finding.
- Recent legacy reports without marker_timestamp can use exact entry/offset
  matches, not a global closest_offset_seconds. Invalid explicit markers do not
  silently use this fallback. Legacy new arrays without quality metadata remain
  available at medium confidence with an explicit novelty qualification. Missing
  event-time relationships produce coverage, including mixed parseable records.
- Text states whether evidence is a snapshot difference or a time relationship;
  partial comparisons do not establish novelty. WHEA severity is retained, but
  no specific failed component or cause of the symptom is claimed. The app_hang
  category also contains application errors, so text no longer asserts that
  every matching entry proves a hang.

## Tests and boundaries

New orion_incident_findings_tests exercises the real diff/merge/filter pipeline
with synthetic logs, per-category counts/offsets, duplicates/overlap, near/window
boundaries, partial/failed snapshots, contradictory imported fields, legacy
metadata, missing times, precedence/noise, TXT/JSON and CPU counterexamples.
Incident-data tests confirm stale CPU values cannot enter the fraction while
fresh observations can trigger it. Main UI receives a synthetic report through
the production signal and checks visible medium confidence and the qualified text.
The existing legacy golden cases and detached-card/layout checks remain required.

No OS log writes, user-program launches, real stress, speed test or LAN scan.
Native 100%/150% UI and extracted portable checks are recorded separately in
outputs/STAGE49-VERIFICATION.md (distributed alongside, outside the source tree).
The source ZIP matches the built source tree, but is not separately rebuilt.
Physical monitor transitions, clean Windows, real hardware and Linux remain open.
