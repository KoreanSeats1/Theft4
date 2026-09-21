# Build 33 device review and next CPU optimization plan

2026-09-20. Analysis only; no game source, build, install or main-branch changes
were made during this review. Diagnostics and runtime log were copied before
terminating Lab PID 5761. A subsequent process query confirmed Lab had exited.

## Capture and comparison limits

The installed app is `com.theft4.m5lab`, version 0.1.3, build 33. Its runtime
reports the new unique-owner queue, a 2,992-byte command and 8-byte queue entry.
Source: `85245661`; previous build 32 baseline: `304345d5`.
Capture 5281558940593 contains 600 samples, with the partial first sample
excluded: 599 analyzed frames, 1745–2343. Capture was armed at 16:32:34 local.
Baseline capture: 5225575488853, also 599 complete frames.

Both captures use 1600×900 rendering, FSR1 quality, 2416×1359 output. The routes
and scene workloads are not controlled. Build attribution uses installed app
and session evidence; metadata does not contain a source commit. These are
instrumented renderer intervals, not physical scanout or an unlogged FPS
benchmark. Timings overlap; producer, worker and GPU durations must not be
added together. Quantiles below use nearest rank consistently for both runs.

Raw evidence, analysis script, comparison JSON/text and pacing summary:
`out/m5-lab/validation/build33-device-review/`. Baseline raw evidence:
`out/m5-lab/validation/build32-review-20260920/`.

## Results

| Measurement | Build 32 | Build 33 |
| --- | ---: | ---: |
| Mean renderer interval | 40.573 ms | 37.844 ms |
| p95 renderer interval | 47.582 ms | 45.576 ms |
| p99 renderer interval | 51.648 ms | 48.550 ms |
| Maximum renderer interval | 79.132 ms | 51.012 ms |
| Intervals over 40 ms | 357/599 (59.6%) | 182/599 (30.4%) |
| Intervals over 45 ms | 117/599 (19.5%) | 40/599 (6.7%) |
| Intervals over 50 ms | 9/599 | 1/599 |
| Worker queue-to-batch transfer, mean | 4.533 ms | 0.161 ms |
| Producer queue-lock wait, mean | 7.401 ms | 1.542 ms |
| Producer state capture, mean | 3.399 ms | 2.252 ms |
| Producer validation span, mean | 10.970 ms | 14.687 ms |
| Total producer capture, mean | 20.189 ms | 22.879 ms |
| Worker assembly, mean | 8.909 ms | 9.509 ms |
| Producer backpressure, mean | 0.865 ms | 5.862 ms |
| Command recording, mean | 13.974 ms | 12.026 ms |
| Coarse GPU envelope, mean | 14.779 ms | 11.565 ms |
| Commands/frame, mean | 16,700.6 | 16,329.0 |

The transport changes hit their intended targets: batch transfer fell about
96%, queue-lock waiting 79%, and state capture 34%. Command count fell only
2.2%; normalized costs also improved (batch transfer 0.272→0.010 microseconds
per command; queue-lock waiting 0.455→0.094; state capture 0.203→0.138).
That is stronger evidence for the targeted optimization than FPS alone.
The reduction in long intervals is consistent with the user's smoother feel,
but different scene load, including lower GPU time, prevents attributing the
entire interval improvement to these changes.

## What still holds it back

**Command allocation/initialization is a plausible new cost, not yet an
isolated measurement.** In `SubmitTitleCommand`, the validation timer starts
before `std::make_unique<NativeCommand>()`. After subtracting its measured
state, geometry and texture subspans, the validation remainder grew from
2.022 to 7.144 ms/frame. This remainder includes allocation/default
initialization plus common validation, copies and resource work. It cannot
all be labelled allocator time. Nevertheless, allocating a large object for
every command is the concrete new operation here: at this command density,
a 30 FPS workload would request roughly 490,000 command allocations/second.
Reusing storage is the leading next experiment while retaining cheap queue
ownership transfer.

