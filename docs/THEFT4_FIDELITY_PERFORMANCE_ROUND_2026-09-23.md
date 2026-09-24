# Theft4: preserve-fidelity pipeline investigation and experiment round

Date: 2026-09-23. Status: **reversible round-one candidate plus existing-update onboarding fix installed on both iPad and iPhone Air; automatic title-update detection confirmed in both device launch logs. Performance comparisons remain pending.**

## Existing-update onboarding follow-up

The launcher now uses the canonical installed-game validator when routing setup,
checking copied files, selecting an update, and returning from Files. It accepts
the verified retail base + TU8 pair or the exact supported prepatched executable;
it no longer insists on a separate `.xexp` for the latter. The saved completion
flag cannot suppress detection or bypass file validation. Foreground checks do
not run during gameplay, loading, an import/base check, or an open picker.

`python3 tests/ios/test_installation_routing.py` compiles and exercises the actual
Objective-C routing methods with Foundation UI/validator test doubles. Covered:
existing pair, prepatched executable, files arriving in the same process,
asynchronous recheck, invalid/missing files, stale preferences, duplicate checks,
idempotence and eight busy-state guards. Both test groups pass. This tests launcher
routing, not the unchanged canonical validator's cryptography. Signed Release
compilation, strict signature verification and `git diff --check` also pass.

Installed artifact: `out/diagnostics/onboarding-update-wL20Xk/Theft4.app`.
Executable SHA-256:
`44a31f16a92cd0166021f19c4acc2d880cfcbe63a5e17a92b15274515d13697b`.
Main identity remains `com.lukebrosious.theft4`, version 0.2.0, build 44; no
commit, release, TestFlight upload, or Lab installation was made.

Both physical devices reported `install.existing_update_detected` after the
in-place update (iPad PID 1114; iPhone Air PID 16767). The Air subsequently
entered game execution. Existing iPad saves and both profiles were backed up
beside the candidate and compared byte-for-byte equal after installation.
The Air had no startup save directory at the preinstall check. Base/update file
sizes and modification dates were unchanged on both devices. No app uninstall
or game-data replacement was performed.

## Reversible round-one implementation

Follow-up: automatic long/lightweight recording is now a **per-launch opt-in**.
The switch starts OFF on each fresh launcher session; the old persisted
`Theft4DetailedPerformanceCapture` preference is removed rather than restored.
The manual short profile remains independent and is started by double-tapping
the graph. No experiment or graphics preference is reset. The README submission
guide intentionally documents only that short profile.

`tests/ios/test_capture_defaults.py` passes for absent, OFF, and ON legacy values,
preservation of unrelated settings, explicit session opt-in, and independence of
the short-profile handler. The onboarding routing regression also passes.
The signed Release candidate is retained at
`out/diagnostics/capture-default-off-WpFMxy/Theft4.app`, executable SHA-256
`361f9db74337881626a86b28424c6f4364c9afe1925ee35944966021232f047d`.
This follow-up candidate has not yet been installed on either physical device;
the onboarding-fix app described above remains the last confirmed installation.

The next local candidate adds three independent System-menu controls. This is
still main Theft4, with no version bump, commit, GitHub release, or TestFlight
submission. Default configuration: **Shader Constant Reuse ON; Frame-Stage
Timing ON; Display-Aligned Submission OFF**. The pacing experiment is separately
selectable so the first gameplay comparison can isolate constant reuse. Restart
the app after changing optimization controls. Turn both optimization controls
OFF for the previous allocation/limiter paths; turn Frame-Stage Timing OFF for
the preceding publication-only capture. Leave the long-capture switch OFF for a
capture-off comparison.

The exact previous signed app is retained at
`out/diagnostics/round1-baseline-gryOxh/Theft4.app`, executable SHA-256
`b98ad69cec445526b5a23b876da50032d827f15c52c94b3bde6062171915b5a6`.
`pre-round-working-tree.patch` beside it preserves the pre-existing tracked
changes. It is a **baseline snapshot, not a patch to reverse blindly**. No user
changes, saves, installed game files, or shader caches were deleted.

### Constant reuse

`native_constant_projection.h` inspects final compiled SPIR-V for a conservative
subset of the buffer-address ABI: three 64-bit push addresses, static byte
offsets, integer-to-pointer conversion/bitcast and direct scalar/vector loads.
Unknown addresses, dynamic offsets, pointer escapes and unsupported accesses
fall back to the full-bank path. Read masks are unioned across both shader
stages and all accepted stock/override, early/late variants. No shader math or
instructions are changed.

