# Lab build 39 enhanced stress capture and build 40 optimization plan

2026-09-21. User requested the latest 720p log, app closure, a comprehensive plan,
then implementation of useful CPU optimizations while retaining the enhanced
settings as a stress test. Lab only; no main-app promotion.

## Evidence and corrected attribution

Capture `6668789367901` is complete: 600 samples, 599 complete frames after the
boundary sample. All 599 match transport/pacing records, with no dropped pacing
records or worker accounting errors. Runtime launch at 09:43:51 local reports:
1280×720, FSR1 to 2416×1359, Enhanced shadows (512 base / 1× range), Original
world distance and model LOD, 1080p reflections, SMAA, motion blur off.
The render-frame CSV's 1920×1080 display field is the title's display extent;
use the runtime output policy/presenter for the physical output configuration.

Diagnostics and runtime were saved under
`out/m5-lab/validation/build39-720p-enhanced-20260921/`. Lab PID 7020 was
terminated after retrieval. The prior stress capture `6658360052532` used the
same reported settings except Ultra shadows (1024 base / 1.5× range). Scene,
route, traffic and camera are not controlled. Comparisons are descriptive,
not a proven shadow-only speedup. GPU timing is a coarse command-queue envelope;
physical scanout is not measured. These are instrumented runs.

| Instrumented metric | Previous Ultra run | New Enhanced run |
| --- | ---: | ---: |
| Renderer interval, median | 56.80 ms | 46.26 ms |
| Renderer interval, p95 (nearest rank) | 68.19 ms | 53.69 ms |
| Maximum renderer interval | 323.70 ms | 62.35 ms |
| GPU envelope, median | 21.33 ms | 12.91 ms |
| Render callback, median | 30.48 ms | 23.84 ms |
| Command recording, median | 20.49 ms | 15.24 ms |
| Worker between-publication gap, median | 26.12 ms | 22.39 ms |
| Worker CPU within that gap, median | 24.64 ms | 21.41 ms |
| Process physical footprint, median | 2516 MiB | 1887 MiB |

The new median is about 21.6 instrumented frames/second. Reaching 33.33 ms
requires removing about 12.93 ms, or 28% of this interval, with additional
headroom needed for heavy-scene tails. The GPU already averages 13.12 ms.
Do not add CPU and GPU durations as if they were serial or promise a gain
based on summing nested profiler scopes.

**Important correction:** legacy `cpu_guest_gap_*` fields sample
`mach_thread_self()` in `PublishFrame`, called from `RenderWorkerMain`.
They measure the render worker between frame publications, including assembly,
dispatch, recycling and waits. They do not directly measure guest simulation
or physics CPU. Earlier descriptions of this as game/producer CPU were wrong.
Build 40 corrects HUD, metadata and analyzer explanations, retaining legacy
column/API names for compatibility. Independent producer transport spans remain
valid but include overlapping/nested work and backpressure.

Latest mean transport/CPU observations:

- 17,143 processed commands/frame: 12,375 state, 4,056 draw, 712 other.
- Command acquire/default initialization: 5.77 ms/frame. It is not purely malloc.
- 12,217 reused command slots/frame, about 71.3%; roughly 4,926 fresh slots.
- Producer capture 28.86 ms, including validation 16.73 ms and backpressure
  9.26 ms. Do not add these totals. Queue lock 1.96 ms and capture lock 0.56 ms.
- Worker assembly 12.60 ms, including constant application 2.92 ms, snapshot
  0.75 ms and frame insertion 0.89 ms. Recycling is separately 1.76 ms.
- Worker acquisition/dispatch total 4.13 ms: queue mutex 0.12, condition wait
  0.08, transfer 0.21, protection 0.80, dispatch remainder 2.93 ms.
- Texture preparation 3.17 ms inclusive. Command recording 15.25 ms inclusive.
  Exclusive recording categories include record-command 2.73 ms, common draw
  bindings 1.51, buffer upload 1.49, constant binding 1.34, shared constants
  1.23, surface lookup 1.05 and dynamic Vulkan state 0.59 ms.
