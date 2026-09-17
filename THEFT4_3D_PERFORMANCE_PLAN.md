# Theft4 — intro and 3D performance execution plan

## Current execution order — September 16 evening

Use the [paced-30 implementation plan](docs/THEFT4_30FPS_IMPLEMENTATION_PLAN.md)
for the current native-renderer/1080p build, supported by the
[new CPU audit](docs/THEFT4_CPU_PERFORMANCE_AUDIT.md). User follow-up adds an
early one-vs-two-frame overlap A/B after resource-lifetime preflight. The first
P0b builds demonstrated near-30 FPS but the initial and texture-quarantine
candidates both failed with Metal Invalid Resource. Texture causation is not
proven. The next [buffer-quiescence experiment](docs/THEFT4_TWO_FRAME_RESOURCE_LIFETIME.md)
is installed with device acceptance pending. Two remains the test default and one the
retained rollback/control. Next code work, only after this decision,
is cheaper integrity fingerprints with checks retained, command transport, then
warm preparation, each independently measured.

The renderer, resolution, build identity and next-experiment descriptions below
are historical. In particular, they do not supersede the current native backend,
720p scene → 1080p FSR and 4× filtering. The historical reference uses one
frame slot; the current P0b candidate uses two.

## Earlier execution record

Latest checkpoint: 2026-09-16. Status: **diagnosis complete; implementation paused.**

The current next-experiment guide is
[THEFT4_GPU_DIAGNOSTIC_2026-09-16.md](THEFT4_GPU_DIAGNOSTIC_2026-09-16.md).
It supersedes the earlier assumption that black native-Metal replay established
cross-fork incompatibility: the replay leaves async compilation on, and its
XeniOS Vulkan control does not restore EDRAM. It also adds a trace-derived eligibility
check for the draw-bounds candidate, the upstream transfer-merging opportunity,
whole-pass render-area bounds, and the profiling GPU-condition caveat.

The September 15 baseline analysis below is historical. Later execution notes
record completed cache/audio/renderer work; do not repeat already resolved steps.

The user requested an intro-performance analysis, then asked to quit the app and
prepare the attack without implementing it. Theft4 PID 1528 was sent SIGTERM;
a subsequent device process listing confirmed it had exited. The working app
remains installed. No graphics fix, rebuild, installation, cache replacement,
or XeniOS modification was performed during this planning pass.

## 1. Objective and operating rules

Improve playable 3D throughput and frame pacing without losing the working
AOT boot, correct scene rendering, improved audio, or centered 16:9 presentation.
First practical target: stable 30 unique game frames/s in the opening and first
driving section, where the game permits it. This is a target, not a prediction.
Do not interpret 60 presentation callbacks as 60 independently rendered frames.

Execution order: preserve baseline → repair/verify cache preload → measure warm
scenes → optimize the measured limiting stage → validate the combined result.
Do not run every possible experiment before making progress. Take one coherent,
high-value change through build, device test, and a keep/revert decision before
moving on. Later passes are conditional, not a mandatory rewrite checklist.

Keep these invariants unless a later experiment explicitly calls for a change:

- AOT CPU execution; no runtime CPU JIT dependency.
- Existing Xenos → Vulkan → MoltenVK → Metal renderer, not a replacement backend.
- Existing game/TU8, saves, and user-owned assets. No asset distribution.
- 1280x720 drawable and 16:9 shell layout; unchanged guest projection.
- Current 48 kHz audio, inline XMA default, startup 32-block buffer, recovery
  eight blocks and 128-frame fade. Keep detailed audio tracing off.
- ARM64 ISA baseline; `-mtune=apple-m5` is not permission to require M5 instructions.
- Preserve all pre-existing dirty work. No blanket reset/checkout, no dependency
  upgrade, and no rebuilding shared XeniOS archives in place.

## 2. What the reconnaissance actually established

### Identity and evidence

- Repository HEAD: `6cadc5c9e376cba089b2587da1efc2a675a8eef4`, plus substantial
  existing local modifications; HEAD alone does not reproduce this build.
- Device: M5 iPad Pro 11-inch, iPad17,2, `test iPad`, iPadOS 27.0 (24A435).
- UDID: `YOUR_IPAD_UDID`; app: `com.theft4.bringup`.
- Installed/current Release executable UUID: `87A1CC77-4470-39C7-AB61-441EB0EE69B5`.
- Executable SHA256: `d2e6ee00a35f9d06e8963fb167a8356261cfd8fc8e449ecc813023dfd0fd9a5c`.
- Current app: `out/build/ios-device-release/theft4/Release/Theft4.app`.
- Baseline prior run: `out/device-audio-aspect-final-controlled/runtime-final.log`.
- New run: `out/intro-performance-20260915/runtime-final.log`, origin
  17:21:03.478; audio records reach 328.369 s. Screenshot `player-control.png`
  shows the vehicle, minimap, and driving instruction. User independently
  confirmed that the intro reached player control.
- Reproduction parser: `tools/analyze_intro_frame_log.py`.

### Throughput: coarse guest swaps, NOT displayed-FPS measurements

The production `IssueSwap` counter advances even when presentation is skipped.
Logs sample every 300 swaps and on outcome transitions; endpoint draw counts
are not interval averages. These data cannot produce valid display-frame p95,
p99, 1% lows, or a count of unique displayed frames.