A bounded per-frame-slot memo may reuse an immutable allocation only when the
same read mask is still valid and at most eight version ancestors prove that
all intervening changes are outside that mask. Full snapshots, missing ancestry,
slot reuse, shader/mask mismatch and failed proofs use the original binding
implementation. Partial views never enter the authoritative full-bank cache.
Existing optional hot-cache validation compares every potentially read register
and falls back to the complete allocation if the comparison fails.

The host corpus audit recognizes 1,016 of 1,356 stock cache entries before
runtime override unioning; this is eligibility, **not** a measured reuse rate or
performance improvement. Synthetic reflection/rejection tests and 20,000
randomized constant updates pass with AddressSanitizer/UndefinedBehaviorSanitizer.

### Lightweight stages

The existing long CSV additionally records guest-submit boundaries, limiter end,
worker publication begin/end, native queue-submit begin/end, CPU fence-wait
begin/end, successful distinct-content present requests, constant reuse/fallback
counts, and display-link predictions. Guest-frame IDs, submission IDs and content
sequence IDs connect the paths without mistaking the publication counter for a
guest-frame number. All stage timestamps share the host monotonic clock.

The new 8,192-record multi-producer ring uses a nonwaiting try-lock: contention
or overwritten records are explicitly reported as `stage_lost`. Producers never
allocate, format text, write files, or issue GPU timing queries. The serial
export queue drains batches. A four-producer/80,000-event stress test accounts
for every delivered or dropped event and checks fields for torn writes. Actual
device overhead still needs capture-off/on A/B measurement.

**Fence observations are not GPU durations. Display-link targets are predictions,
not presented callbacks or physical scanout.** `tools/analyze_frame_stage_trace.py`
joins only complete unambiguous pairs and retains these limitations in its JSON
report. The format remains readable for previous publication-only logs.

### Isolated pacing experiment

At 30 FPS only, an opt-in path places the limiter **before** the guest Present
command is submitted and initializes its phase from a UIKit display-link target.
The target is explicitly converted into the steady-clock domain used by the
limiter. The established fractional cadence then continues; after a full missed
interval the next deadline is re-anchored without adding a wait to the late frame.
Stale/unavailable predictions and other FPS limits use the original post-submit
path. This does not add frame-resource slots or intentionally omit draws. It is
not yet a render-cost predictor or a full Metal display-link/drawable integration.
It can change latency or throughput, which is why it defaults OFF pending A/B.

### Validation and next run

Final signed Release compilation passed, including mask-identity hardening and
newest-capture export priority. The trace stress/pacing tests also pass with
ThreadSanitizer and AddressSanitizer/UndefinedBehaviorSanitizer; the publication
ring regression and analyzer correlation/gap/duplicate tests pass. Executable
SHA-256: `9f868b4a4cace7e3eaab855b916d1b40d47e740af5e26bcf4dcb5f1566ded018`.
Candidate artifact directory: `out/diagnostics/round1-candidate-WxmKH0/`.
Bundle remains `com.lukebrosious.theft4`, version 0.2.0, build 44.
At the initial round-one handoff, iPad installation was pending permission to
close its running session (PID 978). The onboarding follow-up above supersedes
that installation status; performance/visual A/B verification remains pending.
The exact candidate was installed in place on iPhone Air on September 23 at
approximately 15:20 local time. CoreDevice confirmed installation of the main
bundle and successful launch; the new executable remained running (PID 16667).
No uninstall or app-data deletion was performed. Before installation, this main
app's Documents directory was empty and its startup save path was absent, so
save-file preservation could not be independently compared on this phone.
On-device gameplay and visual/performance comparisons remain untested.
No speedup or visual-equivalence result on the device is claimed yet.

Compare the same save, route, city-facing idle and facing-away idle with constant
reuse OFF/ON, pacing OFF, stage timing held constant; then repeat with pacing ON.
Check constant changed-version reuse counts, actual arena bytes, CPU stage tails,
publication p95/p99, consecutive slow runs, visible correctness and input feel.
Repeat capture-off checks before accepting the candidate. Reinstall the retained
baseline app in place if menu rollback is insufficient; never uninstall to roll
back, since uninstalling would remove app data.

## Saved lightweight trace (14:04 run)

