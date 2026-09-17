# Theft4 — CPU efficiency and paced-30 implementation plan

Date: 2026-09-16; updated 2026-09-17. **Status: initial and texture-quarantine P0b candidates both failed with Metal Invalid Resource. Buffer-quiescence candidate survived a short foreground playtest at Boost resolution; longer acceptance pending. Separate background GPU-permission failure recurred. App stopped; build only until a new install cue. P1–P5 remain planned.**

This is the current execution order for the installed native-renderer path.
It builds on the [CPU audit](THEFT4_CPU_PERFORMANCE_AUDIT.md), which contains
the build identity, source findings, measured weights and capture limitations.
Earlier generic-renderer plans remain historical references, not this baseline.

File notation: **SDK** means `glue/rexglue-sdk-main/`; **native** means
`glue/rexglue-sdk-main/src/graphics/gta4_native/`. Test filenames without a
directory are under SDK `tests/unit/graphics/`.

## Outcome and non-negotiables

Deliver one new game frame every **33.333 ms** in representative city gameplay,
without reducing visual quality, simulation accuracy or audio quality. Aim for
28–30 ms of critical-path work to leave headroom. This is a target, not a promise
that one optimization will achieve it.

Reference baseline: native renderer, AOT ARM64 Release/O3, 720p scene → 1080p
FSR 1, SMAA/high, 4× filtering, FIFO presentation and one native frame in
flight. The installed P0b candidate changes only the native frame-slot default
from one to two; one remains available through the launch override and retained
baseline artifact.
Keep the current physics, shadows, reflections, draw distance, 48 kHz audio,
inline XMA, saves, controller support and corruption/lifetime checks. **One frame
is the rollback baseline, not a permanent performance requirement.** The user's
follow-up adds an early controlled two-frame experiment, P0b, below.

Do not change the CPU ISA baseline, introduce global fast-math, remove real
game work, disable prewarming, replace the renderer or upgrade dependencies.
Change frame count only in its own P0b comparison, not together with CPU changes.
Experimental FSR Boost gets its own
acceptance run only after the 1080p reference improves.

P0b is now implemented as an isolated configuration candidate. Execution should
preserve unrelated dirty work, retain the rollback build and update the changelog.
Coordinate installation/playtest windows with the user; never uninstall or
delete app data to switch candidates. Close the game when no longer needed.

## Why this order

In the current 21-second city CPU capture, the native render worker and game
render producer account for approximately **43% of sampled application CPU**.
Command movement, hashing and repeated preparation are evidenced opportunities.
Ordinary logging is approximately 0.01% of recognizable sampled CPU paths;
removing logs is not the main strategy.

The state fingerprint chains 49 hash calls at multiple draw stages. The next
change can target that bounded cost without changing draw contents or ownership.
Command movement has a larger measured footprint but greater lifetime risk.
Pipeline preparation is third because caching/invalidation must stay correct.

These are CPU samples, not additive frame timings. Some system symbols and all
CPU/GPU wait correlation are missing. Do not interpret 43% as recoverable CPU
or promise a matching FPS gain.

| Pass | Deliverable | Risk / relative scope | Gate before proceeding |
|---|---|---|---|
| P0 | Reproducible baseline and bounded frame timings | Low / small | Settings/build identity known; measurement limitations explicit |
| P0b | One vs two frames in flight using the existing override | Medium–high safety risk / small configuration change | Ownership preflight, better pacing, stable foreground soak and acceptable input latency |
| P1 | Cheaper state fingerprints, same integrity checks | Low–medium / small | Correctness tests, faster helper, no gameplay regression |
| P2 | Fewer command-object moves | Medium–high / medium | FIFO/ownership/stress tests; repeatable CPU improvement |
| P3 | Reuse already-prepared pipeline/state work | Medium / medium | Warm gain without new cold-cache stalls or stale state |
| P4 | One newly measured residual bottleneck | Conditional | Fresh data chooses CPU, scheduling or GPU branch |
| P5 | Sustained 30 FPS and stability acceptance | Low implementation risk / test session | Frame deadlines, visuals, audio and saves pass |

P0b is the first device experiment once its resource-lifetime preflight passes:
it can test a potentially larger overlap gain without a renderer rewrite.
P1 can be implemented and microbenchmarked while resolving P0's missing device
trace. Do not spend repeated sessions trying the same disconnected profiler.
Keep P1, P2 and P3 as independently measurable and reversible candidates.

## P0 — preserve the baseline and measure the right thing

### Preparation

1. Retain the current signed app, matching symbols, executable UUID/hash and
   source-diff identity privately. Verify the retained artifact before any new
   build overwrites the normal build directory.