| Phase/window from previous unprofiled run | Guest swaps/s | Endpoint observations |
| --- | ---: | --- |
| 2D opening, roughly 5–26 s | ~58 | 3–5 draws in most samples, then transition |
| Early sustained 3D, roughly 41–136 s | ~19–26 | ~1,100–2,600 draws; ~42–45 resolves |
| Later harbor/car portion, roughly 136–285 s | ~12–17 | ~2,800–5,900 draws; ~44 resolves |

The newer run shows ~14–20 swaps/s in much of early 3D and ~11–15 in later
windows. It is not a controlled A/B against the previous run: scene timing,
profiler activity, and unrecorded thermal conditions differ. Do not attribute
the entire difference to profiler overhead. Scene descriptions are supported
by intermittent screenshots, not a frame-by-frame annotated recording.

Previous run: 623 pipeline-creation log entries, 64 placeholder-skip transition
records. New run: 631 entries and 68 transition records. These are neither
unique-pipeline counts nor counts of all skipped frames. Source confirms a
placeholder draw can suppress presentation of the entire guest frame.

Audio remained much healthier in the new intro log: seven rebuffers and 23,040
recovery-silence frames (0.480 s) through 328.369 s; zero dropped blocks,
clipped, or nonfinite samples. This is a regression reference, not proof of
perfect sound in every workload. Earlier audio reports compare similar-length
but not frame-identical windows; their percentages are not controlled causal
benchmarks.

### Profiling limits and usable CPU evidence

`intro-metal.trace` ended after 112.117 s with “Device disconnected.” Exported
CPU, GPU interval, and displayed-surface tables contain **zero rows**. Its sole
thermal interval is “Unknown.” Therefore this capture supplies no usable GPU
utilization, GPU milliseconds, display FPS, or thermal-throttling conclusion.
The game continued after the profiling connection ended.

The subsequent nominal 30 s `player-cpu.trace`, captured at the gameplay
handoff, did contain samples. `player-cpu-summary.txt` was symbolicated against
the matching Theft4 UUID. Its sample timestamps span ~0.388–31.152 s. Instruments
reported overlapping system-library mappings; 14,265 of 106,991 Theft4 running
samples lacked stacks. Treat system-symbol and aggregate attribution cautiously;
do not silently redistribute missing samples. CPU seconds overlap across cores
and cannot be added as serial frame time.

Useful sampled hotspots:

| Thread / work | Sampled CPU time | Interpretation |
| --- | ---: | --- |
| Guest audio mixer + inline decode thread | 21.687 s | Still substantial CPU demand despite good audio delivery |
| GPU Commands thread | 17.904 s | Host graphics-command preparation is a material cost |
| `memmove`, leaf on GPU Commands | 2.938 s | Need caller attribution before choosing upload versus readback work |
| Base `WriteRegister`, leaf | 1.217 s | Per-register command overhead |
| `GetRegisterInfo`, leaf | 0.980 s | Diagnostic register metadata lookup worth checking |
| `UpdateBindings`, leaf | 0.928 s | Binding/state preparation cost |
| Vulkan `WriteRegister`, leaf | 0.869 s | Additional state tracking; cannot remove indiscriminately |

Several worker stacks include `MVKCommandEncoder::encode` and Metal resource
bookkeeping. This justifies examining translation/encoding cost; it does not
establish that GPU shader execution is the bottleneck. The stationary gameplay
profile is also not a substitute for profiles of the intro's wide shots.

### High-confidence cache-loading defect candidate

`src/graphics/vulkan/pipeline_cache.cpp` in the ReXGlue tree opens both
`*.vk.xpso` (pipeline descriptions) and `*.xsh` (guest shader code) with `a+b`,
then immediately reads the header without seeking to the beginning. The iOS
filesystem wrapper delegates directly to libc `fopen`.

A Mac/Darwin probe on the copied device pipeline cache returned:

```text
initial_position=19086 initial_read=0 rewound_read=12
```

The rewound header has valid XEPS magic, API 0 and version 0x20260228. On header
failure, existing code can truncate/reinitialize caches instead of preloading.
This is directly reproduced on the host, and strongly supported by iOS source;
the on-device corrected preload still must be tested. Do not claim it fixed.

Startup already calls blocking `InitializeShaderStorage` before the AOT entry;
the bridge marshals it onto the command processor and waits. We should repair
that path, not build a second preloader.

Preloading means reconstructing known GPU pipelines before gameplay from the
user's accumulated cache. It is not precompiling every possible shader in GTA
IV, not necessarily persistent native Metal binaries, and not CPU JIT. New
areas/effects will still discover new variants. First-run compilation cannot
be eliminated merely by fixing second-run cache reuse.

## 3. Benchmark contract — use for every pass

### Scene suite

- S0: copyright/slideshow/loading. Smoke test and low-3D reference, not main score.
- S1: first wide ship shot; record a reproducible camera/credit/dialogue landmark.
- S2: tight Niko/crew dialogue; same landmark each time.
- S3: wide harbor/cranes/city and end-of-intro car sequence; primary stress case.
- S4: first player-control view, vehicle stationary, fixed camera, 30 s.
- S5: 60–90 s repeatable driving route from a copied test save once controller
  control is available. Do not treat idle S4 as proof of driving performance.

Annotate the actual scene per capture. Do not compare the same elapsed second
when different builds advance the cutscene at different speeds. Preserve a
test save separately; do not overwrite the user's only save to reset a scene.

### Required record per run

