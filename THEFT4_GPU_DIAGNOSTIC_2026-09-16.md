# Theft4 GPU diagnostic and experiment plan — 2026-09-16

Status: **historical generic-renderer diagnosis; several experiments were later
implemented behind opt-in switches, and the GTA-IV-specific renderer became the
leading path.** Current truth is recorded in `CHANGELOG.md` and section 10 of
`THEFT4_3D_PERFORMANCE_PLAN.md`.

This document preserves the trace-backed diagnosis and experiment design.
The goal remains correctly rendered, paced 30 FPS through the complete intro into
player control, with unchanged visuals and healthy audio. It is not yet achieved.

This pass inspected existing private traces, renderer source, the public sibling
XeniOS checkout, and Apple documentation. It exported additional tables from an
existing Instruments recording. No game/replay app was launched, rebuilt or
installed; no renderer code, device condition, game data or save was changed.
Theft4 and MetalReplay were absent from the final device process listing.

## 1. Findings that change the plan

1. **The bounds-estimator experiment has actual qualifying work.** A read-only
   reconstruction of the visible 1,960-draw trace finds 26 qualifying small
   rectangle draws, only 78 vertex evaluations. All use a simple interpretable
   position/color shader. This is stronger than merely finding a disabled flag.
2. **XeniOS already implements a missing transfer-in-draw-pass optimization.**
   Theft4 currently uses separate passes for ownership transfers. A scoped
   backport can avoid some tile stores/loads without changing renderer backend.
3. **Full render areas can dwarf the actual guest scissor.** Some 128×128 effects
   use a source-derived 160×8192 framebuffer extent. Whole-pass bounds are a
   separate opportunity from tightening ownership ranges.
4. **Native Metal has not had a valid correctness test yet.** The one-shot
   replay leaves asynchronous shader compilation on, so draws may be skipped.
   The XeniOS Vulkan control additionally has an empty EDRAM restore method.
   Fixing the harness is a small experiment; neither black output proves the
   trace incompatible nor disproves a native Metal route.
5. **Current timing evidence has important limits.** Most useful GPU rows cover
   only about one second, and all exported performance-state rows report an
   induced Medium GPU condition. We can prioritize work from this capture,
   but cannot extrapolate normal-launch M5 throughput or promise 30 FPS.

## 2. Evidence and reproduction identity

Current root HEAD: `16e76b9ea230920317436159258c745df706361b`, with substantial
pre-existing uncommitted changes and dirty dependencies. HEAD alone is not the
binary's source identity. Sibling XeniOS HEAD:
`87b176a078c316fde3adf67a217f0c44615b0e0d`; its local modifications also matter.

Current local Release binary:
`out/build/ios-device-release/theft4/Release/Theft4.app/Theft4`.
UUID `3D13C64B-1436-3179-BE0E-3B29B772983F`; SHA256
`ae583181744fe1b5cd4b184ef639c7987df872e7b311fcc59120448e99e6240d`.
These identify the local package; they are not a fresh installed-byte audit.

Private evidence (keep out of GitHub):

- `out/perf-p3-20260916/game-performance.trace`: M5 iPad, process 2556,
  10-second recorded duration despite the requested 45 seconds, saved recording
  mode “Windowed (5 seconds)”. Useful Metal events start around 8.98 s.
- Same directory: `gpu_intervals.xml`, `encoders.xml`, `submissions.xml`,
  `frame-assignments.xml`, `display-intervals.xml`, `diagnostic-extra.xml`,
  `summary-correlated.json`. A multi-table xctrace export only supplies one
  schema, so use single-table exports for generic schema-based parsers.
- `out/metal-replay-20260916/visible-scene.xtr`: 92,689,080 bytes, SHA256
  `9949f42b8b5fe567fb14f03523359883516c80b9574ccee7f5375c8e58d0514c`.
  Exact correct original: `vulkan-exact-2060.png`. This XTR is a separate capture
  from the Instruments recording; do not combine them as the identical frame.
- `out/perf-fsi-20260916/`: runtime log and corrupted captures, independently
  corroborated by the user. FSI is rejected for normal play.

### GPU timing and presentation audit

The previous “1,085 encoders” figure counts **all encoder types**, not render
passes. Representative interior groups contain approximately 310–312 dynamic
render encoders, 341 buffer-copy encoders, 255 compute encoders and 163
buffer-to-image encoders. Counts vary slightly; they are not guest draw counts.

Joining GPU intervals with application submissions agrees with Apple's separate
command-buffer frame assignment for overlapping IDs. Eight GPU buffers remain
unmapped. Excluding obvious boundary/tiny groups, frame IDs 4..22 give 19 samples:

| Measurement in this profiled sample | Median |
| --- | ---: |
| GPU active interval union per grouped frame | 37.33 ms |
| Dynamic render encoder GPU union | 32.11 ms |
| Compute encoder GPU union | 6.56 ms |
| Buffer-copy GPU union | 1.30 ms |
| Buffer-to-image GPU union | 1.20 ms |

These categories overlap; **do not sum them**. The earlier 36.38 ms median
included partial groups. Render/fragment durations do not isolate shader ALU
from tile load/store or other render-pass work. There are no useful limiter
counters in this recording to prove bandwidth, texture filtering or ALU saturation.

The displayed-surfaces table contains 23 intervals: median 41.666 ms, p95
49.999 ms; ten about 41.7 ms, nine about 50 ms, three about 25 ms and one about
16.7 ms. This verifies uneven surface cadence in this short profiled window.
It does not prove full-intro pacing or one-to-one unique game-frame display.
The in-app FPS counter deduplicates mailbox versions at successful queue-present
(`vulkan_presenter.cpp:3056–3074`); it measures submitted new images, not scanout.

The 125 GPU performance-state rows all say Medium, `is-induced=1`, “due to active
device conditions”, while the TOC reports “Induced GPU Performance State: Default”.
Cause and persistence beyond this capture are unknown. Do not silently interpret
it as maximum/default normal-play clocks, or infer thermal throttling. The short
thermal export is nominal. Prior ~90 ms start latency is a reported queue/start
latency, not 90 ms of GPU execution or CPU translation per frame.

### Source-supported mechanisms

All paths below are relative to `glue/rexglue-sdk-main/` unless otherwise stated.

- `src/graphics/vulkan/command_processor.cpp:3403`: any pending barrier ends
  the active render pass. Correct dependencies must be retained.
- `src/graphics/vulkan/shared_memory.cpp:343`: dirty guest-memory uploads force
  transfer usage and buffer copies, interrupting an active pass.
- `src/graphics/vulkan/texture_cache.cpp:1607,1716`: texture conversion dispatches
  followed by buffer-to-image copies. Texture residency already has some batching.
- `src/graphics/vulkan/command_processor.cpp:4466`: some memexporting draws copy
  guest indices to prevent aliasing. These copies are not automatically redundant.
- `src/graphics/vulkan/render_target_cache.cpp:5202`: explicit TODO to merge
  ownership transfers into guest passes and avoid tile-device stores/loads.
- `src/graphics/vulkan/command_processor.cpp:3486,3580`: dynamic passes use full
  framebuffer extent. Attachments use LOAD/STORE, including on repeated passes.

Frequent encoder adjacencies in the recording include render→buffer copy and
buffer copy→render. Their source reason is not labeled, so add bounded reason
counters before attributing all of them to one upload or synchronization path.

## 3. Experiment order and decision points

Run one candidate at a time. Start with E1 because it is small and has identified
qualifying work. Perform E4's one-shot correctness test as a bounded separate
target when convenient; it need not delay E1. Then E2, with E3 promoted if the
new pass-area counters show a large repeated full-extent cost. E0 is measurement
setup, not an open-ended profiling project. Each experiment ends in keep/reject.

| Experiment | Proposed effort | Primary question | Keep gate |
| --- | --- | --- | --- |
| E0: scene and measurement controls | Small | Are we comparing equivalent normal-play workloads? | Known scene/settings and usable cadence data |
| E1: existing draw-bounds estimator | Small | Can 26 small draws avoid large ownership transfers? | Less transfer work and repeatable speedup, correct output |
| E2: transfer-in-draw-pass backport | Medium | Can compatible transfers share the next guest pass? | Fewer actual pass restarts and better timing |
| E3: conservative whole-pass render area | Medium | Are small effects paying for huge tile extents? | Smaller proven coverage, lower GPU cost, identical visuals |
| E4: repair native-Metal replay | Small first gate; larger benchmarking/integration later | Does a complete synchronous replay render correctly? | Correct representative output before any speed comparison |
| E5: targeted copy/synchronization reduction | Conditional medium | Which remaining interruptions are avoidable? | Hazard-safe reduction with frame-time benefit |

Effort is relative implementation scope, not a promised duration or FPS gain.

### E0 — Measure the correct workload

Reuse the current Release target and established 720p/16:9/audio/host renderer.
Preserve the signed baseline and matching symbols/diff before any build. Leave
XeniOS app and saves untouched. Start with warm caches. Capture first wide ship,
Niko close-up, wide harbor/car, and first stationary player-control landmarks.
Scene matching must use camera/dialogue/guest-frame evidence, not equal seconds
after launch when one candidate runs slower.