2. Record actual launch policy, filtering, frame-slot count, loaded save, route,
   shader-cache state, power/charging state and thermal state when available.
   Keep fixed output/settings between candidates. Preserve user saves; do not
   use a moving autosave as if it reproduced identical initial conditions.
3. Define three workloads with the user: a stationary long-view camera sweep;
   a repeatable 90-second driving route including the bridge; and a dense scene
   with pedestrians/traffic. Keep camera, weather/time and route as comparable
   as possible. Document variation from nondeterministic traffic.

### Measurement

- Start with existing Time Profiler and the native bounded CPU/GPU profiler.
  Capture only the short annotated slow section. The previous Game Performance
  and System Trace attempts disconnected: try again only after a concrete
  connection/tooling change, otherwise use the in-engine fallback.
- For the fallback, explicitly enable `Category::kNativeProfiler` for a
  bounded diagnostic run. `THEFT4_DIAGNOSTICS=1` alone does not currently select
  that category. Reuse the native recorder; do not add an unbounded logger.
- Separate producer CPU work, queue backpressure, worker preparation, recording,
  GPU fence wait and presentation. Correlate by frame/content identity. Record
  whether GPU timestamps are supported/valid, not just nonzero.
- Use successful **new-content** presentation timestamps, not display callbacks,
  image-allocation IDs or repeated frames. Prefer existing measurements. If
  insufficient, add a default-off bounded ring of monotonic timestamps/counters,
  with no formatting, allocations or file writes on the frame hot path. Export
  after capture. Do not reuse overwritten slots while a reader owns them.
- Report publication, GPU completion and displayed-frame measurements separately.
  CPU submission timing cannot prove actual scanout pacing or absence of tearing.
- Check instrumentation overhead with recorder on/off. Final FPS comparisons use
  ordinary Release launches without LLDB, GPU validation, screen recording or
  mirroring. Use matching instrumentation in baseline/candidate diagnostic runs.

Files: `tools/summarize_ios_time_profile.py`, `tools/summarize_ios_game_trace.py`,
`ios/bridge/theft4_boot.cpp`, native `native_cpu_profile_scope.h`,
`native_profile_detail.h`, `graphics_system.cpp`, SDK
`src/ui/vulkan/vulkan_presenter.cpp` and `include/rex/ui/guest_output_frame_sequence.h`.
Only extend measurement code if the existing paths cannot answer the question.

**Exit:** a usable comparison workload plus CPU attribution and explicit timing
coverage. Missing GPU data must stay marked unknown, not block all bounded CPU
work or silently become a claim that the game is entirely CPU-bound.

## P0b — controlled two-frame overlap experiment

### Why revisit it

One active slot forces reuse of that slot to wait for its prior GPU work. Two
slots can let the CPU prepare the next frame while the GPU finishes the previous
one. This may improve throughput more than a small helper optimization **if
serialization is the limiting factor**. It does not reduce the amount of CPU/GPU
work or make a GPU pass that exceeds the budget intrinsically faster. It may
increase input latency and memory pressure.

We previously chose one slot while investigating invalid resources and visual
freezes. That history is reason for a controlled test, not proof that two slots
caused every failure or that one slot eliminates all failures. The separately
confirmed background-submission failure occurs even at one slot.

### Existing mechanism, not a new renderer

`ios/bridge/theft4_startup.cpp` already accepts
`THEFT4_NATIVE_FRAMES_IN_FLIGHT=1` or `2`. Native `graphics_system.cpp` switches
between slot0 and a two-slot ring; `CompleteNativeFrameSlot` waits for the exact
submission before reset/reuse. The ring and lifetime tests already exist.
The installed candidate uses 2 for an ordinary app-icon launch. Set
`THEFT4_NATIVE_FRAMES_IN_FLIGHT=1` for the retained control/rollback launch.
No graphics-quality or CPU-optimization changes are part of this candidate.

Check the **effective**, not merely requested, count: native-profiler startup
temporarily forces one slot until profiler initialization completes. Exclude
that initialization from the comparison. Present drawables, swapchain images
and native frame-resource slots are different things; increasing a drawable
count is not a substitute for this experiment. Three slots are not part of the
current two-slot ownership implementation and are outside this pass.

### Preflight and test

1. Review actual integration for per-slot command pools/buffers, constant/upload
   arenas, descriptors, query/readback storage and retained resource generations.
   Mutable data must not be overwritten while either submission can read it.
   Shared images require correct GPU ordering/layout tracking; destruction must
   wait for the last use, including uses from both slots and the presenter.
   Check semaphore reuse against presentation ownership, not only a CPU slot.