Run ID; executable UUID/hash; exact source delta; game/TU; cache identity and
cold/warm definition; effective launch settings; device/OS; power connection,
low-power mode, display settings, initial/final thermal state; scene markers;
profiler/diagnostic settings; start/end and exit reason. Unknowns stay unknown.
Keep screen mirroring, screenshots, debugger, and heavy tracing out of timed
score windows. Use one screenshot outside the window for visual validation.

### Metrics and definitions

- Guest swap rate; successfully published *new* guest images; native present
  requests; actually displayed new images where observable. Report separately.
- Frame intervals: p50/p95/p99, >50/100/250 ms counts, longest stall. Define
  the source clock and whether values are publication or actual display times.
- Unique guest-frame IDs through the existing presenter mailbox/version or
  provenance; do not count repainting the same texture as a new game frame.
- CPU preparation/wait time; GPU execution time only from valid GPU timing;
  presentation wait/queue depth; pipeline creation/misses and placeholder skips.
- Draw/depth counts, resolve/readback bytes, upload bytes, cache misses,
  barrier/submission counts. Start with aggregated counters, not per-draw CSV.
- Audio rebuffer deltas and silence seconds, dropped/clipped/nonfinite deltas;
  memory high-water, thermal state, visible correctness and input response.
- Startup preload time is scored separately from in-game pacing. Do not hide a
  minute of startup work by reporting only improved in-game frame times.

### Fast comparison and acceptance

Screen each candidate with one matched A/B after warmup. Reject obvious
regressions quickly. For a promising performance change, use three matched
warm runs per variant with alternating order and comparable starting thermal
state. If variance swamps the result, narrow the scene and diagnose the source
of variance instead of repeating full intros indefinitely.

Keep a performance change when a selected primary metric improves by at least
~10% and more than observed run-to-run noise, or when it removes a verified
correctness/cache defect with no material performance regression. This is an
engineering gate, not a formal statistical confidence claim. No repeatable
>5% regression in another priority scene or clear p99/input-latency regression
without an explicit, documented user-acceptable tradeoff. Audio must remain
subjectively good and not show a reproducible increase in missing time.

For the eventual 30 FPS target: mean intervals near 33.3 ms, few >50 ms,
no recurring >100 ms stalls after warmup. Report actual results, not “full
speed” from a close-up. Revisit the target if game timing proves a different cap.

## 4. Ordered execution passes

### P0 — Freeze a reproducible known-good baseline

**Objective:** allow a fast, exact rollback before the next build.

**Work:** archive the signed app, matching symbols, relevant build cache and
effective flags, source diff plus untracked source files, nested dependency
revisions/diffs and linked archive hashes. Record current app-cache/save paths
with read-only inspection and back up exact test targets. A git commit hash
or top-level diff alone is insufficient in this dirty/nested tree.

**Checks:** baseline app identity matches this checkpoint; critical game/audio
settings recorded; signing can reuse the known working setup; no source or
save overwritten. Use an isolated output directory for later vendor experiments.

**Exit/rollback:** restorable baseline package and manifest. No need to rerun a
full architecture audit or clean-build the entire toolchain. Risk: low.

### P1 — Repair existing shader/pipeline preload, then prove reuse

**Dependencies:** P0. Highest-priority implementation, small scope.

**Files:** `glue/rexglue-sdk-main/src/graphics/vulkan/pipeline_cache.cpp`,
corresponding header only if counters need it; inspect
`src/core/filesystem_ios.mm`, `ios/bridge/theft4_startup.cpp`, and
`theft4_bootstrap_graphics.cpp`. Diagnostic fixture can live under `tests/ios`
or `tools`; do not change global OpenFile semantics just to fix two readers.

**Implementation:** explicitly seek to the beginning of both successfully
opened cache streams before reading headers. Handle seek/read errors explicitly;
do not label I/O failure as proven corruption. Retain current version/hash and
capability checks and append semantics. Log cache entries loaded, accepted,
rejected, shaders translated, pipelines created/failed, and preload duration
once per startup. Audit flushing/write-read transitions and the existing
truncate path. Avoid widening this into an all-new cache format.

**Tests:**

1. Existing-file Darwin position fixture; empty/missing file; valid shader and
   pipeline records; wrong version; truncated header/last record; bad record hash.
   Use copies in a temporary test directory, never live user data.
2. Corrected device launch with a backed-up valid cache: nonzero expected
   entries survive validation and create before the observed AOT entry.
3. Cold *application-cache* run through S1–S4, collect/flush cache; warm second
   and third launches through the same scenes. OS/driver cache may remain warm;
   do not call this fully cold Metal compilation or purge system caches.
4. Confirm matched pipeline identities hit the cache; separate startup creation
   logs from in-game creation. Zero new shaders is not required if scene variants
   differ. Investigate duplicate keys/version invalidation if reuse is weak.
5. Background/foreground or ordinary close/reopen verifies cached content is
   persistent; interrupted-write recovery uses disposable copies/test cache.

**Gate:** valid cache isn't discarded, supported known pipelines are ready
before gameplay, recurring identical compilation/placeholder stalls decline,
startup stays responsive and bounded, no new visual/audio failures. If preload
is slow, expose progress in the existing shell; do not block UIKit's main thread.

**Failure/rollback:** hang or memory spike in preload, repeated recompilation,
bad/incompatible records, shutdown flush races. Restore only this pass's delta
and copied test cache if necessary; preserve original caches for diagnosis.
Expected benefit is fewer hitches, not guaranteed higher steady-state FPS.
Do not disable incomplete-frame protection to improve a counter.

### P2 — Obtain lightweight frame pacing and classify the bottleneck

