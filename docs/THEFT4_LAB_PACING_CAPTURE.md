# Lab frame-pacing measurements

Lab build 9 adds measurements to the existing manual 600-frame capture. It
does not change the frame limit, wait decision, VSync mode, graphics settings,
command order, or frame-resource count. Device validation remains pending.

## Run when the user is ready

1. Install the verified isolated Lab artifact. Launch `com.theft4.m5lab` with
   `THEFT4_LAB_NATIVE_PROFILE=1`. Ordinary launches keep profiling disabled.
2. Reach the gameplay section to measure, then double-tap the FPS counter. The
   orange background means requested; verify completion in the runtime log.
3. Continue the same route for approximately 30 seconds. Retrieve diagnostic
   exports only after the log reports a complete 600-sample capture, then close
   Lab. One capture per process is supported. No debugger is required.
4. Preserve the full set of files under `Library/Application Support/LibertyRecomp/Diagnostics/`:
   `native-performance-latest.csv`, `native-performance-meta.json`,
   `native-performance-frames.csv`, `native-performance-pacing.csv`, and the
   existing CPU/GPU detail files. Use matching capture identities.
5. Run `python3 tools/analyze_lab_pacing.py <capture-directory> --output <summary.json>`.
   Inspect missing/dropped records before interpreting any timings. Repeat
   acceptance measurements with profiling disabled.

## New observations

- `native-performance-pacing.csv` identifies each present by submitted frame,
  host system thread and guest thread. It records the present-hook entry,
  submission begin/end, limiter entry, limiter-mutex acquisition, sleep begin,
  wake and limiter exit. A fixed 1024-record store bounds memory use; overflow
  is counted rather than overwriting evidence.
- Limiter decision fields include requested/applied FPS, prior and next
  deadlines, intended wait, mode changes and late resets. Derived values
  distinguish the planned wait at decision time, the remaining requested sleep,
  actual elapsed sleep, and wake lateness beyond the deadline. No-wait samples
  must not be counted as zero-overshoot sleep observations.
- `native-performance-frames.csv` adds first/last producer enqueue markers and
  worker dequeue markers, measured command-sequence bounds, and exact profiler
  publication begin/end ticks. These locate command delivery relative to the
  renderer and limiter. The enqueue marker precedes final queue insertion; the
  present submission-end marker follows the submission call's return.
- Worker acquisition/dispatch is split into initial queue-mutex acquisition,
  condition-variable wait (including mutex reacquisition), batch transfer, and
  remaining dispatch setup. Their sum is the existing `worker_idle_ms` total;
  that legacy name does not mean pure sleeping. Assembly remains separate.
  Counts and partition errors disclose incomplete or inconsistent accounting.

## Interpretation rules

The top-level profiler schema remains 2, with transport schema 3 and pacing
schema 1. Fields are appended so existing readers can continue selecting known
columns. `worker_transport_split` explicitly indicates whether the new worker
measurements were compiled in.

Host ticks use the same monotonic clock and frequency as the existing profiler.
Limiter `*_ns` deadlines use `std::chrono::steady_clock`; compare deadlines and
wake nanoseconds within that domain. Never directly subtract a host tick value
from a steady-clock nanosecond value. The capture trigger/stop ticks describe
the requested capture window; records in progress at either boundary may be
missing. Raw first-frame data is retained but excluded from default analysis.

Match by capture/frame/thread identity and actual timestamps, not CSV row
position. Producer threads may finish observations in a different order. The
renderer interval assigned to frame N ends at the beginning of frame N+1;
N+1's command transport can therefore explain part of N's interval. A producer
can also run ahead of the renderer, so frame numbers alone do not prove that
a limiter sleep blocked the renderer during that time.

Normal sleep maintains the 30 FPS cap. It is not automatically wasted time.
CPU spans are elapsed durations, not sampled on-core work. Queue-lock and
command-capture sums overlap other activity, and worker acquisition components
must not be added to their legacy total. No displayed-frame/scanout timestamp
is collected here. GPU timestamps and CPU instrumentation have overhead.

The next diagnosis should distinguish late producer work, queue contention,
and limiter wake delay before selecting a pacing change. Main-app promotion
requires a separate ordinary-launch gameplay acceptance run.

## Validation

Host tests cover capture bounds, immutable stop, multiple producers, stopping
while a producer runs, worker accounting, CSV units/unavailable values, and
the unchanged frame-limiter planner. The Lab export fixture verifies matching
metadata/CSV identities and the analyzer's unit conversions. Release compilation,
signing and identity checks are recorded alongside the private Lab artifact.
No device launch is part of preparation alone.
