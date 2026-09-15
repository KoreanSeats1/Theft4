# Theft4: sustained audio starvation and 16:9 presentation

## Implemented and validated on M5 iPad — 2026-09-15

This section supersedes the discovery-only status below. The pass was built as
Release, signed, installed, and run on physical device `test iPad` (iPad Pro
11-inch M5, iPad17,2). The user listened through the demanding opening sequence
and reported that audio sounded great. The application was then closed by the
user after the car sequence; it did not crash.

Implemented changes:

- The UIKit Metal view is a centered, maximal 16:9 rectangle on a black parent.
  The 1280x720 render path is unchanged. Device screenshots at 30, 145 and 265
  seconds confirm correct geometry and full 3D output without vertical stretch.
- XMA kick processing defaults to the existing XeniOS-style inline path, while
  preserving `THEFT4_XMA_INLINE=0` as the dedicated-worker control.
- Startup still waits for the full 32-block / 8192-frame safety buffer. Recovery
  after an underrun now defaults to 8 blocks / 2048 frames and applies a
  128-frame fade-in. `THEFT4_AUDIO_RECOVERY_BLOCKS=32` restores the old refill
  threshold without rebuilding.
- The actual AVAudioSession rate, buffer period and output latency are logged.
  The tested device negotiated 48,000 Hz, 5.333 ms and 11.0 ms respectively.
- Detailed handoff timing is now opt-in via `THEFT4_AUDIO_TIMING=1`. An initial
  instrumented run generated over two million events in 3.6 minutes, so normal
  play removes the previous trace and incurs no CSV writer/capture cost.

Controlled final run (`17:12:02.806` through `17:16:58.566`, 295.760 seconds):

| Metric | Prior 300.342 s bad window | Final 295.760 s run |
| --- | ---: | ---: |
| Rebuffer events | 368 | 12 |
| Recovery silence | 93.611 s (31.17%) | 0.795 s (0.269%) |
| Effective submitted frames/s | 33,039 | 47,867 |
| Dropped blocks | 0 | 0 |
| Clipped / nonfinite samples | 0 / 0 | 0 / 0 |

The 12 final-run events occurred in two short bursts (seven around 17:14:22 and
five around 17:16:58); the ring returned to its full 8192-frame target after
the first burst and remained there for more than two minutes. Relative to the
matched-duration bad window, this is 96.7% fewer rebuffer events, 99.15% less
recovery silence and 44.9% higher effective delivery throughput. This is a
functional playability improvement, not merely a counter change: inline XMA
removes the sustained production deficit, while the smaller recovery threshold
limits the audible duration of rare transient shortages.

Artifacts:

- `out/audio-aspect-final-build-20260915.log`
- `out/device-audio-aspect-final-controlled/runtime-final.log`
- `out/device-audio-aspect-final-controlled-30s.png`
- `out/device-audio-aspect-final-controlled-145s.png`
- `out/device-audio-aspect-final-controlled-265s.png`

Validation: Release build succeeded; app remained alive through all automated
process checks; full 3D rendered through the ship and car sequence; gain-ramp
oracle passed 20,000 byte-exact cases. This is a strong five-minute acceptance
result, not yet the 10–15 minute soak required to call every audio workload
closed. The latest working build remains installed and stopped on the iPad.

Research checkpoint: 2026-09-15. Source base `6cadc5c9` plus the existing dirty
working tree. Installed executable UUID from the preceding pass:
`67A43655-B298-3215-ACF6-CF80BCF413A4`.

This pass inspected code, existing CPU traces, and a fresh device log. It did
not change runtime source, rebuild, reinstall, restart either app, or modify
game assets. Theft4 was no longer listed as running when checked; its last
recorded playback was at 16:49:03. No conclusion about why it stopped is made.

## Conclusions

1. The vertical stretch has a confirmed presentation-layout cause. Fix the
   UIKit game view to aspect-fit 16:9; keep rendering at 1280x720.
2. Audio is not fixed. The longer run reproduces the user's 1–2 skips/second.
   Early-cutscene comparisons were insufficient to establish sustained success.
