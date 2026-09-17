# CPU-first performance audit — 2026-09-16

Execution follow-up: [paced-30 implementation plan](THEFT4_30FPS_IMPLEMENTATION_PLAN.md).
It translates these findings into ordered changes, tests and keep/revert gates.
User follow-up: that plan now includes an early one-vs-two-frame overlap A/B.
One slot remains the unchanged rollback baseline, not a permanent prohibition
on testing two. No such new test has been run by this audit/planning pass.

## Scope and decision

Target: sustained, correctly paced **30 distinct game frames/s**, with the existing
visual quality, physics, audio, input and save behavior. This pass audits the
installed native-renderer build, not the older generic Xenos/PM4 renderer.
It does not authorize a renderer rewrite, reduced draw distance, lower-resolution
shadows, dropped simulation work, unsafe resource reuse or broad fast-math.

**Do not yet call this CPU-bound or claim a locked 30 FPS.** CPU consumption,
critical-path CPU time, scheduler delay, GPU service time and fence waits are
different measurements. A busy noncritical thread can waste power without
limiting frames; a sleeping producer may be blocked behind a GPU-limited frame.

The safe route is to remove measured redundant work while preserving semantics,
then compare identical workloads in ordinary Release launches. The installed
build already contains substantial optimizations. Repeating those changes or
using pre-native-renderer profiles would produce misleading priorities.

## Build and measurement identity

- Source base: `5ca8635b430749cd1b09029b3cb2aa4c731c8dcc` plus the current uncommitted
  launcher/output and earlier changes. This is not a clean-HEAD benchmark.
- Installed executable UUID: `1AF4E60E-45D0-34CB-B9D5-643120A8515E`.
- Executable SHA-256:
  `1368354c3ec751eded3027cf606927b6d1e2650fd6c72a4723137f4b9c80cab5`.
- 11-inch M5 iPad Pro; Xcode 27.0, build 27A266a. Device data and captures stay
  private. No game data or raw traces belong in this public report.
- Project: `out/build/ios-device-release/LibertyRecomp-ALL.xcodeproj`;
  target `Theft4`, configuration `Release`.
- Artifact: `out/build/ios-device-release/theft4/Release/Theft4.app`.
- Cached C/C++ Release flags: `-O3 -DNDEBUG`; ARM64 tuning `apple-m5` through
  **`-mtune`**, not an M5-only `-mcpu` instruction-set requirement.
- AOT game and native backend are enabled; legacy direct resolve is disabled;
  optional ThinLTO is currently off.
- Recompiled game uses strict floating-point behavior and no strict aliasing.
  `gta4_register.cpp` alone deliberately uses `-O0 -fno-global-isel` for the large
  startup registration function. This is not an unoptimized game-code build.
- Line tables support symbolication; their presence does not mean per-frame
  instrumentation. The Xcode Run scheme can attach LLDB, whereas an ordinary
  app-icon launch has no LLDB attachment. Keep GPU API validation/capture off
  for performance acceptance.

## Latest pre-profile evidence

The previous session's actual game log is
`Library/Application Support/Theft4/startup/runtime.log`. The similarly named
file in its parent directory is old setup output, not the gameplay log.

That session selected **Experimental FSR Boost**, not the 1080p reference:
scene `1280×720`, output `2416×1359`, EASU and RCAS both active, frame cap 30,
FIFO presentation, one native frame slot. Boost has about 58% more output pixels
than 1080p; this is not a claim of 58% more total GPU work or frame time. Scene
resolution remains 720p. The shader cache loaded 1,356 entries.

Sparse unique-presentation milestones gave these coarse rates:

| Local interval | New successful presentations | Approximate rate |
|---|---:|---:|
| 21:53:12.112–21:53:22.108 | 300 | 30.01/s |
| 21:53:22.108–21:53:32.784 | 300 | 28.10/s |
| 21:53:32.784–21:53:44.250 | 300 | 26.16/s |
| 21:53:44.250–21:53:54.724 | 300 | 28.64/s |
| 21:53:54.724–21:54:05.284 | 300 | 28.41/s |