**Dependencies:** P0; score warm performance after P1. Measurement design may
be prepared alongside P1, but its overhead must be tested separately.

**Files:** Vulkan `command_processor.cpp`/header, `shared_memory.cpp`,
`src/ui/vulkan/vulkan_presenter.cpp`, iOS lifecycle bridge, and the existing
log/profile summarizers. Reuse presenter frame provenance/mailbox versions.

**Work:** add bounded, opt-in in-memory frame records and one-second aggregates
for guest swap/publication/present, skipped frames and phase timings. No
per-draw synchronous writes or allocations on audio/graphics hot threads.
Flush outside timed windows. Capture thermal-state changes through the app if
the external profiler cannot supply them. Verify the production Vulkan path is
instrumented, not the obsolete one-shot direct-Metal probe.

Try one short Metal trace on S3/S4 and immediately check exported tables for
rows and the correct process. If it fails again, stop repeating full empty
traces: use lightweight publication timing plus a short CPU profile and a
focused GPU capture. Timestamp-query support and drawable presentation hooks
must be verified in the actually linked MoltenVK before use; declare metrics
unavailable if unsupported. Do not equate queue-present return with scanout.

**Tests/gate:** diagnostics off/on/off with identical scene and settings; aim
for <2% overhead, investigate >5%. Verify repeated repaint vs new-frame counts,
monotonic timebase, no invented p99 from sparse log milestones, bounded memory,
and no realtime audio allocations. Produce at least one useful S3/S4 attribution.

**Branch decision:**

- Pipeline work/placeholder misses dominate → finish P1; tune preload concurrency
  only if measured CPU contention warrants it.
- CPU command generation/encoding dominates → P3, then P4 if copy cost matters.
- GPU saturated on depth/fill/render passes → P5/P6.
- GPU idle while guest CPU/mixer or fence waits dominate → P7 or targeted P4.
- Work completes fast but presentation is uneven → P8.

Timebox a failed external-profiling route to one retry, not a day of tool repair.

### P3 — Reduce command-processing and state-translation CPU cost

**Files:** ReXGlue `src/graphics/command_processor.cpp`, `register_file.cpp`,
Vulkan `command_processor.cpp`, relevant register/binding headers.

**Candidate 1:** `GetRegisterInfo` runs for each valid-index register write to
support a debug warning. Check logging macro expansion/Release assembly. If it
is diagnostic-only, avoid the lookup when that diagnostic is disabled. Preserve
register storage, volatile visibility, scratch writeback, coherency flags and
all Vulkan dirty-state tracking. This is a bounded initial win, not a 2× claim.

**Candidate 2:** use current stacks and counters to locate redundant constant
copies/descriptor updates. Elide only state proven unchanged; audit aliasing,
descriptor lifetimes, memory invalidation and draw boundaries. Do not skip a
write merely because its value looks unchanged if it triggers a side effect.

**Tests:** register sequences with scratch writes, LUT/coherency, out-of-range
handling and repeated values; effective state/side effects agree with baseline.
Compare S1–S4 output, CPU ms/new frame, draw counts and binding updates; resample
GPU Commands with the new binary UUID. A smaller profile leaf without better
throughput or headroom isn't proof of an FPS improvement.

**Gate/rollback:** verified semantics plus measurable reduced cost, no missing
geometry/materials or pacing loss. Separate the two candidates for attribution.
Risk: low for log gating, medium for binding/state elision.

### P4 — Reduce uploads/readbacks and synchronization, preserving coherency

**Files:** Vulkan `shared_memory.cpp`, `command_processor.cpp` (`IssueCopy`,
`EndSubmission`, queue waits), texture/render-target caches and relevant headers.

**Evidence to acquire first:** attribute GPU-thread `memmove` samples to callers;
count bytes, dirty pages, resolves that really read back, cache misses, GPU
wait duration and submissions per new frame. The ~44 resolve calls are NOT
automatically 44 CPU readbacks or 44 stalls.

**Strategy:** fix redundant dirty-range uploads or merge safe adjacent ranges;
reuse buffers/descriptors only within completion-safe lifetimes. For fast
readback, retain the existing alternating-buffer design and prove which guest
consumers require the bytes before changing copy/wait policy. Optimize the
dominant measured caller rather than replacing all memory copies.

**Tests:** first-use cache miss, warm reuse, destination/address/size change,
buffer eviction, overlapping writes and CPU consumption. Validate output bytes
on bounded samples plus S1–S5 visual/game progression. Compare bytes/frame,
waits/frame and actual frame time. No geometry flicker, stale textures, loading
hangs or lost writes allowed.

**Do not:** globally disable readback, remove fences, copy from in-flight buffers,
or assume unified Apple memory eliminates guest/GPU ordering requirements.
Risk: medium/high. Roll back on the first reproducible coherency failure.

### P5 — Recover visibility/occlusion efficiency if draw inflation is material

**Files:** `ios/bridge/theft4_startup.cpp`, Vulkan occlusion code and query
resource lifetime structures; consult XeniOS's corresponding implementation.

Current real occlusion is off because the implementation waits synchronously
for query results. Conservative fake results may increase work, but high draw
count alone does not prove that this explains our wide-scene slowdown.

**Strategy:** inspect the guest query-consumption contract and compare matched
scene draw/depth counts with XeniOS. A bounded diagnostic using the existing
`THEFT4_REAL_OCCLUSION=1` may quantify draw reduction versus stalls; never ship
that toggle as an assumed speedup. If worthwhile, design completion-safe,
asynchronous result delivery that satisfies guest polling/visibility semantics.
Unavailable results must not falsely hide geometry; conservative fallback may
be used only where consistent with the guest contract.

