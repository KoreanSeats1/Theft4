# Lab build 33: CPU command transport

Goal: improve sustained 1600×900 frame production toward the 33.33 ms budget
for a 30 FPS cap. This is an experiment awaiting device gameplay validation,
not a claim of a locked frame rate.

## Evidence and prior experiments

The saved capture retrieved on 2026-09-20 was written at 15:54 local time,
after the build-32 install. The device still reports Lab build 32. Runtime
settings for that session are 1600×900, FSR1 quality, 2416×1359 output.
Capture ID: 5225575488853. The metadata contains 600 samples; the partial
first frame is excluded below. These files do not embed a source commit,
so build attribution uses install/session timing rather than an embedded ID.

Raw files are preserved in the ignored validation directory
`out/m5-lab/validation/build32-review-20260920/`.

| Measured span or gauge | Mean | p95 |
| --- | ---: | ---: |
| Renderer frame interval | 40.573 ms | 47.500 ms |
| Coarse GPU envelope | 14.779 ms | 18.187 ms |
| Render worker pre-submit wall time | 17.900 ms | 21.155 ms |
| Render worker pre-submit on-core time | 17.309 ms | 20.373 ms |
| Producer capture sum | 20.189 ms | 24.751 ms |
| Producer queue-lock wait sum | 7.401 ms | 9.833 ms |
| Worker queue-to-batch transfer | 4.533 ms | 5.539 ms |
| Worker command assembly | 8.909 ms | 11.036 ms |
| Producer state capture | 3.399 ms | 4.424 ms |

Thermal state, buffer-shadow mismatches and fast-path disables are zero
throughout this capture. The maximum renderer interval is 79.132 ms. Timings
overlap and include profiling overhead: do not add them, subtract the GPU
envelope from the interval to label all of the remainder CPU execution, or
equate renderer intervals with physical scanout. In particular, producer
validation encloses state/geometry/texture capture; those are subdivisions.
These runs are not a controlled route-matched before/after benchmark.

The older build-23 Instruments capture used about 3.49 running core
equivalents. Render worker and command producer together accounted for
44.6% of sampled CPU; both showed command move/destruction work. It disproves
the idea that the whole game runs on one core, but does not prove useful
parallelism can be added to every guest operation.

Relevant history:

- MCLA-derived geometry/binding reuse produced the substantial earlier gain.
- Native worker QoS was already raised to user-initiated. The build already
  uses `-mtune=apple-m5`.
- Larger worker batches were tried; merely increasing their size is not the
  next experiment. Pipeline-key batching was reverted after a regression.
- The once-per-frame buffer-validation shortcut was reverted in
  `d53236ad`. Per-reuse validation and its session fallback remain.
- Build 32's consecutive-target cache remains, but the user's impression
  alone does not establish a measurable gain.

Apple recommends reducing submission/synchronization overhead, choosing
useful job granularity and appropriate QoS before adding workers. Our choice
to shorten the existing queue handoff follows that guidance; it does not
depend on pinning threads to named M5 cores.
[Apple: Tune CPU job scheduling for Apple silicon games](https://developer.apple.com/videos/play/tech-talks/110147/).

## Changes in this build

1. **Transfer command ownership through the queue.** In Lab, capture constructs
   one heap-owned command. Queue and worker batch move its unique owner
   instead of the entire object and its arrays of shared resource references.
   Retained draws still move once into the frame as before. FIFO order,
   backpressure, synchronous requests, reference protection and shutdown
   cleanup are preserved. This targets both batch-transfer cost and producer
   contention on the same queue mutex. A heap allocation remains per command;
   device measurements must establish the net allocator/cache benefit.
2. **Reduce constant-state preparation overhead.** Capture reuses scratch
   vectors under the existing capture mutex. Single-span dirty masks walk
   their bits directly in semantic order, eliminating element gathering and
   sorting. MSB-first vertex/pixel constant ordering remains explicit.
   General overlapping/multiple-span layouts retain the old path. Captured
   payloads still own their bytes; scratch never escapes to the worker.
3. **Reject no work earlier.** Validated render-state notifications already
   ignored by Lab now return before constructing a command or taking capture
   locks. Size, type and nonzero-device validation remain; malformed inputs
   follow the normal rejection path.

The normal app, A12X working tree, resolution, physics, thread priorities,
GPU pipeline and pacing policy are not changed. The reported longer-session
physics glitches remain unresolved; this build is not a physics fix or a
candidate for merging into main on performance impressions alone.

## Verification and next device run

The focused test suite passes 26 cases and 688,215 assertions both normally
and with AddressSanitizer/UndefinedBehaviorSanitizer, including
randomized bit-mask comparison with an independent per-element oracle,
single-owner FIFO transfer, lifetime release on shutdown, and exact resource
pin comparisons for both inline and indirect queues. The Release iOS build
and code-signature verification also passed. Validation output is recorded in
`out/m5-lab/validation/build33-cpu-transport/`.

Build 32's signed app is preserved at
`out/m5-lab/backups/build32-304345d5/Theft4.app`, with source baseline
`304345d5`. New build number: 33. Bundle: `com.theft4.m5lab`.

Test the same heavy city route at 900p + the same FSR setting, first with
logging off, then save one capture after a second pass. Compare minimum
frame rate, frame-time spread, queue-lock wait, batch-transfer time, producer
state capture and total interval. A reduced queue cost without a matching
interval improvement means the bottleneck moved or lies elsewhere; use the
remaining producer/game spans to choose the next change. Longer-session
physics stability needs its own reproduced, timestamped investigation before
promoting Lab changes to main.