The iPad's `publication-trace-2026-09-23-14-04-18.csv` was copied to `out/diagnostics/lightweight-2026-09-23-140418/` before closing the app. It has a `saved` footer, 3,518 contiguous published-frame IDs over 117.6 seconds, three user markers (`city-in-view` twice, `moving` once), no `lost` rows, and only nominal thermal-context samples. Across all 3,517 publication intervals: mean 33.44 ms, median 33.33 ms, p95 37.78 ms, p99 42.98 ms, maximum 74.81 ms; 90 exceeded 40 ms and 13 exceeded 50 ms.

The first 60 seconds were tightly paced (only one >40 ms interval, at 26.3 seconds). At about 72.4–73.2 seconds, 17 consecutive intervals exceeded 40 ms, many 50–75 ms: a true short slowdown cluster. In the final ~28 seconds, the mean remained 33.35 ms but p95 rose to 41.24 ms and 67/829 intervals exceeded 40 ms. Those later long intervals were typically followed by short ones (mean 24.6 ms after a >40 ms interval; lag-one correlation approximately −0.88). This is compensating *publication* jitter, not proof of the underlying GPU or physical display behavior. The trace contains no GPU timing, CPU phase, camera position, or scanout timestamps. The markers bound user-labeled phases but opening their menu may itself perturb nearby frames.

After copying the trace, Theft4 was closed as requested. The signed hold-control fix was then installed in place over the same `com.lukebrosious.theft4` 0.2.0 (44) app. The trace and active save directory were verified present afterward; the game remained closed. This is a local device update, not a GitHub/TestFlight release.

## Hold-control follow-up

The user's next run did **not** save a lightweight publication trace: the iPad's `Theft4DetailedPerformanceCapture` preference was `false`, and `Theft4/startup/frame-captures` did not exist. The previous long-press handler silently returned while the capture was inactive. A double-tap did complete a separate 600-frame native detailed profile at about 1:51 PM; that private capture is retained under `out/diagnostics/hold-gesture-2026-09-23/` with metadata `complete=true`. Its publication interval averaged 34.66 ms, p95 was 40.98 ms, and 48/600 intervals exceeded 40 ms. Scene/camera differ from the earlier central-city run, so this is not an A/B improvement claim.

The local fix makes a hold show an explicit inactive-state alert and offers to start a long capture during the current game, without resetting the presenter's sequence while it writes. Holding an active trace still offers markers and Stop; a stopped trace can resume. The revised host concurrency/restart test and signed Release build pass. Local executable SHA-256 is `b98ad69cec445526b5a23b876da50032d827f15c52c94b3bde6062171915b5a6`. It was installed after the user's session was stopped and the trace saved. It has not been published to GitHub or TestFlight.

## Implementation update (central-city capture)

The later M5 iPad capture is retained at `out/diagnostics/central-city-2026-09-23/` (ignored private data). It covers native frames 2349–2948. Its publication interval averages 41.87 ms, with p95 51.83 ms and 381/600 intervals above 40 ms. This is a detailed-profile run; it does not measure the user's capture-off 40 ms peaks directly. The first 200 frames are predominantly CPU/worker heavy (GPU envelope about 19–20 ms, worker pre-submit about 23–25 ms), while later city views have substantial G-buffer and GPU work as well. Draw commands average 3233 per frame versus 2129 in the earlier overlook capture. No quality-setting reduction is justified from these measurements.

The ordinary System capture switch now starts a low-overhead publication trace instead of automatically starting the costly 600-frame detailed profiler. A preallocated 16,384-slot ring is drained about twice a second to a dated CSV; force-quit can lose the most recent unflushed portion but not the whole run. The CSV explicitly labels publication timing, thermal/output context, overflow, and collecting/saved status. Double-tap of the frame-time graph still requests the short detailed profiler; long-press adds a scene marker or stops the trace. Gameplay A/B validation remains; this is not a claim of measured overhead reduction.

`tools/analyze_native_pass_graph.py` produces a bounded attachment chronology from the existing detailed CSV without affecting gameplay. On frame 2569 it finds 411 approximate GPU ranges, 655 shared-attachment chronology edges, and frequent reentries to main color/depth targets. It does **not** know all sampled reads, color attachments beyond RT0, subresources, aliases, barriers, load/store operations, or actual Metal encoder boundaries. Those unknowns block any safe resolve/store elision solely from this report.

The production Release target builds successfully and the trace ring's host-side concurrency test passes. The signed app was installed over `com.lukebrosious.theft4` 0.2.0 (44) on the M5 iPad; the on-device save directory and prior save exports were verified present, and the app was closed afterward. Local executable SHA-256: `a794ad6750f311fee8c53b52b3213a13d3769d996b8493a17549e9108b6be077` from source base `6ba83faa` plus the present dirty tree. This is an in-place local development build with the same version/build string, **not** a GitHub or TestFlight release. The new lightweight trace itself has not yet been exercised during gameplay or compared against capture-off pacing; no performance uplift is claimed.