**Tests:** camera turns/disocclusion, near occluders, ship/city, movement/driving,
reused query slots and wraparound; no early result publication or visible pops.
Measure both reduced draws and total wait/frame cost. Stop if safe behavior
costs more than it saves. Risk: high; not part of the first small fix bundle.

### P6 — Optimize actual GPU work / MoltenVK configuration, conditionally

**Files:** Vulkan render-target and texture caches, presenter, shader translator
only if identified; `ios/bridge/theft4_startup.cpp`, `ios/CMakeLists.txt`.

**Strategy:** use a valid GPU capture/timeline to identify depth/shadow passes,
blits/resolves, bandwidth, render-pass breaks and shader cost. Try one supported
configuration at a time (for example the existing dynamic-vs-legacy render-pass
control) with independently valid caches. Preserve the existing host RT route
as baseline; FSI/native Metal replacement is not an automatic next step.

Verify an actual internal-render-resolution control before proposing a 540p
mode. Changing only CAMetalLayer/drawable size does not guarantee lower Xenos
scene-rendering cost. Quality tradeoffs may be useful for playability, but keep
them selectable, screenshot matched scenes, retain default-quality rollback,
and quantify gain instead of silently removing effects.

MoltenVK archive dependencies currently come from the sibling XeniOS build.
If a vendor experiment is justified, build isolated copies and point only
Theft4 at them. Do not replace archives used by the working XeniOS app/project.
Check options against the pinned source, not guesses about current defaults.

**Gate:** real frame-time benefit and documented visual/memory effects. Never
disable synchronization/validation-required correctness to inflate FPS. Heavy
validation and GPU frame capture runs are correctness runs, not scoring runs.
Risk: low for reversible supported settings, high for shader/backend changes.

### P7 — Recover AOT CPU / mixer headroom if it limits graphics

**Files:** `ios/bridge/theft4_ios_audio_hotpaths.cpp`, current gain-ramp helper,
generated AOT routines and transition hooks; native XMA path only if profiling
points there. Avoid broad regeneration of working generated code.

The new profile still spends substantial time in `829321A0`, `82935BD0`,
`821997F8`, `82936CE0` and related mixer code. Sounding good does not mean audio
has zero CPU cost. Select ONE measured hot routine for a native/NEON equivalent
only after confirming its semantics and benefit. Preserve existing rounding,
aliasing and state transitions; retain scalar fallback and startup control.

**Tests:** differential inputs including in-place/unaligned/edge cases appropriate
to the real routine; strict numeric behavior or explicitly bounded justified
error; real dialogue/radio/effects workload, full-intro and S5 audio metrics.
Confirm reduced CPU/frame or increased 3D throughput with no audio regression.
Do not lower sample rate or mute tracks as the first remedy. Risk: medium/high.

### P8 — Presentation and frame-pacing policy

**Files:** production Vulkan presenter and submission path, iOS lifecycle/view
shell. Only touch direct-Metal presenter submission logic if it is actually on
the production path; much of it is bring-up infrastructure.

**Strategy:** follow one unique guest frame through publication, mailbox,
acquire, queue submission and display; separate repeated frames from dropped
new ones. If presentation is the limiter, test supported present modes and
bounded frames in flight one at a time. Consider a 30 FPS policy only after
confirming guest timing and that it does not stall audio or change game speed.

**Tests:** queue depth, p95/p99, input-to-visible response, transitions, rotation,
background/foreground and resumed rendering. A smoother counter with more
input latency, stale frames, or slowed simulation is not a win. Risk: medium.

### P9 — Combined acceptance, thermal soak and compatibility

**Dependencies:** only the candidates kept by prior passes, combined explicitly.

1. Clean application-cache run and two warm full intros; reach player control.
2. Three scored warm scene suites for combined vs preserved baseline; alternate
   order and record thermal state. Do not sum isolated percentage gains.
3. 15-minute active driving/audio soak including turns, traffic, dialogue,
   radio and heavy view distance. Controller/user input is required for this;
   label unattended stationary coverage accurately if unavailable.
4. Three foreground/background/resume cycles, normal close/reopen, save/load
   on copied test data, cache persistence, controller reconnect and aspect.
5. Memory stays bounded after warmup; monitor stalls, thermal deterioration,
   unexpected exits, missing textures, corrupt audio and delayed input.
6. A19/M4 compatibility: inspect produced ISA/deployment settings, but reserve
   runtime/performance claims for actual tests on those devices. Simulator can
   check lifecycle/cache handling, not predict physical GPU performance.

**Gate:** reproducible benefit in heavy scenes, preserved successful boot/game
state, no new correctness or sustained audio failure, clear known limits.
Stop the app after data collection unless the user wants to keep playing.

### P10 — Freeze the improved build and hand off

Archive signed app/symbols/manifest and source deltas; update handoff with exact
run IDs, settings, per-scene results, failed experiments and rollback controls.
Document cache warmup UX and remaining cold-first-run hitches. Keep a short
user-facing result: what improved, where it remains slow, what was traded off,
and the single next highest-value task. Do not call the port “optimized” from
one successful intro.

## 5. First authorized execution session: concrete batch

1. P0 baseline archive (minutes, not another reconnaissance phase).
2. Implement only P1's reader correction and compact preload accounting; build
   incremental Release and run cache fixture checks.
