# Stage 7 checkpoint — native Task Manager

Stage 7 replaces the Task Manager placeholder with a real cross-platform
process inventory. This is also the first shared foundation for the future
problematic-application process-tree monitor.

## Process contract

Each row can contain:

- PID, parent PID, process name and thread count;
- CPU usage measured between consecutive snapshots;
- resident working set and private memory;
- cumulative process read and write bytes.

Individual counters are optional. A protected or short-lived system process
is retained in the inventory even when Windows or Linux denies one of its
memory/I/O queries. Missing access is not turned into a fabricated zero.

Process CPU follows the original Python/psutil presentation: 100% represents
one fully occupied logical processor, so a multithreaded process may validly
exceed 100%. The table therefore makes genuinely parallel workloads visible
instead of dividing their value by the machine's processor count.

## Windows collector

- `CreateToolhelp32Snapshot` enumerates processes, parent PIDs and threads;
- `GetProcessTimes` supplies cumulative kernel/user time for interval CPU;
- `GetProcessMemoryInfo` supplies working set and private bytes;
- `GetProcessIoCounters` supplies cumulative transfer bytes;
- inaccessible processes retain their identity and available fields.

## Linux collector

- `/proc/<pid>/stat` supplies identity, parent, threads and CPU ticks;
- `/proc/<pid>/status` supplies resident and anonymous/private memory;
- `/proc/<pid>/io` supplies cumulative disk transfer bytes;
- processes disappearing during enumeration are skipped independently.

## UI and lifecycle

- the default view contains the 15 highest CPU consumers;
- «Показать все процессы» switches to the complete inventory;
- search by case-insensitive name or PID always uses the complete list;
- all numeric columns use native numeric sorting;
- collection runs on its own worker thread only while the tab is visible;
- the global pause stops process scans and resume restarts them;
- the current checkpoint is read-only and does not terminate external
  processes.

## Live verification

The Windows verification machine returned about 280 processes. Sequential
sampling produced valid CPU values, including values above 100% for a heavily
multithreaded game, and valid working-set/private/I/O/thread data. The packaged
probe uses the same collector and prints the five current CPU leaders.

All 9 CTest targets and all 17 diagnostic golden cases pass. The next direct
migration block is the read-only Autostart inventory.