The runtime log shows the default asynchronous pipeline path logging first-use `action=defer-draw`; the draw caller returns without replay when the pipeline is unavailable. At least the first 32 instances are logged in the current startup session, but the log throttles later instances, so the full omission count is unknown. This is a draw-completeness issue separate from the steady city slowdown. The setting remains reversible with `THEFT4_LAB_ASYNC_PIPELINES=0`; changing its default must be measured for first-use stall cost.

## Objective

Improve city-view frame pacing toward 30 distinct game frames per second while preserving the current appearance, world detail, simulation, audio, and input. Exhaust redundant-work removal, better data movement, earlier preparation, scheduling, and equivalent rendering methods before considering any reduction in visual settings. Longer initial loading is acceptable when it demonstrably prevents first-use stalls. Delaying background preparation is acceptable only while required content remains ready before use.

The earlier choice between preserving the image and pursuing performance was premature. The capture identifies expensive intervals; it does not prove that reducing quality is necessary. Each candidate below must establish its own benefit and visual equivalence.

This plan follows the current main Theft4 app. Historical Lab terminology in source names does not authorize a separate product or branch. Preserve the existing dirty working tree, installed app, saves, and comparison artifact. No release, version change, commit, install, or renderer setting change is part of this planning pass.

## Current evidence and its limits

The private capture is retained under `out/diagnostics/city-overlook-2026-09-23/`, which is ignored by Git. Capture ID: `2094478369`; 600 samples, native submissions 1390–1989, captured approximately 11:49:54–11:50:16 local time. The supplied and live screenshots are later than this capture; there are no camera markers linking an individual captured frame to those images.

Source inspected: `6ba83faa` plus existing uncommitted changes. This is source identity, not verification that the installed executable exactly matches it. Before the next A/B, retain and record the installed executable identity, corresponding symbols, source diff, dependency identities, device/OS, and effective settings.

The capture identifies Apple M5 GPU, two alternating frame-resource slots, 1280×720 render extent, and a 1920×1080 **PresentCommand display field**. The session startup log reports a 2416×1359 output policy. Those values come from different layers; record the guest logical extent, resolved scene size, upscaler destination, actual swapchain/drawable extent, and UI view extent separately before calling either value the final screen resolution. Do not silently change a setting to reconcile metadata.

Whole-capture statistics below exclude the first manual-attach sample. Percentiles use the sorted sample at `floor((n-1)*p)`; these are not physical scanout statistics.

| Measurement | Mean | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|
| Renderer publication interval (`cpu_frame_interval_ms`) | 33.63 ms | 39.43 ms | 47.48 ms | 56.27 ms |
| Approximate GPU queue envelope | 22.83 ms | 29.80 ms | 41.90 ms | 47.47 ms |
| Render-worker preparation before submit, wall time | 8.73 ms | 17.12 ms | 22.65 ms | 26.15 ms |
| Same worker preparation, on-core time | 8.54 ms | 16.35 ms | 21.54 ms | 24.34 ms |
| Slot fence wait | 0.26 ms | 0.004 ms | 13.29 ms | 18.51 ms |

There are 27 publication intervals above 40 ms and 16 GPU envelopes above 33.333 ms among those 599 samples. CPU and GPU work overlap across two slots; do not add these columns to reconstruct a frame.

The following windows describe the captured sequence, not matched camera/traffic states or an optimization A/B:

| Mean per frame | Before: 1640–1677 | Slow cluster: 1678–1689 | Later: 1720–1749 |
|---|---:|---:|---:|
| Publication interval | 33.59 ms | 47.02 ms | 33.04 ms |
| Approximate GPU envelope | 25.34 ms | 41.52 ms | 13.19 ms |
| Light setup + light accumulation ranges | 7.95 ms | 15.06 ms | 3.16 ms |
| Scene-to-G-buffer range | 5.44 ms | 6.97 ms | 3.63 ms |
| Resolve ranges | 2.75 ms | 4.72 ms | 1.39 ms |
| Water reflection ranges | 1.57 ms | 3.64 ms | 0.22 ms |
| Environment reflection ranges | 1.06 ms | 1.64 ms | 0.53 ms |
| Light setup draws | 71.5 | 84.2 | 47.4 |
| Water reflection draws | 159.6 | 580.5 | 49.7 |
| Environment reflection draws | 192.6 | 193.2 | 195.2 |