These are successful unique content handoffs, **not measured scanout intervals,
frame-time percentiles or an annotated heavy-city benchmark**. They establish
that some windows missed the target, not which subsystem caused the misses.
The last audio summary before deactivation reported zero underruns, rebuffers,
drops, clipping and nonfinite samples.

Lifecycle order matters: pause at 21:54:08.269, background at 21:54:09.175, then
the first GPU wait/publication failure at 21:54:09.648. This is not evidence that
CPU overload or Boost caused a foreground failure. The known background GPU
submission/device-loss issue remains a separate correctness task.

`VK_SUBOPTIMAL_KHR` does not establish a per-frame swapchain recreation defect:
the iOS path guards reconnection using the prior optimal/suboptimal state. Do
not change fixed-resolution presentation just to remove this expected status.

## Live CPU capture — completed after the user's in-game cue

Recorded with Time Profiler in deferred mode, local 22:11:17.594–22:11:38.597,
21.003 seconds including trace start/stop. The user confirmed city gameplay.
Startup and presentation logs confirm 720p scene → **1920×1080 FSR**. No app
rebuild, install, LLDB attachment or settings mutation was performed. Filtering
was requested at 4×; output extent is independently confirmed in the log.

The existing symbolication utility verified the executable UUID before using
the local binary. There were **58.853 sampled running CPU-seconds across 27
Theft4 threads**, approximately 2.8 occupied core equivalents over the trace
duration, not 280% of the entire CPU and not proof of the frame's critical path.
57.216 CPU-s had stacks; 1.637 CPU-s (2.8%) did not. 11.927 CPU-s had unresolved
leaf symbols, principally system kernel/driver/allocator libraries; parent
stacks remain useful, but attribution is incomplete. No kernel stacks or waiting
thread samples were requested. Time sampling is an estimate, not cycle counting.

| Thread role, identified by call chain | Sampled running CPU-s | Share of app sampled CPU |
|---|---:|---:|
| Native render worker (`RenderWorkerMain`) | 14.945 | 25.4% |
| Game render-command producer (`SubmitTitleCommand` call chain) | 10.460 | 17.8% |
| Guest audio mixer (`sub_821909D0`) | 7.380 | 12.5% |
| Main guest thread | 4.537 | 7.7% |
| All other app threads combined | 21.531 | 36.6% |

The native worker and producer remain first and second in each full five-second
bin, not merely a single sampled spike. Together they account for roughly 43%
of app CPU work. This supports prioritizing **renderer CPU overhead** ahead of
blind game-simulation changes. It does not establish that GPU work never limits
frames or that optimizing every busy thread improves pacing.

The worker's inclusive samples include `RecordNativeFrame` 4.072 CPU-s,
`PrepareFrameTextures` 1.838, `BindCommonDrawState` 1.733 and
`TryPrewarmDrawPipeline` 1.226. Producer `ValidateAndCopyCommand` accounts for
3.899 CPU-s inclusively. These nested costs overlap and must not be summed.

Across all app threads, recognizable command construction/move/destruction
stacks account for 3.420 CPU-s, XXH3 hashing 2.144, and memory-copy paths 1.857.
These categories also overlap: moving a command can invoke memory copying.
On the worker, sampled `memmove` callers include command move construction
(0.351 CPU-s), move assignment (0.218) and copying shared constant vectors
(0.249). These are concrete places to investigate, not a forecast of recovered
frame time.

The guest audio mixer's largest named leaf is `rex_generated_sub_829321A0`
(2.252 CPU-s); `rex_generated_sub_821997F8` adds 0.800. This is a secondary
efficiency opportunity, not evidence that sample-rate reduction is necessary.
The last collected audio summary still has zero underruns, rebuffers, drops,
clipping, nonfinite samples and recovery silence.

### Logging versus always-on verification

Normal logging is enabled and its default `flush_level` is `info`:
`src/core/logging.cpp`, `include/rex/logging/types.h` and
`ios/bridge/theft4_boot.cpp`. But only **six runtime-log messages, about 1.5 KB**,
occurred during the CPU capture. Recognizable logging/format-output call paths
account for approximately **0.006 CPU-s / 58.853 = 0.01%**. This does not bound
every unsymbolicated/system I/O operation, but it argues strongly against a
log flood as this run's main slowdown. Keep error reporting; buffered/rate-limited
informational logging is a lower-priority experiment if a later workload floods.