For mechanism diagnostics, use a short **attach-to-running-process** recording
at a known landmark, verify the resulting duration/rows, and inspect GPU-condition
settings. Select Performance Limiters explicitly in Instruments when needed;
the current `--show-recording-options` CLI does not expose Metal counter settings,
so do not invent JSON keys. A saved custom Instruments template is appropriate.
Keep profiling/captures outside the ordinary-launch score run.

Reuse `VulkanPresenter::PaintTimingSnapshot` / `ConsumeLastPaintTiming` at
`include/rex/ui/vulkan/presenter.h:144` for correlation, noting it stores only the
latest sample. If full distributions are needed, add a bounded ring at publication
with mailbox version, guest frame, monotonic time and present result; a low-rate
consumer of the latest value can miss frames. No per-draw file I/O. Validate
actual display cadence separately with the Display instrument or presented-time
events. Instrumentation off is the performance score; instrumentation on explains it.

Report frame p50/p95/p99, >50/100/250 ms intervals and worst stall, first-use
pipeline stalls, submission/pass counts, GPU spans, CPU encoding, audio rebuffer
deltas, thermal state, and run identity. An average FPS overlay is insufficient.

### E1 — Turn on the existing ownership-range estimator

Code: `src/graphics/util/draw_extent_estimator.cpp:27,263`,
`src/graphics/pipeline/render_target/cache.cpp:571`, and
`ios/bridge/theft4_startup.cpp` for a proposed `THEFT4_DRAW_BOUNDS=0|1` adapter.
**That environment variable is not implemented yet.** Set
`execute_unclipped_draw_vs_on_cpu`; keep `..._with_scissor=false` initially.
Default false dates to the SDK import; inspected history contains no rationale.
XeniOS defaults true. The evaluator is existing scalar C++, not CPU JIT.

Trace reconstruction found 557 clip-disabled draws but only 26 passing both
8192-scissor eligibility gates. All 26 are 3-index auto-indexed rectangle draws
using the same 27-dword position/color shader with no texture fetch results,
loops/calls or tessellation. It passes the current static interpretability gate.
The 78 vertex evaluations make an inexpensive A/B plausible. Reconstructed inputs
predict substantial Y-bound reductions, but these are not measured runtime savings.
The parser does not emulate every conditional/RMW/GPU-memory effect.

Instrument aggregate eligible/interpretable/reduced counts, old/new heights,
actual ownership-transfer rectangles/tiles/bytes, estimator CPU time and pass
count. Qualifying input freshness matters: the interpreter reads CPU guest RAM;
its gate does not exclude GPU-produced vertices. For this captured shader verify
vertex data provenance or retain conservative bounds on uncertain GPU-written
ranges. Do not add a synchronous whole-memory readback to enable the optimization.

Warm A/B the same scenes. Stop quickly if eligible/reduced counts are zero or
no downstream transfer work falls. Keep only with correct depth/shadows/UI and
repeatable frame-time benefit. The estimator **does not shrink framebuffer
allocation or renderArea**; that is E3.

### E2 — Reuse XeniOS's compatible transfer merging

Reference: a sibling public XeniOS checkout at
`../XeniOS/src/xenia/gpu/vulkan/`:
`vulkan_render_target_cache.cc:36,2301,2473,2615,2682,2729,5912`,
`vulkan_command_processor.cc:3569`. Upstream flag `vulkan_transfer_in_draw_pass`
defaults true; no counterpart currently exists in Theft4.

Files to modify: Vulkan render-target-cache header/source, command processor,
and iOS launch adapter for proposed `THEFT4_TRANSFER_IN_DRAW_PASS=0|1`.
Queue eligible transfers in Update, preflight their pipelines, transition
sources before entering the pass, encode transfers immediately before the guest
draw, then restore the guest pipeline, descriptors and all dynamic state.

Prerequisite: Theft4's dynamic transfer pipeline currently declares depth-only
OR one color target (`render_target_cache.cpp:4847`). Generalize the attachment
signature to all MRT slots, including gaps, with independent depth/stencil,
matching XeniOS's implementation at `:5799`. Existing destination-color-index
shader output and write-mask support can be reused.

Keep standalone fallback for mismatched views/formats, source aliasing ANY active
attachment, unsupported depth/MSAA and failed preflight. Flush pending transfers
on post-Update early returns, including async placeholders, and before resolve,
submission/frame end or destruction. Reuse the scope-guard concept in XeniOS
Metal command processor `:2779`; do not blindly assume the Vulkan caller has
equivalent early-return protection. No barriers inside the merged render pass.

