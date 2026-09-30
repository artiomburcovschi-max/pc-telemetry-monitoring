# Stage 36 — trustworthy partial scans and sensor evidence

The Stage 35 review found three concrete correctness gaps: a cancelled scan
could retain cached temperatures as its post-load snapshot; the converter
promoted general sensor rows to valid even when their collection was stale;
and its fallback could use an arbitrary CPU core or GPU memory temperature in
place of the backend's primary measurement.

Deep Scan now starts with fresh diagnostic evidence and clears cached hardware
and autostart results too. Until the final sensor collector returns a result,
the report contains `sensors.data_quality=not_collected`. Earlier successful
phases remain in a partial report. Memory captured only before cancellation is
labelled as before-only. Full Scan preserves this evidence through composition.

The pure `DeepScanWorker::sensorSnapshot` converter uses the primary CPU/GPU
metrics selected by the platform backend. General channels remain inventory,
not fallback primary values. Valid/estimated readings feed the current snapshot;
stale primary temperatures remain unknown. Stale inventory rows retain their
quality and original observation time. Unusable retained collection values are
excluded from the presented inventory. Temperature and fan collection metadata
are independent, with mixed quality reported as partial and provider reasons
visible in TXT. Real zero measurements remain valid values.

The dedicated regression checks exercise this actual native conversion boundary
with synthetic telemetry, rather than replacing it with prepared JSON. Cases
include primary-versus-core/memory identity, stale data, failed collectors,
permission denial, zero values, current RAM transfer, cancellation with a hot
cached snapshot, failure before any collection and failure in the final sensor
phase. Full Scan checks propagation of an absent post-load snapshot.

This is a report-correctness checkpoint, not completion of the full migration.
Next: source-led Task Manager and Autostart field/interaction/visual comparison.
Linux runtime, high-DPI review and manually confirmed real load remain separate
validation work. See the delivered STAGE36-VERIFICATION.md for executed checks.