Trace-named wrappers such as `SubmitFireTracedCommand` include downstream real
render submission in their inclusive time. Their names do not prove logging
was enabled. Detailed profiler scopes were not observed as a named hotspot;
inlined checks may be charged to their callers. No named SceneKit/menu call
paths were found in the gameplay stacks, consistent with scene retirement.

However, **state verification does real work even with verbose tracing off**.
In native `graphics_system.cpp`, `HashFixedFunctionState` performs **49 chained
seeded hash calls** for one fingerprint. It is called during producer capture,
worker finalization and recording-time corruption checking for draws/clears.
Seeded-hash leaf samples directly attributed to those three callers total
**1.270 CPU-s** (0.411 + 0.382 + 0.477). Some callers contain other hashing, so
this is attribution to the call paths, not an exact isolated benchmark of the
helper. The code and samples together justify the first experiment below.

**Do not simply remove the recording-time check:** it rejects changed snapshots
before issuing a potentially corrupted draw. Optimize the fingerprint while
preserving detection and rejection. Shader constant content hashes also serve
real state/cache identity; they are not disposable debug output.

### Frame evidence and capture limitations

One sparse presentation interval wholly inside the CPU run was 300 new frames
over 11.679 seconds, approximately **25.69/s**. Subsequent windows ranged from
about 19.3 to 30.2/s as gameplay and instrumentation attempts varied. This is
not a controlled before/after result or measured p99 frame pacing. No locked-30
claim is supported, and these data must not be compared to the earlier Boost
run as a matched resolution A/B.

Game Performance disconnected after 1.148 seconds. A shorter retry and a
System Trace attempt also disconnected. The application stayed alive; the
failure is in capture/connectivity, not evidence of an app crash. **No valid
CPU/GPU wait timeline or scheduling-policy measurement was obtained.** Do not
extrapolate the truncated traces. Next time repair/verify Instruments device
tracing or use the existing bounded native phase/fence profiler to obtain the
missing correlation; do not keep retrying the same broken capture.

Final gameplay log was collected and Theft4 was terminated after capture;
process listing confirmed it stopped. No game files or saves were modified by
the audit. Raw traces and logs remain private and uncommitted.

Historical generic-renderer CPU profiles are not the current baseline. The older
native hang capture suggested command copying, mutexes, reference counting and
hashing as investigation targets, but preceded subsequent batching changes and
included a renderer failure. None is proven dominant in healthy city gameplay.

## Source audit: what already exists and what remains uncertain

Paths below are relative to the repository. SDK paths start with
`glue/rexglue-sdk-main/`.

