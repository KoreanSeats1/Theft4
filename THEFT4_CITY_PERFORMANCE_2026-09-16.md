# Theft4 city performance checkpoint — 2026-09-16, 18:36–18:38 run

Follow-up: the counter correction and optional 1080p FSR output are now
implemented for a build-only handoff. The diagnosis below is historical;
device validation remains pending. See [the acceptance plan](THEFT4_1080P_OUTPUT_TEST_PLAN.md).

## Observations and limits

User reports playable city driving, with lower visual frame rate during fast
driving and long views containing substantial architecture. Physics do not
appear to slow. User finds enhanced 4× filtering subjectively comparable to 1×;
preserve 4× as the optimization target, not a quality reduction to claim a win.
This was not a synchronized same-route 1×/4× benchmark.

No live CPU/GPU trace was collected before the user left the app. The first
process query lacked `DEVELOPER_DIR` and failed; the corrected Xcode command
worked. All future device/profile commands must explicitly select
`/Applications/Xcode.app/Contents/Developer`. Saved runtime and lifecycle logs
were copied privately to `/private/tmp/theft4-city-perf-3mKEhM/`.

Latest runtime segment confirms GTA-IV-native rendering, Apple M5 GPU,
1280×720, 4× material anisotropy, and one active native frame-resource slot.
The descriptor subsystem's separate `frame-slots=2` capacity log does not
override that one-active-slot launch setting. Shader cache loaded 1,356 entries;
pipeline cache opened. Last audio report has 27,648 blocks, zero underruns,
rebuffers, dropped/clipped/nonfinite samples and recovery-silence frames.

Presentation-call milestones 1,500→3,900 span 95.015 seconds (about 25.26
calls/s). Individual 300-call windows range 23.43–27.61 calls/s. These are
coarse CPU presentation-call rates, NOT measured display scanout, frame-time
percentiles, or guaranteed distinct game frames. They support the reported
slowdown but cannot identify its CPU/GPU/streaming cause.

Lifecycle records pause at 18:38:31.097 and background at 18:38:31.962;
GPU wait cancellation/failure messages start at 18:38:32.379. Do not label
these post-background messages as the cause of foreground driving slowdown.
Process 3767 remained resident after the user left. It was terminated, and a
subsequent process listing confirmed Theft4 absent. No relaunch, build, install,
renderer change, save modification, or live debugger attachment occurred.

## Confirmed source-level FPS counter defect — not fixed yet

`VulkanPresenter::RefreshGuestOutputImpl` in
`glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp` assigns
`GuestOutputImageInstance::version` only when allocating a new image.
`guest_output_image_next_version_` starts at zero in
`glue/rexglue-sdk-main/include/rex/ui/vulkan/presenter.h`.
This is resource identity, not a fresh-frame sequence.

The iOS counter in the present path requires a nonzero image version different
from the last counted version. Thus valid allocation zero is always excluded;
fresh content reusing an image cannot reliably be distinguished from repeat
presentation. A synthetic 30-frame rotation through image IDs 0,1,2 counts 20
with the current predicate. This reproduces a mechanism for the reported
20-FPS overlay, not proof that this particular drive sustained 30 true FPS.
UIKit's elapsed-time division itself is not the discovered problem.

## Next implementation and test order

1. Add a dedicated successful-content-publication sequence carried with
   `Presenter::GuestOutputProperties`, assigned before mailbox publication.
   Count each distinct sequence once after a successful/suboptimal present.
   Do NOT repurpose image `version`, which also identifies GPU resource lifetime.
   Test valid first image, reused allocation, repeated presentation, failed
   refresh/present, and dropped mailbox frames. No readback or per-frame logging.
2. Keep 4×, 720p, existing visual effects, 30 cap and one frame in flight.
   On the next user-authorized play session, take a bounded 15–20 second
   `Time Profiler --all-processes` capture, then a separate bounded GPU/game
   trace while driving the long-view route. Do not overlap heavy profilers.
   Export/filter Theft4; verify executable UUID before CPU symbolication.
3. Select optimization from evidence: guest work/streaming, native render
   recording/driver CPU cost, GPU passes/bandwidth, or pacing/waits. Inspect
   existing caches and pass-merging before proposing duplicates. Do not enable
   two active frames in flight without resolving the prior resource-lifetime
   failure. No reduced view distance, resolution or effects as a hidden tradeoff.
4. Validate candidate versus baseline on repeated same-route, warm-cache runs;
   distinguish profiled diagnosis from unprofiled performance. Measure long
   frames and pacing, not just average FPS. Stop the app when the test ends.