3. The output ring genuinely runs out. Its full-refill recovery policy makes
   each shortage especially audible. There is also a substantial effective
   production deficit in the bad interval; recovery/buffer changes alone
   cannot establish adequate sustained throughput.
4. We need one bounded timing capture that separates DSP computation, XMA
   round-trip waits, guest mixer synchronization, and native callback timing.
   Existing sampling profiles do not distinguish those causes adequately.
5. There are concrete next optimizations. None requires JIT, removing voices,
   changing sample rate, or rewriting the renderer.

## Evidence from the actual bad period

Source: `out/audio-diagnosis-current-20260915.log`, last launch beginning
16:32:15.931 and ending in the log at 16:49:03.023. Counters below are differences
between timestamped records nearest the stated windows, not cumulative startup
values.

| Window ending 16:49:03 | Gaps | Gap rate | Recovery silence | Effective submitted frames/s |
| --- | ---: | ---: | ---: | ---: |
| 59.640 s | 82 | 1.375/s | 23.392 s (39.22%) | 29,175.6 |
| 120.130 s | 152 | 1.265/s | 42.277 s (35.19%) | 31,108.7 |
| 300.342 s | 368 | 1.225/s | 93.611 s (31.17%) | 33,039.2 |

Required output: 48,000 frames/s = 187.5 blocks/s at 256 frames/block.
No new clipped or nonfinite output samples occurred in these windows. The last
minute averages approximately 285 ms recovery silence per gap. This is not
merely a click detector or a subjective interpretation of the sound.

These rates measure delivery through the current pipeline and its policy;
they do NOT prove that DSP arithmetic alone is too slow. Blocking, scheduling,
credit accounting, and scene/thermal changes still need separation. There is
currently no thermal-state record to justify claiming thermal throttling.

A useful approximate explanation of the repeating rhythm: refilling 8,192
frames at ~29,176 frames/s takes ~281 ms. While playing, demand exceeds that
production rate by ~18,824 frames/s, draining that reserve in ~435 ms. The
resulting ~716 ms cycle resembles the observed ~1.4 gaps/s. This is an
illustration of the policy under steady underproduction, not independent proof
of which upstream stage causes the shortfall. A larger buffer changes this
cycle's length, not the underlying rate deficit.

## Confirmed 16:9 cause and minimal fix

Call/layout chain:

- `ios/Theft4/main.m:69`: `_metalView` fills all root-view edges.
- `ios/bridge/theft4_metal_presenter.mm:79`: drawable remains 1280x720 regardless
  of UIView dimensions.
- `ios/bridge/theft4_rex_vulkan_gate.cpp:33`: Vulkan surface dimensions are that
  drawable size, so its presenter sees an already-16:9 destination.
- `theft4_metal_presenter.mm:65` initially sets `ResizeAspect`, but
  `glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp:1600` supplies no
  swapchain scaling policy (`pNext=nullptr`).
- The actually linked XeniOS MoltenVK implementation at
  `../XeniOS/third_party/MoltenVK/MoltenVK/MoltenVK/GPUObjects/MVKSwapchain.mm:447`
  defaults to `Resize`, then assigns it to the layer at line 509. The initial
  aspect-gravity setting is overwritten.

**Implementation:** change only the shell layout in `ios/Theft4/main.m`:

- Black parent background.
- Center the Metal view on both axes.
- Required width = height * 16/9, width <= container width, height <= container
  height. Prefer full width at priority 999 to select the largest fitting box.
- Retain 1280x720 drawable, guest video mode, and existing swapchain/backend.
- Leave the bring-up overlay as a separate full-size sibling.

No guest projection patch, vendor patch, new Vulkan extension, or full-Retina
drawable is needed. The present input backend uses GameController and the Metal
view disables touch interaction, so current controller coordinates are unaffected.
Future touch aiming must map relative to the game rectangle and ignore bars.

Verify uncropped 16:9 screenshots, correct proportions, black bars, rotation and
window resizing, controller operation, and unchanged 1280x720 rendering logs.
This fixes geometry; do not attribute audio performance gains to it.