The dominant light shader family in the slow cluster is `deferred_lighting_vs1` for setup and `deferred_lighting_vs3` / `deferred_lighting_ps6` for accumulation. This is a concrete inspection target, not evidence that its shader instructions alone account for the measured interval. Environment-reflection draw counts barely change while their timings do; G-buffer draws are also fewer in the slow cluster than the preceding window. Work count alone cannot explain the slowdown.

Additional findings:

- Pipeline misses, pipeline creations, recorded compile time, and recorded pipeline wait time are all zero in this capture. More prewarming cannot directly remove a compile stall that did not occur here.
- Thermal state remains nominal; no texture allocation retries or failures are recorded. These facts do not prove constant GPU clocks or absence of memory-bandwidth pressure.
- Pacing wake-up overshoot averages 0.047 ms and peaks at 1.488 ms; four limiter late resets occur. A timer rewrite is not the first explanation for the 47–56 ms cluster.
- Queue depth peaks at 9,294 commands, and maximum observed command dwell reaches 58.05 ms. These are backlog observations, not an independent 58 ms cost to add to frame time.
- The GPU timestamps are explicitly approximate queue/encoder boundaries. CPU/GPU clocks are not calibrated in the export. GPU utilization, memory bandwidth, fragment cost, and hardware presentation intervals are missing.
- Detailed capture averages 97,126 CPU clock reads and 470 GPU queries per frame, with a 512-query budget. It omits about 48,435 CPU events per frame from the detailed event list while retaining aggregates, and drops some GPU attribution boundaries. CPU `profiler-bookkeeping` self time averages 2.63 ms in the slow cluster and 1.40 ms in the later window. This is only measured bookkeeping, not total instrumentation overhead. Existing query readback and snapshot times are small and do not represent all capture cost.

Conclusion: view-dependent rendering/transport pressure is a strong lead. We have not separated shader execution, render-pass/memory traffic, driver work, profiler perturbation, and queue waits sufficiently to choose an architectural fix from the pass names alone.

## Round 0 — make the measurement trustworthy

### 0A. Long lightweight capture and useful graph

Implement a three-minute capture with manual stop and a bounded rolling prehistory. Keep the 180-frame HUD graph inexpensive; long capture storage is independent of its display history.

Record per-frame numeric rows using preallocated storage: frame/content identity, producer-ready, worker-start/end, submission, fence completion, publication, and presentation identity when available. Use a coarse GPU envelope only if supported without forcing a wait or adding expensive pass boundaries. Keep a small fixed record budget, report overflow, and export in batches on a background queue. Periodic checkpoints must survive force-quit; never serialize files on the render/game thread. Final capture status must distinguish collecting, saving, saved, incomplete, and export failure. Preserve previous captures until a new one is durably saved.

Record low-rate context (about 1 Hz): thermal state, app memory, power/charging/low-power mode where available, resource residency, pipeline queue depth and readiness, texture/geometry uploads and evictions, and current effective output configuration. Reuse existing counters rather than rescan all resources every frame. Audit counter cost before enabling it continuously.

Add lightweight user markers: entering overlook, city in view, facing away, stationary, and moving. A marker records the current frame/time with a short ID; no screenshot or expensive game-memory scan is needed. Capture position/orientation/time/weather only after identifying a safe, authoritative read point; otherwise keep user markers.

The graph should distinguish publication interval, GPU envelope, and presented-content interval, with unavailable tracks explicitly labeled. Show recording duration and marker ticks. A single FPS number, latest interval, rolling peak, and full-capture percentile use different windows; label them so they cannot appear to contradict each other. Reuse `theft4_frame_counter_note_published` and the existing history, but do not relabel publication as display time.

Run the same stationary view with capture off, lightweight capture, and detailed capture separately. Proposed lightweight acceptance budget: median change below 0.3 ms and p95 change below 0.5 ms, provided repeated runs can resolve those differences; otherwise report an upper bound/noise floor. Check memory, power trend, and tail latency too. These are engineering targets, not measured overhead promises.

### 0B. Correlate the actual queue and display

Join guest frame, native submission, frame slot, command buffer, published content, and presented drawable. Query supported calibrated timestamps and retain calibration uncertainty; the vendored MoltenVK contains timestamp-calibration and `addPresentedHandler` paths, but the shipping binary/device feature exposure must be verified. Prefer existing Vulkan presentation-timing support when usable. Otherwise add a small bounded bridge to the actual MoltenVK submission/presentation path. The UIKit probe command queue is not a substitute for the game's queue.