3. Install once, warm the application cache with a full intro, then relaunch
   for the same scenes. Inspect cache reuse, startup duration and placeholder
   presentation loss. Stop if cache persistence is still broken.
4. Add/use P2's minimum frame measurements only where existing evidence cannot
   answer the comparison. Obtain one useful heavy-scene CPU/GPU attribution.
5. Present the result and choose the next branch. The likely next small target
   is P3 diagnostic register lookup, but measured P2 evidence determines order.

Do not automatically execute P4–P8 as a single speculative batch. Bigger strides
mean fewer low-value detours and full device validation of a coherent change,
not mixing five unrelated changes whose effects cannot be separated.

## 6. Test matrix and decision ledger

| Test | Baseline/control | Candidate | Pass/fail record |
| --- | --- | --- | --- |
| Cache reader fixture | Existing behavior on disposable valid cache | Explicit initial seek | Header/records recognized, bytes preserved |
| Cache reuse | Same-scene baseline startup/in-game creation split | Second/third fixed-cache launch | Known keys preload; fewer in-game misses |
| Intro throughput | Warm S1/S2/S3, unprofiled | Same warm scenes | New-frame interval distribution + visual/audio checks |
| CPU hotspot | Short scene-matched sample, exact UUID | Candidate sample | Cost per new frame down, wait not merely relocated |
| Upload/readback | Baseline bytes and waits | One coherency-safe optimization | Correct data + lower dominant cost |
| GPU/settings | Same scene/cache/quality | One supported setting | Valid GPU timing + measured output, no artifacts |
| Sustained gameplay | Fixed camera then active route | Combined kept changes | Thermal/memory/audio stable, genuine controller response |
| Failure recovery | Saved baseline app/cache | Cache error/resume/relaunch | No hang or destructive loss of user data |

For each candidate record: hypothesis, touched files, primary metric, control
settings, run IDs, result range, correctness observations, keep/revert/defer,
and next action. Do not require broad test-suite construction before the first
cache fix; use focused fixtures and the actual game to guard the risky behavior.

## 7. Execution command reference (not run by this plan)

Use the existing configured Release Xcode project and preserve its signing
configuration. Inspect effective build settings before invoking a rebuild;
the configure preset alone does not encode every current local override.

```sh
# Repository: /path/to/developer/LibertyRecomp
env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcodebuild -project out/build/ios-device-release/LibertyRecomp-ALL.xcodeproj \
  -target Theft4 -configuration Release -sdk iphoneos \
  -allowProvisioningUpdates build

env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcrun devicectl device install app \
  --device YOUR_IPAD_UDID \
  out/build/ios-device-release/theft4/Release/Theft4.app

env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcrun devicectl device process launch \
  --device YOUR_IPAD_UDID --terminate-existing --activate \
  com.theft4.bringup --theft4-start-game

python3 tools/analyze_intro_frame_log.py PATH_TO_COPIED_RUNTIME_LOG
```

Do not relaunch while a user is playing without coordinating. New benchmark
artifacts should use unique run directories. Capture commands need device
service access; a disconnected/empty Instruments trace is not a performance
measurement. Revalidate tool syntax and process identity before terminating.

## 8. Explicitly deferred

Native Metal renderer rewrite; CPU JIT; mass AOT regeneration; blanket fast-math;
M5-only ISA changes; automatic lowering of audio quality; global readback removal;
disabling incomplete-frame protection; system-cache deletion; XeniOS changes;
distribution of game-derived cache/assets. None is necessary for the next pass.

## 9. Execution ledger — 2026-09-16

- P0 baseline/cache archives created under `out/perf-baseline-20260916/` and
  `out/perf-cache-backup-20260916/`.
- P1 cache reader/preload correction kept; warm storage preload created all 332
  requested pipelines in the representative run.
- P2 opt-in GPU timing kept. CPU preparation/encoding was small relative to
  completion waits in heavy 3D. Normal launches do not enable the clocks/logs.
- One-submission-per-frame candidate rejected (2–4% slower).
- Real direct-host resolve candidate rejected as the default: correct output and
  fewer EDRAM dumps, but no frame-rate win. It is compile- and launch-opt-in only.
- Release/no-debugger/no-diagnostic build restored, installed over existing app
  data, and launched normally on the physical iPad.

**Resume here:** perform the P2/P4 scene-matched Metal capture that attributes
texture uploads/cache misses, render-pass/barrier churn, command-buffer duration
and translated draw count. Use that result to choose one targeted fast path.
Do not revive the direct-resolve or one-submission candidates without new evidence.

### Diagnostic checkpoint and next moves — 2026-09-16, paused on request

User requested one or two diagnosis passes, a plan, then STOP. The two passes
below are complete. No renderer fix from this checkpoint has been implemented;
no new binary was built or installed. The existing FSI environment override was
used for one diagnostic launch, then Theft4 PID 2593 was stopped. Device process
listing confirms both Theft4 and MetalReplay are absent. Normal manual launches
use the established host-render-target Release path. Game files/saves remain.

#### Pass 1: FSI correctness — rejected

Fresh FSI test on the now-working AOT build used
`THEFT4_RENDER_TARGET_PATH=fsi`, `THEFT4_DIAGNOSTICS=1`, and
`THEFT4_GPU_TIMING=1`. The prior FSI test predated the working 3D fix, so this
was a necessary revalidation rather than evidence that the old issue persisted.