| Area | Verified implementation | Implication / next evidence |
|---|---|---|
| Launcher | `ios/Theft4/main.m` calls `retireScene` before starting the game; the city view removes its SceneKit scene/view and rain | Do not blame the decorative menu without finding surviving SceneKit work in gameplay samples. |
| AOT CPU | `ios/CMakeLists.txt`, generated code under `glue/rexglue-sdk-main/gta4-recomp/generated/` | Normal compiled ARM64 game code, not a CPU JIT. Hot generated functions may still retain expensive emulation semantics. |
| Frame pacing | `ios/bridge/theft4_gta4_native_graphics.cpp`, SDK `gta4-recomp/src/gta4_native_hooks.cpp` and its frame-limiter helper | iOS already caps at 30. `PaceNativePresent` sleeps against deadlines. Another cap cannot create missing frame budget. |
| CPU command transport | SDK `src/graphics/gta4_native/graphics_system.cpp`, `RenderWorkerMain` | Existing 64-command batch extraction, reused command capacity, generation protection and producer backpressure. Profile remaining moves, ownership, allocations and notifications before changing them. |
| Snapshots/settings | Same file, `ValidateAndCopyCommand`, snapshot generation reuse and `BeginModernShaderFrame` | Dirty snapshots/settings are already cached in several paths. Identify remaining repeated work rather than adding duplicate caches. |
| Resource safety | Same file, `CompleteNativeFrameSlot` and texture-generation ownership | One active frame is the stability default; GPU fence wait is wall time, not automatically CPU time. Do not remove waits/protection or restore two frames as a casual optimization. |
| Native profiling | `native_cpu_profile_scope.h`, `native_profile_detail.h`, `PublishFrame` in the native renderer | Detailed timers/queries require the native-profiler diagnostic category. Normal scopes retain a TLS pointer check, but do not all take timestamps. Existing bounded capture should be reused. |
| Audio | `ios/bridge/theft4_ios_audio_hotpaths.cpp` | Mixer QoS, narrow queue/poll backoffs and guarded NEON gain-ramp path already exist. Preserve guest stores, call-site guards, strict reference checks and 48 kHz output. |
| Apple scheduling | SDK `src/core/threading_mac.cpp`, `src/system/xthread.cpp` | iOS uses the Mac thread implementation. Affinity is a no-op. Raw host priority changes use `SCHED_FIFO`, but `ignore_thread_priorities=true` bypasses ordinary guest-priority mapping; certain creation flags still call it. Measure effective policy/QoS rather than assume every guest thread is FIFO. |
| Native worker scheduling | Native `graphics_system.cpp` creates a `std::thread` with no explicit worker QoS assignment | Inherited/effective QoS and ready-to-run delays need measurement. This does not prove E-core placement or starvation. |
| Driver dispatch | SDK `src/ui/vulkan/vulkan_instance.cpp` | MoltenVK synchronous queue submissions are explicitly false. This controls where CPU encoding/submission work runs, not a forced GPU-completion wait. A controlled existing-cvar A/B may be useful if dispatch/driver work dominates. |
| Output cost | `ios/bridge/theft4_output_policy.h`, `theft4_metal_presenter.mm`, SDK `src/ui/vulkan/vulkan_presenter.cpp` | Reference is 720p scene → 1080p FSR, SMAA/high and 4× filtering. Measure Boost separately; do not silently lower the accepted visuals. |

Particular traps: sampled CPU-seconds across threads cannot be added and called
frame latency; blocked threads may not appear in a running-only profile; a
mutex function appearing in a sample does not establish which owner delayed it;
short `nanosleep` requests are not guaranteed wakeup deadlines. Generated
`gta4-recomp/generated/gta4_init.h` uses direct calls and a bounds-checked
per-module function-pointer table for the usual indirect-call path. The global
dispatcher is a fallback, not a mutex lookup on every generated call.

## Concrete experiment queue from this capture

### E1 — cheaper state-integrity fingerprints, checks retained

**Recommended next implementation task.** Touch native `graphics_system.cpp`
and an isolated helper/test under SDK native-renderer code/tests. Serialize the
exact same logical fields into a canonical contiguous representation and
benchmark one XXH3 operation against the present 49-call chain. Do not hash raw
struct padding. Preserve floating-point bit patterns, arrays and field coverage.
The digest value may change, but all comparison producers/consumers must use
the same implementation; check for persisted/external identity assumptions.

Tests: identical fields match despite storage/padding differences; each scalar
and array field mutation changes representative fingerprints; signed zero and
NaN payload bits are treated consistently; captured/finalized/recorded states
detect injected mutation and still reject it. Use seeded randomized states in
addition to explicit cases. Do not demand old and new digest values be equal.

Benchmark the helper in optimized ARM64 code, then one-variable installed A/B
with unchanged output, one-frame protection and logging. Success requires lower
CPU cost in those caller paths and no visual/correctness regression. Even total
removal of the measured 1.270 CPU-s would be only a few percent of this capture's
app CPU; it is not a promise that this alone produces locked 30.

### E2 — reduce command transport's repeated large-object moves

The current path moves a `NativeCommand` into the queue, into the worker batch,
into a local command, then into the retained frame. Batching already reduces
lock overhead but does not eliminate those object moves and destruction.
First measure actual object size and move counts in a bounded diagnostic.
Then consider stable owned packets/pool slots with cheap handles, or removal of
an intermediate move where lifetime permits. Compare against existing value
storage: adding one heap allocation per command could make it worse.