First variant retains **all LOAD/STORE operations**. Do not combine it with
`load_dont_care`, cache-format changes, direct resolves or relaxed synchronization.
Count eligibility, merged rectangles, fallback reasons and pass closures actually
avoided. Test MRT+depth, format reinterpretation fallback, sample counts and the
placeholder/early-return case; then image/audio verification and warm A/B.

### E3 — Restrict work to the conservative area of the entire pass

Source-derived examples from the exact visible trace (not measured tile traffic):

| Draw count | Calculated full framebuffer | Guest scissor |
| ---: | --- | --- |
| 94 | 160×8192 | 128×128 |
| 81 | 320×8192 | 256×256 |
| 170 | 280×2344, 4× MSAA | 256×256 |
| 291 | 1280×1024, 2× MSAA | 1280×720 |

`RenderTargetCache::GetRenderTargetHeight` (`cache.cpp:790`) uses the EDRAM
addressing period. The vendored MoltenVK `Commands/MVKCommandBuffer.mm:792–803`
uses renderArea right/bottom extents to set Metal renderTargetWidth/Height for
tile-memory preallocation. Therefore a smaller valid area can affect real host
work while preserving attachment allocation and visual resolution. Savings
remain unmeasured, and offset rectangles do not necessarily reduce allocation
in proportion to rectangle area.

Implement conservative union over **every** draw, transfer rectangle and clear
in a pass. Keep attachments full-sized and LOAD/STORE unchanged. Unknown coverage
falls back to full extent. Never clamp to 720p, the first draw or the last scissor.
Use the final host scissor/coordinates after offsets/resolution scale and include
transfer regions; handle empty, negative/overflowing and enlarged later bounds.

The existing `DeferredCommandBuffer` copies begin-rendering arguments into a
`std::vector<uintmax_t>` (`deferred_command_buffer.cpp:375,419`). A possible
implementation stores a stable **stream index**, accumulates bounds until pass
end, then patches the begin command before Vulkan encoding. Never retain a raw
pointer across vector growth. Validate command type/generation and finalize on
every end/switch/submission path; support legacy passes conservatively or leave
them full-sized initially. Verify no partially finalized stream is executed.

Screen first with counters for full versus union right/bottom extents, sample
area and fallback reasons. Test two draws where the second expands coverage,
transfers outside the first scissor, shadow targets, offset bounds, depth/stencil
and MSAA. Compare GPU pass cost and visual output. Promote ahead of E2 only if
measured pass-area waste is clearly the larger opportunity.

### E4 — Make the native Metal comparison valid

Change only the separate `tools/ios-metal-replay/main.mm` shell first: set
XeniOS `async_shader_compilation=false` **before** GPU setup. It currently defaults
true (`gpu_flags.cc:185`), and actual replay log line 29 confirms six asynchronous
workers. Metal command processor `:3070` skips not-yet-compiled draws. Its replay
completion promise waits for command processing, not missing-draw reexecution.
Sleeping after playback is not a fix.

Replay the same private visible XTR once, require no skipped/unavailable pipelines,
then compare output to `vulkan-exact-2060.png`, using broad structural and pixel
differences with documented floating-point/format tolerance. If still black,
inspect source render targets before gamma/presentation and find the first
divergent resolve. Keep this within a single correctness experiment initially.

The Vulkan control has `RestoreEdramSnapshot(...) {}` at XeniOS Vulkan CP `:178`.
Our trace carries a 10 MiB snapshot with 7,328,221 nonzero bytes; Metal implements
restore. Prefer same-ReXGlue Vulkan replay as the completeness control, or repair
and verify snapshot restoration in the isolated Vulkan control. Protocol layouts
match and the trace parses to EOF, so broad format rewriting is not justified.

Only after correctness compare **warm** identical workload GPU timestamps with
matched options. Standard TracePlayer backward seek clears caches, and current
elapsed replay time includes shader compilation/readback/file output; neither
is a warm FPS benchmark. Larger native-Metal integration is justified only by a
repeatable useful gain and a clean API/dependency plan retaining Theft4 AOT.

### E5 — Reduce the remaining copy interruptions by their actual cause

Label/count pass breaks for shared-memory upload, texture conversion/upload,
memexport index snapshot/readback, resolve readback, ownership/depth store and
attachment change. Include bytes, ranges and same-framebuffer-resume counts.
Use `SharedMemory::RequestRanges` batching where suitable; the vertex-fetch
loop currently calls RequestRange individually. Primitive built-in index upload
is once-only and is not an explanation for hundreds of recurring copies.