Evidence: `out/perf-fsi-20260916/runtime.log`, `fsi-1875.png`, `fsi-1918.png`.
2D works; Niko geometry appears, but broad rectangular bands, texture-like
overlays, and black output corrupt the 3D. User independently confirmed it.
Swap 2100 has a fully black sampled swapchain despite 2,491 submitted draws and
no pipeline placeholders. The later log also contains cold pipeline stalls;
this is not a valid performance comparison. Reject FSI as a shipping option.
Its corruption has not been localized to the translator, EDRAM addressing,
MSAA packing, or MoltenVK interlock semantics. Do not claim any one cause yet.

#### Pass 2: actual Apple GPU attribution — rendering dominates this sample

Evidence: `out/perf-p3-20260916/game-performance.trace`, `encoders.xml`,
`gpu_intervals.xml`, `submissions.xml`, `summary-correlated.json`.
`tools/summarize_ios_game_trace.py` now joins command buffers to Apple frame IDs
and encoder labels, and distinguishes elapsed span from active interval union.
It is explicitly allowed in `.gitignore` so the reproducible analyzer can be
reviewed/committed later. No private trace data is added to source control.

The trace records 10 seconds with meaningful Metal rows concentrated in its
last approximately one second. It is NOT a full-intro or unprofiled benchmark.
The recording's exact scene is not proven by an accompanying screenshot.
Eight GPU buffers have no matching submission row (capture boundaries).
Boundary frames are partial, and encoder attribution is incomplete.

- 23 Apple submission frame groups; median 1,085 encoders and 7 submissions.
- Median CPU encoder duration per grouped frame: 15.84 ms.
- Correlated GPU frame span: median 45.20 ms including gaps; active union:
  median 36.38 ms. Neither is equivalent to display frame cadence by itself.
- Matched interior frames (IDs 3..15 with GPU rows, 12 samples): median GPU
  active union for dynamic render encoders 31.07 ms, compute dispatches 6.41 ms,
  buffer blits 1.24 ms, buffer-to-image blits 1.15 ms. Categories can overlap;
  do not add them to claim total frame time.
- Median CPU-to-GPU latency per command buffer: 90.33 ms. This is queue delay,
  not shader execution time, and does not prove that translation alone is slow.

Source evidence supporting the next experiment:

- `src/graphics/util/draw_extent_estimator.cpp:27` defaults
  `execute_unclipped_draw_vs_on_cpu` to **false**. The sibling XeniOS source
  `src/xenia/gpu/draw_extent_estimator.cc:24` defaults it to **true**.
- `EstimateMaxY` only invokes it for appropriate unclipped draws; by default
  it restricts CPU interpretation to the unbounded-scissor case. Unsupported
  vertex shaders conservatively return the full extent.
- `src/graphics/pipeline/render_target/cache.cpp:571` uses that estimate to
  determine EDRAM ownership ranges. Overestimation can therefore cause extra
  render-target transfers. The number of qualifying GTA IV draws is UNKNOWN.
- `VulkanCommandProcessor::SubmitBarriers` ends the render pass whenever it
  has pending barriers. Texture loading does compute conversion and a
  buffer-to-image copy; both force render-pass termination. This explains a
  possible mechanism for pass churn, not its measured frequency by cause.
- Dynamic rendering uses full `framebuffer->host_extent` with LOAD/STORE.
  Actual attachment traffic and whether the driver trims it need measurement.

#### Ordered next moves (planned only; resume on user instruction)

1. **A/B existing draw-bounds estimator on the correct host renderer.** Add an
   opt-in launch setting in `ios/bridge/theft4_startup.cpp`; keep default off
   until measured. Reuse `DrawExtentEstimator` and its conservative fallback.
   Add bounded counts for qualifying draws, successful reductions, reduced
   EDRAM ownership/transfer ranges, and CPU time. Keep the with-scissor option
   off initially. If the estimator never applies, stop this candidate promptly.
   Warm both runs, compare the same intro checkpoints and first driving state,
   screenshots/audio, GPU work and unprofiled presented-frame intervals. Reject
   any corruption or CPU regression. This is the first recommended experiment,
   not a claim that changing the default will fix 30 FPS.
2. **Attribute and reduce render-pass breaks.** In Vulkan command processor,
   render-target cache, shared memory and texture cache, count breaks caused by
   attachment changes, ownership transfer, uploads, memexport and submission.
   Give diagnostic encoders/pass groups meaningful labels. Obtain a bounded
   representative capture at a known visual checkpoint. Optimize the dominant
   *proven* redundant transition; preserve all real data hazards. Candidate
   work includes batching independent texture preparation or ownership
   transfers before a draw group. Do not simply delete barriers.
3. **Bound attachment work if the capture proves wasted regions.** Inspect
   full attachment extent versus guest draw/transfer bounds and tile load/store
   work. Consider render-area bounds only with coverage across every draw in a
   pass, resolve use, depth/stencil and later reads proven. Do not use DONT_CARE
   as a generic speed switch or shrink areas around only the first draw.
4. **Return to native Metal only after an actual parity oracle.** The later
   visible XTR (`out/metal-replay-20260916/visible-scene.xtr`, 1,960 draws) has
   correct original output `vulkan-exact-2060.png`, but replay through BOTH
   XeniOS Metal and its Vulkan control is black. That implicates cross-fork
   trace/capture compatibility, not Metal specifically. Repair/revalidate
   trace state and readback before comparing warm timings or integrating it.