- Detailed profiler bookkeeping accounts for about 2.49 ms exclusive plus
  unisolated instrumentation overhead; about 165,695 CPU clock reads/frame.
  Do not subtract that figure to predict unlogged FPS.
- Thermal state is nominal throughout. GPU fence waits are negligible, no
  pipeline compilation/wait is measured, and the limiter sleeps only once.

The worker has little wait time while the producer experiences backpressure.
The leading target is CPU command processing/assembly/recording. Affinity,
more queued frames, another limiter, or a GPU rewrite are weak first choices.

## Build 40: focused implementation bundle

1. **Suppress identical binding notifications before packet allocation.**
   A bounded producer cache remembers only validated, successfully queued
   texture, vertex-stream, index-buffer and render-target/depth bindings.
   Exact command bytes include device, slot, surface fields and trace context.
   The corresponding worker operations already return without changing state
   when identical. Font texture registrations and shader/declaration commands
   retain their full path. Registry/release/unlock/device/present/resolve and
   other unrecognized boundaries invalidate the cache. Synchronous locks and
   diagnostic envelopes also invalidate it. Draw resource capture, ownership,
   command order and game state remain authoritative. Export
   `producer_binding_skips` so a negligible hit rate is visible.
2. **Compact the state-command stream.** All eight non-font binding/shader
   notification types now use a 192-byte state packet instead of the prior
   3008-byte full draw packet (about 94% less packet storage). The producer preserves shader metadata updates; the worker runs the
   same state application function directly without expanding into a draw packet.
   Mixed packets retain FIFO and unique ownership. Compact notifications carry
   no texture-resource references; draws still capture and protect generations.
   Font updates, diagnostic envelopes, resource registrations, draws and sync
   commands use the original full path. State packets use a separate bounded
   recycler (8192 slots, 1.5 MiB shared storage), while full-packet retention stays
   at 2048. Queue entries grow from one pointer to two; queue depth is unchanged. Export
   `compact_state_commands` and log the actual packet sizes to verify coverage.
   This is the larger experiment: remove allocation/initialization/destruction
   and lifetime-scanning work across the majority state traffic, not merely tune
   the original pool. The earlier idea of enlarging the full-packet pool is
   superseded, avoiding roughly 18 MiB of additional large-packet retention.
3. **Correct measurement attribution.** Preserve export compatibility but label
   the worker gap correctly. This avoids optimizing game simulation based on
   measurements actually taken on the render worker.

These are hypotheses awaiting device acceptance. The redundant-state hit rate
and actual compact-packet coverage require device measurement. No locked-30 claim.

## Next optimization stages, ordered by evidence

### 1. Compact command transport and worker assembly

After build 40, rank remaining producer acquisition, worker assembly, recycle,
and command-type counts. Verify that compact packets cover the expected state
traffic and reduce per-command cost. If draw-packet materialization dominates
afterward, investigate a lean retained-draw representation or fewer immutable
snapshot copies. Keep resource generations and unique ownership; this would be
a separate larger refactor, not an extension of caching mutable guest memory.

Within assembly, measure prewarm cost and the remaining constant-delta work.
Empty-delta skipping and snapshot reuse already exist; do not reintroduce them
as new fixes. Preserve bootstrap deltas, boolean constants, generation checks
and shader/vertex-declaration identity. The previous pipeline-key batching
experiment regressed and was reverted; do not repeat it without new evidence.

### 2. Texture/binding preparation and warm draw preparation

The 3.17 ms texture phase and repeated surface/binding work justify a source
and hit-rate audit. Existing code already memoizes prepared images by generation,
samplers, consecutive binding sets, draw pipelines, render targets and constants.
Targets are remaining full-stage scans/temporary-array initialization on the
indexed path and duplicated prewarm/record lookup work. Reuse only immutable
results keyed by shader state, resource generation, target format/sample count,
and descriptor epoch. Keep depth/resolve/alias transitions and invalidation.