2. Run existing `native_frame_context_test.cpp` and
   `native_submission_lifetime_test.cpp`, plus relevant descriptor/constant
   lifetime tests. These test the contracts; they do not prove every real
   Vulkan/Metal resource is correctly wired to those contracts. If a concrete
   integration hazard is found, fix/test it separately before device comparison.
3. Keep the current binary/settings fixed and run a short one-slot control,
   then two slots. Use bounded flight-recorder evidence for the diagnostic pair;
   do the performance pair with that recorder disabled and no debugger. Keep
   fences, watchdogs and corruption checks enabled in both.
4. Compare new-frame pacing, slot wait, CPU/GPU overlap where measurable, audio,
   memory and controller responsiveness. Exercise dense long views, fast camera
   turns, streaming, vehicles/fire and the previously troublesome scenes. Follow
   a successful smoke test with the matched pairs and ten-minute soak in P5.

**Keep/promote to candidate default** only if pacing improves repeatably with
no visual freezes, invalid-resource errors, fence timeouts, stale draws or
unacceptable latency/memory cost. One good minute is not enough. If GPU tracing
remains unavailable, an FPS gain can establish utility but not the exact overlap
mechanism. Revert to one immediately on a lifetime/corruption failure, retain
the first-failure evidence and repair that ownership bug rather than suppress it.

After acceptance, compare later CPU candidates against the accepted slot count;
do not keep comparing new two-slot builds to unrelated older one-slot scenes.
Until acceptance, two slots are a device-test candidate rather than an accepted
production default. The initial two-slot build delivered a user-reported solid
30 FPS but froze video while audio continued. Its flight trace isolated an
`Invalid Resource` failure: after slot 0 / submission 1648 completed, texture
views were released while slot 1 / submission 1649 was still executing.
Texture retirement now waits through the latest already-committed submission,
which adds at most one submission of quarantine without removing two-frame
overlap. A regression test covers that exact sequence. The preceding signed
one-slot app was copied to
`/private/tmp/theft4-p0b-one-frame-baseline/Theft4.app` before the normal build
directory was overwritten. Host preflight passed 33 focused test cases with
459,117 assertions. The corrected signed ARM64 candidate (SHA-256
`df914b60f653d1969bcd3bdd910bbbdaabc993b7b9f790a11d8bc5ddd64469d7`, UUID
`18BD2405-8D9B-32BB-8AB8-69C989FA4F53`) was installed in place and launched;
it also failed at frame/submission 4137. This rejects the texture-only change
as sufficient, not two-frame overlap as an achievable goal. The next
[buffer-quiescence candidate](THEFT4_TWO_FRAME_RESOURCE_LIFETIME.md) addresses
MoltenVK's global addressable-buffer declarations and batches cleanup at
all-native-slots-complete boundaries. Host checks passed 36 cases / 461,192
assertions and the signed Release build passed; device acceptance is pending.
Keep P1–P5 separate.

Sources: [Khronos frames in flight](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/03_Drawing/03_Frames_in_flight.html),
[Apple dynamic-buffer overlap guidance](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/TripleBuffering.html),
and [Khronos presentation semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).
Their synchronization principles apply; a tutorial's preferred buffer count is
not evidence that Theft4's current resource ownership is safe at that count.

## P1 — replace chained state hashing with a canonical fingerprint

### Implementation

- Extract the exact field coverage of `HashFixedFunctionState` into a small
  testable helper. Serialize fields/arrays into a deterministic contiguous
  representation, then hash once. Use the existing XXH3 dependency.
- Preserve integer widths and floating-point bit patterns, including signed
  zero and NaN payloads. Do not hash uninitialized padding or allocate a vector
  per fingerprint. Make the field list reviewable against the state definition.
- Update all capture/finalize/record consumers together. Search for persisted
  digests, external identities and tests before changing the digest algorithm.
  Version affected cache formats if any actually depend on this digest.
- Keep the mutation comparison and rejection/error path. Do not replace it with
  a token that merely assumes immutable state. Keep shader/texture identity
  hashes and lighting correctness mechanisms unchanged.

Files: SDK `src/graphics/gta4_native/graphics_system.cpp`, its fixed-state
definition and a proposed `native_fixed_state_fingerprint.h`; proposed
`tests/unit/graphics/native_fixed_state_fingerprint_test.cpp`, wired through
`tests/unit/CMakeLists.txt`. These two proposed files do not exist yet.

### Checks and decision

- Test every scalar/array field, differing object padding, explicit FP edge cases
  and seeded randomized states. Mutating representative bits must change the
  fingerprint; equality must not depend on storage layout.