Measure CPU running versus waiting, GPU start/end or envelope, slot-fence stalls, mailbox overwrites, drawable acquire waits, missed presentation deadlines, and duplicate displayed content. A presented callback is OS presentation timing, not an external physical scanout measurement.

A short Metal GPU capture/counter session can distinguish fragment/texture pressure, bandwidth, occupancy, and encoder overhead. Use it for diagnosis only; replayed/captured frames are not ordinary gameplay performance. Previous Instruments sessions disconnected, so verify one bounded session after a concrete connectivity/tooling check; if it still fails, continue using engine instrumentation rather than repeatedly retrying.

### 0C. Deep detail only where necessary

Keep the existing detailed capture available, but capture a short targeted window or selected frames around a marked heavy view. Log the mode and query budget with every result. A spike-triggered deep capture can observe subsequent frames, not retroactively recover the shader detail of a completed spike. Lightweight prehistory covers that gap.

Use CPU-only, coarse-GPU-only, and deep-GPU modes independently to quantify observer effects. Compare candidates in the same capture mode and accept performance improvements with detailed instrumentation off.

## Round 1 — reconstruct the resource dependency graph

The current attribution plan groups commands for timing. It is not a proof that passes can be reordered or removed. Add a diagnostic graph builder that observes a bounded set of frames without changing execution. Export graph construction outside gameplay where possible.

Each node needs pass/command identity, original order, render extent, sample count, actual host formats, color/depth/stencil reads and writes, shader side effects, target load/store actions, render-scope breaks and their reason, resolves/copies, and resource generations/subresources. Each edge must identify a required producer/consumer dependency, including overlapping guest placement aliases, partial regions, stencil, mip/face, external/guest readbacks, and retained reflection contents. Cross-frame edges matter with two active slots. Unknown access or aliasing is a dependency, not evidence of no use.

For each materialized surface record last writer, all consumers, next overwrite, lifetime/fence ownership, and whether old contents remain observable. Record each resolve's chosen operation and rejection reason for a faster route. Match logical Vulkan scopes to actual Metal encoders in a diagnostic capture; do not assume they are one-to-one.

Produce a dry-run opportunity report for:

1. Same attachments unnecessarily leaving/re-entering a render scope.
2. Full overwrites preceded by unnecessary loads or standalone clears.
3. Stores/resolves whose output has no observable consumer before overwrite.
4. Repeated resolves of the same unchanged source into the same destination subresource.
5. Redundant barriers or unnecessarily broad synchronization.
6. Preparation that could occur earlier without extending a resource's unsafe lifetime.

Every proposed removal includes its proof and fallback reason. Resource-graph correctness tests must cover aliases, partial writes, depth/stencil independence, persistent reflections, readback, resource release, and two-frame ownership. A second execution path is enabled only after the observer reports candidates in real city frames.

## Round 2 — separately switchable rendering experiments

### E1. Preserve attachments and reduce data movement

First candidate: retain compatible rendering scopes across redundant boundaries, and select cheaper load/store/clear behavior only when Round 1 proves equivalent contents. Extend existing `native_resolve_policy.h` logic; inspect whether the existing producer-depth resolve, full-overwrite handling, and FP16 resolve fusion actually fire before adding duplicates. An enabled feature is not proof it applies to this workload.