**Worker command assembly remains substantial.** It averages 9.509 ms and
reaches 15.596 ms. It includes constant-delta application, pipeline snapshots,
frame construction and other work; retained draw commands still move into
`current_frame_` once. We need subspans before deciding which dominates.
There is a specific redundant-state opportunity: `ApplyStateCommand` always
updates the pipeline version for `kSetVertexDeclaration`, even when both the
handle and resolved resource are unchanged. Shader paths already guard this.
Such version changes can defeat `SnapshotPipeline` reuse. The frequency and
potential saving remain unmeasured.

The slowest 60-frame window averaged 43.109 ms with about 19,399 commands/frame
and only a 10.689 ms GPU envelope. A lighter window averaged 32.645 ms with
12,223 commands/frame and 9.009 ms GPU time. The longest interval, 51.012 ms,
was followed by 15.322 ms of worker assembly and 10.233 ms GPU time. Some of
the longest intervals had no texture upload, so new texture uploads alone
cannot explain the spikes.

Aligning interval N with work in N+1 gives descriptive correlations of about
0.91 with worker assembly, 0.74 with command count and 0.30 with GPU time.
These are associations within this run, not proof of causation. Together with
the code and timings, they favor the command producer/assembly path.

Limiter wake overshoot averaged 0.0083 ms, p95 0.0169 ms; there were no dropped
pacing records or worker partition errors. Pre-submit worker wall/on-core
times were 16.018/15.742 ms, giving little evidence for broad scheduler stalls
in that measured phase. Neither CPU affinity nor another VSync experiment is
the best-supported next move. Higher producer backpressure also argues for
reducing downstream work before increasing queue depth.

Detailed profiler bookkeeping alone reports 2.025 ms/frame of self time.
Logging can affect performance; this number cannot simply be subtracted to
predict unlogged FPS, and it is not all instrumentation overhead.

## Recommended next build: one focused bundle

1. **Bounded command-storage recycling.** Keep queue ownership transfer; reuse
   command storage to reduce allocation/free churn. Reset all command state,
   release shared resources at the same logical lifetime boundaries, preserve
   FIFO ordering, synchronous completion and shutdown behavior. Batch returns
   to avoid adding a shared lock per command. Cap retained storage and allow
   correct fallback allocation on bursts. Pooling must not keep old resource
   references alive or let a producer overwrite in-flight command data.
2. **Skip unchanged vertex declarations.** Resolve the registered resource
   first and compare both handle and resource identity before returning. A
   reused guest handle with a new resource must still update state. This is a
   small, specific reuse improvement, with no promised millisecond gain.
3. **Measure the unresolved work in that same build.** Separate allocation,
   initialization/reset and remaining validation; split worker assembly into
   constant application, snapshot work and frame insertion. Count pool
   hits/misses/high-water retention, command types and avoided declaration
   changes. Prefer aggregate counters and bounded instrumentation overhead.

Avoid combining this with physics changes, resource-validation shortcuts,
larger queues, thread-affinity changes or a rendering-pipeline rewrite. If
allocation is cheap but assembly stays dominant, use the new split to choose
the following fix. Removing the final retained-command move is a later
candidate, contingent on measured cost and locality/lifetime review.

Verification before device use: command reuse/reset and resource lifetime
tests, FIFO and synchronous/shutdown coverage, burst/fallback coverage,
same-handle/new-resource declaration coverage, sanitizers, Release build and
signature checks. Preserve build 33 as the rollback app. Then compare one
combined candidate on the same 900p route, first unlogged, then with one
capture; include a first pass and repeat pass to distinguish streaming.

Success means lower producer capture/assembly and fewer >40/45 ms intervals
without renewed queue contention, rising retained memory or correctness
errors. A locked 30 FPS is not demonstrated: the current mean is 4.51 ms over
33.33 ms, and the heaviest window is about 9.78 ms over. There is useful work
to target, but the next build cannot be promised to recover all of that.

## Stability observations

Thermal state stayed 0. Buffer-shadow mismatches, buffer fast-path disables
and texture allocation failures were zero. Logged audio counters showed no
underruns, rebuffers or nonfinite samples. These limited observations do not
resolve the separately reported longer-session physics bugs. Performance
improvement alone is insufficient to promote Lab into the main app.