5. **Final acceptance remains full intro through playable state.** Confirm
   visible, unique game frames paced at 33.3 ms under ordinary Release launch,
   no debugger/validation/captures, unchanged 720p/16:9/detail/effects, intact
   audio, no hangs or visible tearing/stutter. Use repeated warm runs and a
   sustained thermal run. Current evidence does NOT satisfy this goal.

Do not resume automatic builds, launches, FSI repairs or backend integration
from this checkpoint until the user requests continuation.

### User-requested long-shot probe: native Metal replay (initial checkpoint; superseded above)

`tools/ios-metal-replay/` builds an isolated iOS app using the public XeniOS
native Metal backend, with a null CPU backend and no game executable/JIT. A fresh
Theft4 XTR containing 880 draws completed shader translation and 1280x720 output
on the M5. Evidence is private under `out/metal-replay-20260916/`.

Both the native Metal output and nearby original capture are nearly black;
the current trace trigger caught an early transition. This is only a trace
integration proof, not correct visible 3D or improved FPS. Cold replay including
compilation/readback/file output was 7,231 ms, not a steady-state measurement.

Next gate: later visible scene + exact original output, renderer parity, then
warm identical-trace GPU timing. Keep the production backend unchanged unless
that comparison justifies an integration effort. Both Theft4 and the separate
replay app were stopped after this experiment; user game data remains intact.

## 2026-09-16 native GTA IV renderer checkpoint

This checkpoint supersedes the earlier instruction above to defer backend
integration. Repository forensics found a mature GTA-IV-specific Vulkan
renderer in `glue/rexglue-sdk-main/src/graphics/gta4_native/`. Unlike the
generic Xenos command processor, it consumes the title's known graphics hooks
and cached native shaders. It remains Vulkan -> MoltenVK -> Metal, so this is a
game-aware renderer change rather than a speculative direct-Metal rewrite.

The renderer now compiles and links into the iPhoneOS ARM64 Release app behind
the opt-in launch selector `THEFT4_GRAPHICS_BACKEND=native`. The generic
renderer remains the default and fallback. The physical M5 iPad accepted all
required Vulkan features, created a 1280x720 three-image swapchain, selected
`gta4-native`, loaded 1,356 cached shaders (11,599,741 SPIR-V bytes), applied
TU8, and entered title-specific 3D/deferred-light rendering. There was no
startup crash. A bounded log sample advanced frames 865 through 874 in roughly
0.31 seconds, consistent with the configured 30 FPS limiter, but visual
correctness and sustained playable-state pacing still require a witnessed,
ordinary Release run before acceptance.

Known follow-ups:

- Package or deliberately disable the optional native font-atlas assets; the
  first probe logged missing font image paths.
- Reduce the extremely verbose native diagnostic logging before performance
  comparison; it is not representative of a shipping run.
- Run a visually witnessed A/B through the same intro/playable checkpoint,
  comparing the generic backend with the opt-in native backend.
- Measure presented-frame cadence and GPU time without debugger, validation,
  trace capture, or verbose diagnostics. Do not infer sustained 30 FPS from
  the short frame-limiter sample.
- Preserve the generic path until native output parity, stability, audio, save
  loading, and controller behavior are verified.

Device hygiene is a hard requirement: Theft4 must remain terminated whenever
a device test is not actively collecting evidence. Every future probe must be
bounded as launch -> capture result -> terminate -> verify no Theft4 process.
The app was terminated and absence verified after this checkpoint.

## 10. Retail-fidelity native renderer measurement — 2026-09-16

The first native-renderer probe exposed desktop-enhancement defaults that were
far above the original Xbox workload: 1080p mirror/water reflections, 1024-square
environment reflections, doubled shadow distance, forced highest LOD, tripled
draw distance, and a 20,000-reference drawable limit. The iOS shell now uses the
original resource sizes and title-equivalent limits while keeping 1280x720 output,
16:9 presentation, SMAA, vsync, and a 30 FPS guest cap. Optional vector font
atlases are disabled because their replacement assets are not packaged; stock
game fonts remain active.

High-volume native resource/resolve/declaration trace messages are now gated by
the explicit native-trace category. This removes diagnostic formatting and I/O
from normal Release runs without deleting the diagnostic path.

Both unsigned and development-signed ARM64 iPhoneOS Release builds linked. The
latest bounded M5 iPad run selected `gta4-native`, created a 1280x720 three-image
swapchain, loaded 1,356 cached shaders (11,599,741 SPIR-V bytes), applied TU8,
and crossed into title-specific 3D/deferred rendering.

Presentation milestones provide the following direct cadence evidence:

- present 900 at 13:17:47.092 to present 1024 at 13:17:51.227:
  124 frames / 4.135 seconds = 29.99 FPS;
- present 1024 at 13:17:51.227 to present 1200 at 13:17:57.096:
  176 frames / 5.869 seconds = 29.99 FPS.

The 3D transition began inside these windows. Audio reached block 8,192 with
zero underrun frames, rebuffers, dropped blocks, clipped samples, or non-finite
samples. `VK_SUBOPTIMAL_KHR` was reported for the UIKit layer's current extent,
but presentation remained active; it is not a queue-present failure.

**Acceptance is still pending.** This exact retail-fidelity build has not yet
been visually watched through the full 2D intro, 3D cutscene, and first playable
state. The next bounded run must confirm correct pixels, no tearing/corruption,
the on-screen distinct-frame counter at the heavy scene checkpoints, and healthy
mixed audio. Then terminate the app and verify process absence immediately.

The full public implementation record, switches, file map, rejected experiments,
and evidence limitations are maintained in [CHANGELOG.md](CHANGELOG.md).