Files: native `graphics_system.h/.cpp`; transport/lifetime tests. Preserve FIFO,
shader snapshots, protected texture generations, synchronous completion and
bounded memory. Recycle only after all current owners release the packet; do
not confuse CPU packet lifetime with GPU resource completion. Keep one frame
in flight. Stress fire/explosions, phone/TV, rapid camera movement, texture
release, save/load and shutdown. This is higher risk than E1 and a separate A/B.

### E3 — avoid repeated warm pipeline/preparation work

`TryPrewarmDrawPipeline` accounts for 1.226 inclusive worker CPU-s in the warm
run. It resolves a target and asks for a pipeline on each eligible draw; actual
recording subsequently needs the target/pipeline too. Investigate a validated
already-ready fast path or reuse of a prepared result rather than turning off
prewarming. Key on target lifetime, formats, sample count, pipeline layout,
shader/state identity and primitive specialization. The existing lookup cache
may already cover part of this; measure what remains before adding another.

Pair this with a separate investigation of constant-vector materialization and
`PrepareFrameTextures` only if a fresh profile supports it. Retain cold-cache
prewarming and cache invalidation; improving warm averages at the expense of
first-use stalls fails the acceptance criteria.

### E4 — secondary CPU paths, only after remeasurement

Guest DSP `sub_829321A0` is a sizeable leaf in this run. Use the generated body
as a bit-accurate reference and the existing audio hotpath regression approach;
do not skip filters, tracks or reduce sample rate. Keep inline XMA ordering.
QoS, MoltenVK submission mode, selective ThinLTO and eventual PGO remain
conditional experiments below, not substitutes for the stronger E1–E3 evidence.

## Ordered passes and decision gates

### P0 — establish a repeatable reference

Keep the current installed executable. Ordinary app-icon launch, 1080p FSR,
Boost off, 4×, one frame, same save and driving route. Note battery/power state,
thermal state if available, warm/cold shader cache and scene. Avoid recording,
screen mirroring, app switching and downloads during the reference. A debugger
or profiler run is a diagnostic sample, not the final retail benchmark.

### P1 — CPU attribution, then critical-path correlation

Capture 15–20 seconds in the actual slow city scene. Symbolicate only against a
matching executable UUID. Rank running CPU time by thread, leaf and inclusive
caller; report missing-stack coverage. Use CPU Profiler to cross-check periodic
sampling bias if needed. Then a separate bounded System Trace/Game Performance
recording correlates running, runnable and blocked intervals with GPU work and
new-frame publication. Never run all profilers together.

Use existing tools `tools/summarize_ios_time_profile.py` and
`tools/summarize_ios_game_trace.py`; inspect each export schema before applying
the parser. CPU Profiler's cycle weights are not Time Profiler nanoseconds.
Stop the game after the agreed capture window when it is no longer needed.

### P2 — select one evidence-backed experiment

| Priority if supported by trace | Experiment and files | Safety/verification |
|---|---|---|
| Producer or render-worker CPU is critical | Remove a repeated immutable snapshot/hash/allocation or unnecessary command copy in native `graphics_system.cpp`; consider buffer reuse keyed by existing generations | Keep order, dirty invalidation, ownership and resource retention. Compare command/output correctness, resource lifetime and frame-time tail; no unbounded cache. |
| Excessive wakeups/lock contention is critical | Adjust notification/batch boundaries in the existing native command transport | Keep FIFO, condition predicates and generation protection. Check queue depth, latency, shutdown and blocking progress; larger batches can hurt latency. |
| Critical work is runnable but unscheduled | Audit effective QoS and try a narrow role-specific assignment in Apple thread/renderer initialization | Do not globally elevate every guest thread or use CPU affinity folklore. Check audio deadlines, system responsiveness, energy and thermal behavior. |
| One AOT function dominates | Optimize that function/bridge with a proven equivalent ARM64 implementation or avoid redundant calls | Compare guest memory/register/FP outcomes, alignment, overlap and boundary cases; preserve gameplay timing and strict DSP semantics. |
| Driver dispatch dominates CPU/ready delay | A/B existing `vulkan_moltenvk_synchronous_queue_submits` true vs false | Same one-frame lifetime protection, output and scene. Compare CPU handoff latency, GPU overlap, frame tails and audio; revert if not repeatable. |
| Hot cross-TU call overhead remains | Isolated ThinLTO experiment in `ios/CMakeLists.txt`, later representative PGO if justified | The current LTO option does not cover every archive. Verify strong native hooks still override weak generated functions, registration/indirect calls survive, startup/audio/saves work, and symbols match. Keep baseline artifact. |
| GPU rather than CPU limits the frame | Return to measured pass/texture/bandwidth analysis with existing native GPU profiler | Preserve visuals. Do not optimize unrelated guest code and assume a GPU-bound frame will improve. |