## Audio path and verified differences from XeniOS

Only host client 0 is registered in the observed runs. Its callback is
`8219A2B8 -> 821910D0`. The normal route signals/waits for the guest mixer
`821909D0`, executes guest DSP, submits through `82194698` /
`XAudioSubmitRenderDriverFrame`, reaches `Theft4BootstrapAudio::SubmitFrame`,
converts six guest channels to stereo, queues PCM, and feeds RemoteIO.
Multiple speaking characters are mixed inside that guest audio graph; they do
not become independent native iOS audio clients.

### Recovery policy

`ios/bridge/theft4_ios_audio_output.mm:90` waits for all 8,192 frames after every
underrun, emits silence meanwhile, and stops advancing read-based credits.
XeniOS's 32-block queue depth is NOT a 32-block mandatory restart threshold.
Its `src/xenia/apu/sdl/sdl_audio_driver.cc:224` consumes available blocks,
releases a recovery credit when empty, and can resume on a later callback
without waiting for the entire queue to refill.

### Callback credit accounting

`ios/bridge/theft4_bootstrap_audio.cpp:194` increments `pumped_blocks_` after
every returned callback; it does not reconcile that with submissions.
`821910D0` has alternate-wait/error paths that skip submission. One permanently
missing submission can make a later exact 32-block restart threshold
unreachable once credits are exhausted. This is a latent weakness, NOT a proven
explanation of this run: the repeated recoveries show it is not permanently
stuck. Delayed submissions must not be blindly replaced with additional work.

### XMA round trips

`glue/rexglue-sdk-main/src/audio/xma_decoder.cpp:298` enables contexts, signals
a dedicated worker, then synchronously waits for their completion. This can
add latency which a CPU-only sample of the mixer does not reveal.

XeniOS defaults to a dedicated XMA worker too (`xma_decoder.cc:59`). It also
has an inline `context.Work()` option at line 348. Available local reference
artifacts did not establish an override used by the user's working XeniOS run.
Inline decode is therefore a controlled experiment, not a proven missing fix.
XeniOS's `SetBoostPriority()` is simply an alias for `Set()` on non-Windows
platforms (`src/xenia/base/threading.h:324`); its name does not imply a missing
iOS priority boost in Theft4.

### Existing diagnostics

`handoff::Span("xma-kick")` and `Span("xma-work")` already record wall duration.
However, only desktop `AudioSystem::Setup` initializes that subsystem; the iOS
bootstrap bypasses it. An environment variable alone will not activate it.
The current implementation captures decoded PCM and does additional signal
analysis; its minimum PCM budget is 64 MiB. Add an event-only mode before using
it to diagnose performance, and check capture overhead against a control run.

### Measured DSP targets, not missing SIMD

Existing `out/audio-fence-profile-summary-20260915.txt` has ~8.33 sampled CPU-s
in the mixer over a 15-second capture. Five DSP leaves account for ~5.10 CPU-s:
`82935BD0`, `829321A0`, `821997F8`, `82936CE0`, and `82934F00`.
That sample predates the final worst minute and is not a measurement of its
blocking time.

Source/assembly inspection finds native NEON and hardware double FMA already
present. This is not fixed by simply enabling SIMD or replacing a libm call.
Generated scalar DSP deliberately performs double operations followed by float
rounding; float FMA or global fast-math can change the game DSP. The likely
optimization opportunity is reducing repeated context/guest-memory accesses
and retaining local filter state, with exact arithmetic and state validation.

## Ordered implementation plan

### P0 — independent 16:9 shell fix

Files: `ios/Theft4/main.m` only for functional changes.
Implement the centered aspect-fit rectangle above. Verify geometry and inputs.
Keep this separate from audio experiments so visual changes cannot confound
audio conclusions. Small, low-risk, reversible layout change.

### P1 — capture one real bad interval, with deadlines accounted for

Files: `ios/bridge/theft4_bootstrap_audio.cpp`, `theft4_ios_audio_output.mm`,
`theft4_ios_audio_hotpaths.cpp`, SDK `include/rex/audio/handoff_trace.h`,
`src/audio/handoff_trace.inc`, and the already-instrumented XMA files.

