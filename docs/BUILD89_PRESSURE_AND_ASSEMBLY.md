# Build89: memory-pressure response and frame assembly reuse

## Evidence and scope

Based on clean build88 `38f52ec2674a846016d1d324be8da7130bd143da`.
Reviewed the investigation/ledger, latest3341-frame capture, implementation history,
and the external tester triage context. The84 instruction/cycle and fixed-work
measurements support shared effective CPU-throughput loss, not doubled instruction
work. The exact system controller remains unknown. The88 indoor transition precedes
its compression episode and current-PID memory warning by about40seconds; memory
pressure cannot by itself explain the first indoor transition.

This pass targets avoidable repeated native work and the demonstrated later memory
pressure. It does not promise to preserve the initial operating speed. Existing
clock, audio, frame admission, pacing, constant helper, image lifetime and recipe
repairs remain. No core indices or M5-specific policy were added.

## Changes

### Native memory-pressure response

UIKit's existing warning increments an atomic counter independently of capture.
The renderer coalesces requests at its existing fence-protected maintenance boundary.
A120-title-frame window temporarily stops retaining newly retired idle image
allocations, and trims at most4already GPU-complete pooled allocations per pass.
The pool has an existing32MiB bound.

Cold reproducible texture images use a60-frame inactivity grace, the existing
current/staged/queued generation protections and completed-submission check.
A sweep visits at most128generations; pressure retirement is at most16images/8MiB
per pass, with a64MiB per-warning limit. One oversized allocation is admitted for
progress, so byte limits are soft by at most that allocation. GPU-produced content
is never discarded through this path. Descriptors and images still quarantine
through the existing retirement submission; reported image bytes are logical
retirement, not instantaneous physical release. No new GPU drain or wait is added.

CPU buffer shadows require60frames of inactivity plus cache-only shared ownership,
rechecked under the capture mutex. Pressure visits run every8title-frame positions,
with16entries/4MiB per pass and32MiB per warning, subject to the same oversized-item
progress rule. Existing ordinary budget/age reclamation continues separately.
No guest memory size or allocation limit is raised and guest pages are not decommitted.

**Memory Pressure Recovery** is a new default-ON System switch; OFF plus full restart
restores88's warning behavior. It operates without logging enabled.

### Successful pipeline receipt reuse

The old128-entry32set/four-way selector groups all same-shader/declaration variants
into one set, including different target/depth/blend modes. The new fixed512-entry
64set/eight-way cache mixes important logical target and fixed-state fields. A full
64-bit selector tag rejects unrelated entries before the complete logical-key
comparison. Hits refresh recency; replacement chooses the least recently used
receipt within its set instead of rotating through useful entries.

Correctness still requires exact full key equality, including lifetime/layout,
shader and declaration identity, strides, all fixed state and complete target/policy
context. Only successful pipelines are cached. No pipeline/driver object is newly
created by a receipt, and lifetime invalidation is preserved. Frame Assembly Reuse
OFF plus full restart disables this receipt path. Performance benefit is pending.

### Capture attribution

The existing sparse task sample now records decompressions when VM info revision5
is available (sample_valid bit32), plus the iOS available-memory estimate(bit64).
No VM region walk is added. Zero without validity is unavailable.

Long Capture samples one complete assembly frame per60title frames, using a separate
renderer-owned accumulator. It records assembly, constants, snapshots, prewarming,
insertion, post-Present clearing, command recycling, queue/condition wait, protection
and batch-transfer wall ticks plus sampled draws/setters/batches. Full profiling does
not populate this sparse accumulator. No global detailed profiler is enabled.

These are cumulative wall-time samples: queue/condition are waits, and constant/
snapshot/prewarm/insertion are included in assembly. Do not sum overlapping spans
or claim they are CPU time. Sample count advances after post-Present clearing;
read the next publication row to include the complete sample. Per-draw timing occurs
only in the sampled frame, averaging one-sixtieth of full worker instrumentation;
actual capture overhead remains to be measured. Schema is222fields throughout.

## Review and delivery

Regular Theft40.2.0(89), not a separate lab app. Signed88 app/dSYM are retained for
in-place rollback. Saves/settings must be copied before and compared after update.
No game launch or automated test run. Release compilation and signing verification
are delivery checks, not performance validation.

## User run

Keep Native and the same graphics settings. Parallel Render Preparation, Frame
Assembly Reuse, Renderer Efficiency, target reuse, texture conversion and Memory
Pressure Recovery ON; Runtime Wait Improvements OFF; Direct Guest Clock ON.
Fully restart after settings changes. Run without logging first; repeat with Long
Performance Capture. Stay inside with the same view through the spike and30seconds
of recovery, then go outside and hold the same view for60seconds. Save capture,
wait5seconds, and leave the game open for pulling. Do not clear learned recipes.
Record recovery, outdoor FPS and pop-in separately. A comparison with Memory Pressure
Recovery OFF isolates cleanup; signed88 is the whole-pass rollback.