Apple recommends grouping compatible rendering commands and managing load/store actions to reduce tile-memory traffic. Treat this as a hypothesis to test on the current Vulkan/MoltenVK path, not a guarantee of savings. [Apple: Render with Metal](https://developer.apple.com/videos/play/wwdc2023/10125/)

Do not discard an attachment merely because a pass name says temporary. Blending, masked color channels, depth/stencil, later sampled aliases, and reflection persistence may require its previous contents. Do not substitute `STORE_OP_NONE` for a demonstrated lifetime proof.

Measure encoder count, load/store bytes estimated from formats/extents, actual bandwidth counters where supported, resolve-path counts, CPU recording, GPU envelope, and presented frame-time tails. Validate exact intermediate output for copy/clear/elision changes; compare final output and special effects too.

### E2. Equivalent local-light rendering

Use the VS1 → VS3/PS6 light pair as the first target. Determine whether cost comes from volume coverage, shader instructions/texture reads, stencil traffic, or pass/encoder transitions. Capture screen-space bounds, shader variants, stencil/depth behavior, shaded coverage, and actual fragment cost only in short diagnostic samples. Xcode's shader cost graph is an appropriate diagnostic when capture works. [Apple: shader cost graph](https://developer.apple.com/documentation/xcode/analyzing-apple-gpu-performance-using-shader-cost-graph-a17-m3)

Candidate order:

1. Reuse immutable per-light preparation/bindings where existing generation keys prove equality; measure CPU work saved.
2. Conservative current-frame light bounds/scissors; reject only volumes proven unable to affect any relevant target. Near-plane/camera-inside cases, stencil setup side effects, and unusual projection states fall back to full work. Scissors may add little when volume geometry already bounds rasterization, so require measured coverage reduction.
3. Specialize the translated shader for known invariant state, eliminate repeated calculations, and improve data layout while retaining precision, texture filtering, light equations, shadow behavior, and stencil semantics. Inspect generated Metal shader code before assuming a source-level rewrite improves it.
4. If bandwidth dominates, prototype a small tile-local G-buffer/light path for this proven shader family. The vendored MoltenVK has local-read/framebuffer-fetch machinery; query actual runtime support and validate its semantics. Screen-coordinate-local reads are candidates; arbitrary neighboring texture reads may prevent a direct conversion.
5. A tiled/clustered light list is a larger follow-up only if this evidence justifies it. It must include every contributing light and preserve shadow/stencil/material behavior. Floating-point blend order can change the image, so visual equivalence must be demonstrated rather than asserted.

Do not use previous-frame occlusion alone to remove current-frame lights during camera motion. No light count cap, distance reduction, precision downgrade, or stale lighting is an acceptance shortcut.

### E3. Reflection work based on dependencies and contribution

The water-reflection draw count is especially worth investigating: about 160 before the slow cluster, 580 during it, and 50 later. Record the capture camera/frustum, source transforms, destination generation, current consumers, reflected-object bounds, and invalidation reasons. Establish whether this is expected visibility or an overly broad capture; a main-camera hidden object may still be visible in a reflection.

Start with conservative culling in the **reflection camera**, removal of graph-proven unused outputs, and reuse of immutable preparation. Preserve the original capture resolution, AA, range, geometry detail, lighting, weather, and material appearance.

A cached reflection is eligible for reuse only if all inputs affecting its pixels are unchanged or no observer can consume the result before a replacement. Player/camera movement is not the only invalidation: traffic, pedestrians, animation, lighting/time/weather, shadows, water state, and any sampling transform matter. Reusing an old reflection on a fixed update interval is not considered fidelity-preserving.

If useful, separate static preparation from dynamic scene contributions. Reusing static *pixels* needs a proof of correct depth, blend order, illumination and visibility; stationary buildings do not guarantee an unchanged reflection. Prefer cached geometry/material preparation when pixel equivalence cannot be established.

## Round 3 — loading and scheduling without missing content

Treat first-use stalls and steady idle cost as separate problems. Run the same route twice in one session and compare cold/warm starts, with cache identities recorded. A warm city view still exceeding budget needs less per-frame work; preloading alone cannot solve it.

Instrument request → file read → decode → upload → pipeline ready → first consumer. Record bytes, queue depths, deadlines, main/render-thread waits, page faults where available, resource residency and memory pressure. Existing upload counters do not measure file I/O or decompression.

Test bounded prefetch from current camera direction, velocity, and known neighboring content; prioritize imminent consumers and back off speculative work when the renderer is busy. Move compatible decode/pipeline creation to background workers, publish ready immutable resources at safe boundaries, and retain originals until their last fence completes. Spreading upload work is allowed only when every required byte is ready before its consumer. Deferring a visible asset to a later frame creates pop-in and fails this round.

For pipeline warming, record the exact PSO recipes encountered on representative routes and prepare likely recipes during the loading screen/available idle budget. Test on-device persistent caches before introducing a new archive layer. Binary archives are device/GPU-specific, so key caches by device/driver/build/shader/layout identity and fall back correctly. Do not assume an M5 cache is portable to A19 or M1. [Apple: Metal binary archives](https://developer.apple.com/documentation/metal/metal-binary-archives)

Audit `gta4_native_async_pipeline_no_wait`: current startup enables it by default, and a pending pipeline can return null with `action=defer-draw`. Determine which visible draws are omitted and whether anything replays them; count omissions explicitly. Preserve draw completeness in candidates using advance preparation plus a correct first-use fallback. A synchronous fallback can expose cold stalls, which should be measured and moved into preparation rather than concealed. This path does not explain the latest warm capture's zero pipeline misses.

Do not defer physics, collision, audio, or required game simulation. Do not increase the frame queue to hide time without measuring input-to-presentation latency. Existing two-slot ownership remains part of the baseline for this round; frame-count changes require their own comparison.

## Execution order and acceptance

| Candidate | Concrete deliverable | Gate |
|---|---|---|
| A: measurement | Three-minute lightweight capture, markers, mode/extent metadata, frame identity correlation | Bounded overhead/storage; force-quit persistence; no gaps silently dropped |
| B: graph observer | One normal and one heavy-view dependency graph, scope-break and resolve-path report | Complete dependencies or explicit unknowns; no rendering changes |
| C: attachment experiment | One proven scope/load/store/resolve improvement behind an independent switch | Identical required contents and repeatable tail-latency improvement |
| D: lighting experiment | One measured VS1/VS3/PS6 preparation, coverage, or shader change | Same lights/shadows/material result; speed gain with deep profiling off |
| E: reflection experiment | One conservative reflection-frustum or dependency-based reuse change | No missing/stale reflected content during movement or scene changes |
| F: preparation experiment | Bounded loading/prefetch/PSO readiness candidate chosen from cold-run evidence | Fewer first-use stalls; complete draws; bounded memory and no new steady-state contention |

Candidates C–F are compared separately before combining accepted changes. Re-rank them after A/B evidence: the table is a default sequence, not a requirement to implement an ineffective change.

Use manual gameplay for now, as requested: the same save, route, camera sweep, city-facing idle, facing-away idle, and a return sweep. Record time/weather, traffic variation, power and thermal conditions. Run at least three paired baseline/candidate repeats and alternate their order. Repeat a route cold and warm separately; avoid clearing the user's caches or saves to manufacture a baseline. A dedicated test cache can be used when necessary.

Report median/p95/p99, worst frame, counts over 35/40/50 ms, missed presentation deadlines, consecutive slow-frame runs, input latency, memory and thermal trend. Report phase-local results instead of a whole-run mean that hides the overlook. Use uncertainty across repeats; require gains larger than that variation. Target eventual critical-path headroom around 28–30 ms for a 33.333 ms deadline, but do not claim that this round guarantees it.

Validate intermediate images and final appearance on the same frozen render state when possible. Short private Metal captures can help compare selected passes; the existing `tools/ios-metal-replay` is a separate experimental backend and is not already a faithful warm benchmark of this renderer. Replay evidence complements live gameplay, not replaces it. For algebraic shader changes, require justified numerical tolerance and no perceptible difference; never enlarge a tolerance merely to hide an artifact.

Visual regression scenes: city night lights, interiors, camera-inside-light volumes, wet roads/water, moving cars and pedestrians in reflections, shadow edges, alpha-tested foliage, glass, phone/radar/UI, rapid turns, day/night/weather changes, and save/load. Run resource lifetime/alias tests and foreground/background stability checks for affected paths. Preserve physics, audio and save behavior.

## Implementation map

- Capture/HUD: `ios/Theft4/main.m`, `Theft4FrameTimeView.m`, `ios/bridge/theft4_frame_time_history.h`, `theft4_metal_presenter.mm`.
- Actual gameplay submission/profile: SDK `src/graphics/gta4_native/graphics_system.cpp/.h`, `native_performance_samples.*`, `native_profile_detail.h`, `native_gpu_attribution.*`, `native_pacing_export.h`.
- Resource/resolve proofs: `native_resolve_policy.h`, `native_resolve_sync.h`, `native_aspect_content.h`, `native_reflection_registry.h`, `native_frame_context.*`, `native_submission_lifetime.*`.
- Light preparation and shader work: `BindCommonDrawState`, `RecordFrame`, shader translation/override path, `native_stencil_volume_policy.h`, and guest native hooks.
- Loading/PSOs: `PrepareFrameTextures`, `TryPrewarmDrawPipeline`, `GetOrCreatePipeline`, `native_pipeline_compiler.h`, `native_pipeline_recipe.h`, `ios/bridge/theft4_startup.cpp`.
- Presentation/calibration: Vulkan presenter and the vendored MoltenVK `MVKImage.mm`/`MVKDevice.mm`; verify public extension access before editing dependency internals. Existing dependency changes must be preserved.

The first implementation round is A and B plus the pipeline-completeness audit. It supplies the missing evidence needed to make C–F concrete, reversible performance improvements while retaining the intended image.