- Inject a snapshot mutation in a test and verify the existing rejection still
  happens. Test the actual production helper, not a duplicate test algorithm.
  Old/new digest equality is not required; identical semantics are.
- Benchmark optimized ARM64 old/new helpers over realistic state distributions,
  consuming results to prevent compiler elimination. A Mac microbenchmark is a
  screening result, not an M5 iPad FPS result.
- Build Release, run the targeted existing lighting/fixed-function tests, then
  a brief device visual check and matched city A/B when authorized.

**Keep** if helper cost falls materially/repeatably, state checks still work and
frame/audio/visual behavior does not regress. **Revert or revise** if packing
cost erases the gain or field coverage is incomplete. Report a CPU-efficiency
win separately from a demonstrated frame-pacing win. Do not claim this modest
part of total CPU is sufficient by itself for locked 30.

## P2 — reduce repeated movement of native commands

### Implementation

1. Measure `sizeof(NativeCommand)`, moves, allocations and queue/batch sizes in
   a bounded diagnostic. Attribute bytes copied to actual command types.
2. First remove an unnecessary intermediate move if existing container/lifetime
   rules allow it. Do not introduce a pool just because pools sound faster.
3. If justified, prototype stable owned command packets in bounded storage and
   transfer cheap handles instead of repeatedly moving the whole object. Avoid
   trading copies for a fresh heap allocation/reference-count increment at every
   step. Retain the current implementation for controlled A/B.
4. Keep batch64 initially. Packet layout/ownership and batch size are different
   variables and should not change together.

Preserve queue ordering and bounds, condition-variable predicates, shader and
constant snapshots, generation protection, synchronous commands and shutdown.
Recycle packets only after their CPU owners release them; GPU resources still
follow their fences. Error/cancellation paths must not leak or reuse packets.

Files: native `graphics_system.h/.cpp`, optional new packet-storage helper;
SDK tests `native_submission_lifetime_test.cpp`, `native_frame_context_test.cpp`,
`native_lighting_transport_test.cpp`, plus focused transport tests as needed.

**Checks:** producer backpressure, wrap/reuse, aborted submission, resource
release while queued, frame clear, synchronous completion and shutdown. Use
host sanitizers where supported as correctness tests, never FPS benchmarks.
On device exercise rapid camera turns, fire/explosions, phone/TV and save/load.

**Keep** only with lower measured movement/allocation cost, bounded memory and
unchanged ordering/lifetime behavior. Reject any speedup accompanied by stale
textures, disappearing geometry, visual freezes or increased frame tail.

## P3 — reuse warm pipeline preparation and immutable state

Begin with `TryPrewarmDrawPipeline`, `ResolveRenderingTarget` and
`GetOrCreateDrawPipeline`. The current profile assigns 1.226 inclusive CPU-s to
prewarming, but does not prove all of it is redundant.

- Count repeated ready hits, genuine misses and invalidations for a bounded run.
  Inspect the existing lookup memo before adding another cache.
- Reuse a prepared ready result or add a cheap validated ready-hit path when
  the same draw is prewarmed and then recorded. Do not disable cold prewarming.
- Validate target/image lifetime, dimensions, formats, sample counts, layout,
  shader generation, primitive specialization and fixed state. A pending async
  pipeline is not a ready cached pipeline. Never reuse a result across a reset
  or destroyed/replaced resource solely because a guest handle matches.
- Separately consider constant-vector materialization and texture preparation
  if they remain hot. Reuse immutable versioned data, not writable guest pointers.

Files: native `graphics_system.cpp`, existing pipeline lookup helper(s),
`stateful_constant_state.h` only if constant work is selected;
SDK `tests/unit/graphics/native_pipeline_lookup_memo_test.cpp`,
`native_hotpath_cache_test.cpp`, `dirty_state_delta_test.cpp` and relevant
existing lighting tests. Add cases to the owning test targets in CMake.

**Keep** when warm CPU work decreases without incorrect cache hits, additional
cold compilation stalls or lost draws. Test target recreation, reflection/main
pass changes, shader/state changes and warm/cold starts. Never clear the user's
cache destructively: use an isolated test cache when authorized and necessary.

## P4 — reprofile, then choose only the remaining limiting branch

