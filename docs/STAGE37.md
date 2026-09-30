# Stage 37 — Task Manager and Autostart interaction parity

Compared against the original `task_manager_widget.py` and
`autostart_widget.py`. The source comparison found a missing process-termination
action, refreshes that overrode header sorting, and selection that could migrate
to another row. Autostart is intentionally read-only in the original too.

## Implemented

- Windows Task Manager now offers explicit forced termination of the selected
  process. Confirmation names the process/PID, warns about unsaved data, uses
  plain text, and defaults both Enter and Escape to No.
- The collector carries the process creation timestamp. Selection and termination
  bind to PID plus timestamp, not row position or PID alone. CPU deltas also
  rebaseline when the timestamp changes instead of mixing two processes.
- The worker opens one handle, verifies creation time and critical-system status,
  and uses that same handle for termination. Missing identity, self/reserved PID,
  unconfirmed and overlapping requests are rejected. Missing safety information
  fails closed; access denial and already-exited/stale targets have explicit
  results. The UI remains responsive during a bounded exit wait.
- Global Pause disables refresh/termination and is rechecked after confirmation.
  Already issued termination cannot be undone. Shutdown retains a worker if an
  operating-system call outlives the bounded wait.
- Both tables preserve the chosen header sort on refresh. Process selection uses
  PID plus creation timestamp; autostart uses name/command/source/category.
  Removed or replaced entries clear selection. Filters cannot redirect an action.
- TOP-15 still means the 15 highest CPU users, sorted within that subset by the
  chosen header. Searching queries the complete inventory by name or PID.
  Missing metrics remain unavailable, and CPU colors use shared thresholds.
- Compact rows, wrapped metadata outside control rows and full-value tooltips
  improve narrow-window use. Disk I/O is labelled in its tooltip as cumulative,
  not a throughput measurement. Autostart command/source search and category
  filters remain combinable; unknown enabled state stays an em dash.

## Verification design

`orion_process_action_tests` exercises rejection gates, concurrent requests,
result forwarding and exception handling with injected executors. On Windows it
starts its own short-lived fixture child, rejects an incorrect creation identity,
then terminates only that child with the correct identity. It never targets an
existing user process. UI integration verifies refresh sorting/selection, PID
reuse, search beyond TOP-15, missing values, pause/self guards, default-No
confirmation cancellation and read-only autostart filtering with duplicate names.

See the delivered `STAGE37-VERIFICATION.md` for executed build, test, visual and
portable-archive checks. This checkpoint is not a completed migration.

## Remaining boundary

Process termination is Windows-only in this checkpoint; Linux process identity
and safe termination require a separate implementation and runtime validation.
Critical-system rejection is implemented but is not tested by attempting to
terminate a real critical process. Broader high-DPI/theme review remains. Next
source-led comparison: Problem Application fields, monitoring lifecycle and
reports, followed by remaining secondary windows and full end-to-end review.