Initialize/shut down the existing bounded trace from the embedded lifecycle.
Add event-only mode that also avoids PCM inspection/copying, not merely disk
writes. Capture 60–90 seconds after sustained heavy 3D reproduces the skips:

- Requested, entered, completed callbacks; accepted/rejected submissions;
  bounded in-flight work and ring occupancy at each transition.
- Guest mixer and callback wall time versus thread CPU time; separate actual
  DSP work, guest event/lock waits, XMA kick-to-completion, and worker work time.
- RemoteIO callback spacing, requested frames, consumed frames, silence, and
  output deadline misses. No formatting, filesystem I/O, or locks in callback.
- Actual AVAudioSession sample rate / I/O duration and audio-route changes.
  Record thermal and power state away from real-time callbacks.

Gate: no silent telemetry losses, known measurement overhead, and an accounted
timeline around repeated gaps. Choose P2 based on this capture, not the intro.

### P2 — remove the measured producer bottleneck

**If XMA handoff/wait dominates:** add a startup-selectable inline decode path
equivalent to XeniOS's existing option. Keep completion synchronous; ensure the
worker and inline path never process the same context concurrently. Preserve
disable/clear/release ordering. Retain dedicated-worker control. Run existing
XMA lifecycle/loop regressions and compare context state and PCM on the same
legally owned fixtures; then compare warm heavy-scene latency and silence.

**If arithmetic/state access dominates:** first consider `sub_82936CE0`, a
bounded recurrent scalar filter, then `sub_829321A0`. Keep generated bodies as
the oracle. Extract only inner kernels into the iOS hotpath layer; preserve
double-FMA-to-float rounding, flush modes, state updates, and alias behavior.
Use randomized nonzero inputs plus representative captured state; compare
outputs and persistent filter state, not just silence or one initial call.
Keep a generated fallback. Profile before adding further overrides.

**If graphics contention/scheduling dominates:** measure runnable/blocked time
and graphics submission work under a fixed scene. Do not keep adding arbitrary
sleeps or elevate every thread's priority. Existing wait toggles allow controls.

Gate: increased sustained delivery under load, reduced missed audio time,
correct audio, and no frame-progress regression. CPU percentages alone fail
this gate.

### P3 — separate startup buffering from underrun recovery

Files: `theft4_ios_audio_output.mm` and its focused deterministic callback test.
Compare current full refill with a smaller bounded restart threshold or
available-sample playback, with a short continuity ramp at silence boundaries.
Keep adequate queued work ahead of playback. Measure total missing audio,
latency, repeated gaps, and A/V drift. Do not claim a cure from merely changing
gap size/frequency or moving counters. Keep 48 kHz and all game effects.

### P4 — harden credits without unbounded catch-up

Files: bootstrap callback ledger and output request accounting.
Test synchronous submit, delayed submit, callback-without-submit, rejected
submission, underrun/recovery, and unregister/re-register. Track lifecycle and
in-flight work before granting bounded recovery credits. Never create duplicate
credits simply because a callback has not yet submitted. Required result:
no permanent refill stall, no queue overflow, and no duplicate guest work.

### P5 — accept only a sustained result

Use the same save/scene, output route, 720p settings, device power conditions,
and controlled thermal state for A/B runs. Include overlapping dialogue, music,
wide 3D, cutscene transitions, and gameplay for at least 10–15 minutes after
warm-up. Compare 60-second windows, not just cumulative launch counters.

Target: no recurring starvation gaps in representative gameplay, no clipping
or numerical corruption, no growing A/V drift, healthy frame progress, and a
listening check. Report any loading-boundary exceptions explicitly. The current
build fails this acceptance test.

## Exact next task

Implement P0's one-file 16:9 layout and P1's event-only timing capture in
separate changes. Collect one sustained bad-scene capture. Then select inline
XMA versus the first DSP kernel from measured elapsed-time attribution. Do not
start with another buffer increase, 44.1 kHz, per-chip builds, or global FP flags.