First measure which loops remain after existing cache hits. Batch a confirmed
improvement with the next command-layout change, not speculative global caching.
Do not bypass buffer-shadow validation: previous shortcuts were rolled back.

### 3. Streaming and frame-time tails

Compare first and repeated traversal of the same long street. Log bytes uploaded,
new texture generations, pipeline misses, resident memory and longest frame spans.
This warm capture has no pipeline wait, so another shader-prewarm change cannot
explain its sustained cost. Move only proven independent decoding/preparation
jobs off the critical worker, with bounded queues and intact guest order. Avoid
upload deferral that silently displays stale or missing resources.

### 4. Graphics cost controls without hiding engine progress

Keep 720p + Enhanced shadows + 1080p reflections + SMAA as the first stress
comparison. After CPU changes pass, repeat at 900p to test whether the win
transfers to the user's main target. Separately expose shadow resolution and
range, and individual mirror/water/environment reflection budgets using existing
controls. Measure their draw/pass cost before designing a balanced preset.
A lower setting is a quality/performance choice, not evidence of engine speedup.

### 5. Real producer/game profiling and safe parallel work

If worker savings leave a producer bottleneck, capture a short symbolicated
CPU sample of the actual producer thread. Rank generated game functions and
caller stacks before replacing or parallelizing anything. Current gap fields
cannot identify physics/visibility/AI CPU. Prefer parallel immutable preparation
with explicit dependencies; keep simulation order and strict floating-point
behavior. Existing two frame slots and asynchronous worker already overlap work.
Do not equate more threads or all-core utilization with better latency.

### 6. Presentation, power and long-session stability

Once work approaches budget, measure actual presentation intervals as well as
content publication. Keep physical scanout claims separate from this profiler.
Target fewer missed 33.33 ms deadlines and lower p95/p99; a 30 FPS average is
insufficient. Current negligible waits provide no case for a VSync change now.

Then compare equal-quality, capped runs after thermal equilibrium, including
battery/power state, thermal state, CPU time and GPU work. Less command churn
should reduce work, but energy benefit must be measured. Preserve the existing
physics timestep/watchdog changes as a separate safety line; absence of an error
in this short capture does not prove the 10–20 minute physics failure is fixed.
Require at least a 25–30 minute driving/collision session before main promotion.

## Verification and acceptance

Before building: randomized binding-stream equivalence; changed fields/devices;
malformed inputs; font side effects; reset/resource/synchronous boundaries;
recycler FIFO/resource destruction/burst cap; normal and ASan/UBSan runs; profiler
CSV/metadata checks; Release build/signature/identity verification. Preserve
signed build 39 as rollback. Do not install or launch during plan/source work.

First user test of the combined candidate: same save, settings, camera and route;
an unlogged first pass, an unlogged repeat pass, then one captured repeat pass.
Compare against build 39 with the same settings, not Ultra versus Enhanced.
One combined build per driving session is enough; changes remain independently
revertible in source. Inspect missed deadlines, >40/50/100 ms intervals, memory,
audio underruns, visible resources and physics. The new packet/cache counters
must show the intended reduction. If hit rate is negligible, memory grows, or
normalized CPU cost/pacing regresses, remove the relevant experiment.

Aim first for a measurable reduction of the ~46 ms interval; ultimately reserve
several milliseconds below 33.33 ms in representative heavy scenes. Multiple
validated steps may be needed. Preserve Lab isolation throughout.

Implementation checks: 19 focused host test cases passed 722,250 assertions
in ordinary and AddressSanitizer/UndefinedBehaviorSanitizer builds. These cover
packet validation, mixed FIFO transport, resource destruction, bounded recycling,
randomized redundant-binding equivalence and profiler aggregation. Device
performance and long-session gameplay acceptance remain pending.