No estimated FPS gain is assigned before measurement. Do not combine multiple
unrelated changes into a single candidate. Retain a change only if matched A/B
runs improve the intended metric without correctness, audio or thermal regression.

### P3 — confirm a paced 30, not merely a 30 average

The deadline is **33.333 ms per new frame**. Aim for approximately 28–30 ms
critical-path work in representative heavy scenes to leave headroom; that is an
engineering target, not current measured performance. CPU/GPU overlap means
component times must be placed on a timeline, not blindly summed.

Record new-content intervals, p50/p95/p99, missed presentation deadlines,
>50 ms and >100 ms stalls, audio underruns, visual/resource errors and memory
growth. Treat the corrected FPS overlay as a quick indicator, not a replacement
for intervals. Mean 30 with repeated stalls does not meet the target.

Run at least three alternating baseline/candidate comparisons on the same route,
then a ten-minute city/fast-driving soak and save/load check. Separate cold shader
compilation from warm steady-state pacing. Repeat on other hardware before
claiming general compatibility/performance; M5 tuning alone cannot guarantee it.
Only after 1080p acceptance should Boost repeat the same sequence.

## Reproducible capture outline

Set the real connected device ID locally; never commit it or raw captures.
Use a fresh directory from `mktemp -d` and the selected Xcode developer directory.
The existing all-process recording workaround may be needed when device attach
is unavailable; filter analysis to Theft4 and keep the trace private.

```sh
env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcrun xctrace record --template 'Time Profiler' --device DEVICE_ID \
  --all-processes --time-limit 15s --output PRIVATE_CAPTURE/cpu.trace

env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcrun xctrace export --input PRIVATE_CAPTURE/cpu.trace --toc \
  --output PRIVATE_CAPTURE/toc.xml

# Select the actual time-profile table from the TOC before exporting it.
env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  python3 tools/summarize_ios_time_profile.py PRIVATE_CAPTURE/cpu.xml \
  out/build/ios-device-release/theft4/Release/Theft4.app/Theft4
```

## Relevant primary references and how they affect this plan

- Apple's [Optimize CPU performance with Instruments, WWDC25](https://developer.apple.com/videos/play/wwdc2025/308/)
  distinguishes CPU work from waiting, recommends CPU Profiler to reduce timer
  sampling bias, and explains CPU Counters and short Processor Trace captures.
  Use counters/trace after locating the costly path, not before. Processor Trace
  requires appropriate device support/settings and can generate very large data;
  enabling it is a separate explicit step, not assumed in this pass.
- Apple's [Tune CPU job scheduling for Apple silicon games](https://developer.apple.com/videos/play/tech-talks/110147/)
  supports checking job granularity, synchronization, priority inversion and QoS.
  It does not justify blindly raising thread priorities or spawning more workers.
- Apple's [Analyzing the performance of your Metal app](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app)
  is the reference for correlating CPU/GPU activity with Game Performance tools.
- LLVM's [ThinLTO documentation](https://clang.llvm.org/docs/ThinLTO.html)
  describes cross-module optimization. Its existence is not evidence this large
  AOT binary will run faster; hook/interposition correctness remains our concern.
- [MoltenVK configuration documentation](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Configuration_Parameters.md),
  checked against the local pinned copy at
  `thirdparty/MoltenVK/MoltenVK/Docs/MoltenVK_Configuration_Parameters.md`,
  explains synchronous versus dispatched CPU queue submission. Use the pinned
  implementation's behavior; no dependency upgrade is implied.

These sources guide experiments. None proves a Theft4-specific bottleneck or
promises locked 30 FPS. The next implementation choice must follow the current
annotated native-renderer trace, not anecdotes about another emulator.