| Evidence after accepted changes | Next candidate | Required safeguard |
|---|---|---|
| AOT DSP remains a material CPU cost | Specialize `sub_829321A0` or the measured inner kernel in `ios/bridge/theft4_ios_audio_hotpaths.cpp` | Generated code remains the oracle/fallback; compare PCM, state, aliasing and strict FP behavior; no missing tracks/effects or sample-rate cut |
| Critical thread is runnable but not getting CPU | Narrow QoS experiment in SDK `src/core/threading_mac.cpp` / native worker initialization | Measure actual effective policy and delays; no global priority boost or assumed core pinning; audio must stay healthy |
| MoltenVK handoff/encoding dominates | A/B existing `vulkan_moltenvk_synchronous_queue_submits` | Keep the accepted slot count and identical output; distinguish CPU dispatch from GPU-completion waiting |
| Hot call boundaries remain | Selective ThinLTO in `ios/CMakeLists.txt`, then PGO only if worthwhile | Verify weak/strong hook binding, indirect function registration, boot/audio/saves; retain symbols and rollback build |
| GPU time/fence dependency dominates | Optimize the measured pass, redundant state/attachment work or texture traffic | Preserve lighting, AA, shadows, reflections and resolution; do not spend another pass optimizing unrelated CPU work |

No scheduler/driver/compiler experiment should be combined with P1–P3 for its
first comparison. The known background-submission/device-loss bug is a separate
correctness task: avoid app switching during foreground benchmarks, but do not
mistake that workaround for a lifecycle fix or overall stability acceptance.

## P5 — acceptance and release decision

Use the latest accepted build as A; introduce one candidate as B. After a short
smoke test, perform three matched A/B pairs on the 90-second route, alternating
order and restoring comparable starting/thermal conditions. Do this for promising
candidates, not every failed microbenchmark. No uninstall or user-data reset.

Record build identity, settings, route, cache/thermal state, frame count/interval
distribution, CPU work normalized to comparable workload, GPU timing coverage,
audio counters, memory behavior and visible defects. Report p50/p95/p99 and
>50/>100 ms stalls, not just a rounded FPS overlay. If the gain is within normal
baseline variation, label it inconclusive; do not manufacture a percentage.

Classify each experiment:

- **Frame-pacing win:** repeatable deadline/tail improvement without regressions.
- **CPU-efficiency win:** less CPU work but no demonstrated FPS change; useful
  headroom/power potential, not proof of improved frame rate or battery life.
- **Inconclusive:** variability or missing data prevents a decision.
- **Rejected:** correctness, visual, audio, memory or pacing regression.

After combining only accepted changes, run a ten-minute foreground city soak
including long views, driving/streaming, camera rotation and busy audio. Check
save/load and the opening cutscene. Compare matched visuals for geometry,
textures, shadows/reflections, effects and HUD. FIFO alone is not proof of
tear-free/steady presentation; obtain displayed-frame timing or label the
remaining presentation limitation explicitly.

**Locked-30 gate:** every scheduled new-frame deadline is met at 30 Hz within
the calibrated measurement precision during the stated acceptance workloads,
with no observed renderer hangs, visual corruption, audio starvation or new
memory growth. A run averaging 30 with missed deadlines is still near-30, not
locked. State the workload/device and test duration; do not claim all-game or
all-device guarantees from one iPad route. Where actual display timing is not
available, claim only the measured publication/completion result.

Test Boost separately afterward. If it misses the target, retain it as explicitly
experimental rather than silently reducing the reference scene quality.

## Handoff and record for each pass

Update `CHANGELOG.md`, `docs/IOS_HANDOFF.md` and a result entry here with:
candidate ID; source/configuration change; baseline/candidate UUID; settings/workload;
tests/capture coverage; median and tail comparison; CPU attribution; correctness
result; keep/revert decision; next hypothesis. Keep raw traces, saves and game
files private. Commit/push only when requested.

| Candidate | Status | Decision |
|---|---|---|
| B0 — audited 1080p Release build | Installed; CPU profile obtained; timing gaps documented | Reference, no locked-30 claim |
| P0b — two-frame overlap | Not tested in this pass; existing override available | First device A/B after lifetime preflight; one-slot default unchanged |
| P1 — canonical fingerprint | Not implemented | Next bounded implementation task |
| P2 — command transport | Not implemented | After P1 decision and recheck of hotspot |
| P3 — warm preparation | Not implemented | After transport decision or evidence-based reprioritization |

Relevant primary guidance: Apple's [CPU profiling and bottleneck analysis](https://developer.apple.com/videos/play/wwdc2025/308/)
and [Apple silicon game scheduling](https://developer.apple.com/videos/play/tech-talks/110147/).
The [audit's reference section](THEFT4_CPU_PERFORMANCE_AUDIT.md#relevant-primary-references-and-how-they-affect-this-plan)
also links LLVM and MoltenVK documentation. These guide the method; our own
matched measurements decide which Theft4 changes are worth keeping.