Target the dominant verified redundant work. Do not skip data invalidation,
remove real barriers, use stale textures, or disable readback needed by GTA IV.
Keep async compilation/pacing/queue-depth changes as separate experiments so
mechanism and correctness remain attributable.

## 4. FSI disposition and future correctness experiment

Leave FSI off. Its pipeline is substantive, not an empty scaffold, but the game
output is visibly corrupt. A narrow inconsistency exists in
`src/graphics/pipeline/shader/spirv_translator_rb.cpp:1491–1495`: documented host
samples 0/3 are remapped using an extract from bit 2. Pipeline/depth code uses
sample 3. A truth table differs on eight of sixteen masks; input 1001 yields
01 instead of documented 11. FSI forces 2x-as-4x and the game uses this path.
The same inconsistency exists upstream. This may explain coverage errors but
does not establish the cause of broad bands (fully covered masks agree).

If revisited, first test coverage/EDRAM semantics offline and inspect generated
MSL raster-order-group bindings. Then use an identical correct replay to isolate
the first bad resolve. Do not make another full-game FSI performance run merely
because a one-line coverage fix compiles.

## 5. Commands and verification contract for the next execution pass

These are instructions for later execution, not commands run during this pass.
Use a fresh output directory per variant, the actual connected iPad UDID and
the existing signing team. Capture baseline identity before rebuilding.

```sh
env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcodebuild \
  -project out/build/ios-device-release/LibertyRecomp-ALL.xcodeproj \
  -target Theft4 -configuration Release -sdk iphoneos \
  DEVELOPMENT_TEAM=YOUR_TEAM CODE_SIGN_STYLE=Automatic \
  CODE_SIGNING_ALLOWED=YES -allowProvisioningUpdates build -quiet

env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun devicectl \
  device install app --device YOUR_IPAD_UDID \
  out/build/ios-device-release/theft4/Release/Theft4.app

# Only AFTER the proposed E1 adapter is implemented:
env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun devicectl \
  device process launch --device YOUR_IPAD_UDID --terminate-existing \
  --environment-variables '{"THEFT4_DRAW_BOUNDS":"1"}' \
  com.theft4.bringup --theft4-start-game

# Mechanism capture at a known landmark; inspect its TOC and actual row span:
env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun xctrace record \
  --template 'Game Performance' --device YOUR_IPAD_UDID --attach ACTUAL_PID \
  --time-limit 10s --output out/UNIQUE_RUN/scene.trace --no-prompt

env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun xctrace export \
  --input out/UNIQUE_RUN/scene.trace --toc --output out/UNIQUE_RUN/toc.xml
```

Export each desired table separately using
`--xpath '/trace-toc/run[@number="1"]/data/table[@schema="SCHEMA"]'`.
Schemas: metal-gpu-intervals, metal-application-encoders-list,
metal-application-command-buffer-submissions, displayed-surfaces-interval,
gpu-performance-state-intervals, device-thermal-state-intervals.
Use the existing analyzer with `--gpu`, `--submissions`, `--encoders`, `--output`.
Verify actual GPU state/counters and row coverage; requested duration is not proof.

Fast screen: one warm matched A/B plus images; discard obvious regressions.
Promising candidate: three alternating-order warm runs, comparable power/thermal
conditions. Prefer >=10% improvement beyond run-to-run noise; smaller repeatable
wins can accumulate but must be reported honestly. Reject any visual corruption,
recurring hang, substantial audio regression or >5% priority-scene regression.
Retain simple opt-out/fallback until the entire intro and player-control sequence
pass. Do not uninstall the app to change settings or clear its data.

Final acceptance is unique game images paced around 33.3 ms through all intro
scenes and into player control, without tearing/stutter/hangs, under ordinary
Release launch. Review p95/p99 and recurring deadline misses, not just average
FPS. Preserve 720p, 16:9, projection, textures, effects, depth/shadows and audio.
Repeat a sustained thermal run and first-use/warm cache cases. Profiler timings
and replay correctness are intermediate evidence, never the final acceptance.

## 6. Primary guidance consulted

Apple explains how to inspect real display intervals and explicitly enable
GPU limiter counters in [Analyzing the performance of your Metal app](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app).
Per-pass counter measurements may serialize work; do not add them as if they
were an overlapped frame timeline ([counter-statistics guidance](https://developer.apple.com/documentation/xcode/analyzing-apple-gpu-performance-using-counter-statistics)).
Apple's [GPU scheduling and pass-management session](https://developer.apple.com/videos/play/wwdc2020/10632/)
supports investigating false dependencies and tile load/store traffic. The
specific opportunities above come from this repository and captured workload;
these documents alone do not prove which optimization will win.
