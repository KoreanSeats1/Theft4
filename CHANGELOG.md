# Theft4 engineering changelog

## 2026-09-22 — 0.2.0a Lab corrective release

- Keep Theft4 Lab’s promoted native renderer and performance implementation as
  the official Theft4 foundation; no legacy renderer path was restored.
- Add the first-launch installation route for creating the shared `game`
  location, detecting prepared base-game data, and guiding title-update
  selection/validation.
- Preserve the bounded detailed performance capture across relaunch and expose
  **Download Latest Log Capture** so TestFlight testers can export a dated
  diagnostic bundle from Files after reproducing a slow route.
- Publish a getting-started, sideload/TestFlight, and diagnostic workflow in
  [the 0.2.0a release notes](docs/RELEASE_0.2.0A.md). Performance and heat
  claims remain route/device dependent until supported by comparable captures.

## 2026-09-18 — Lab limiter and queue timing (build 9)

- **Opt-in, device test pending:** extend the manual Lab capture with present
  producer timestamps, limiter deadlines/sleep/wake/lock timing, and separate
  worker mutex, condition-wait, batch-transfer and dispatch measurements.
- Preserve the existing pacing decision and rendering settings. Store at most
  1024 present observations in memory and export after capture; no per-frame
  file writes or formatting. Report dropped records and capture boundaries.
- Add clock-domain and frame-identity checks to the capture analyzer. See
  [the capture procedure](docs/THEFT4_LAB_PACING_CAPTURE.md) for the next playtest
  and interpretation limits. This preparation does not install or launch an app.

## 2026-09-17 — Lab manual frame-timing capture (build 8)

- **Opt-in:** Launch Theft4 Lab with `THEFT4_LAB_NATIVE_PROFILE=1`, reach the
  desired gameplay scene, then double-tap the FPS counter to request the existing
  600-frame CPU/GPU capture. The counter turns orange when requested. Only one
  capture is supported per process; completion and timestamp validity must be
  checked in the diagnostic export before drawing conclusions.
- Profiling no longer automatically consumes its sample window during loading.
  The control is compiled only into the Lab native-renderer app. Ordinary launches
  retain profiling disabled. Rendering settings and the main app are unchanged.
- Build, isolation checks and on-device capture validation are recorded with the
  private Lab artifact. Instrumented FPS is diagnostic evidence, not acceptance
  performance; repeat the route with profiling disabled for the control.

This file is the durable, public engineering record for Theft4. It records
implementation changes, experiments, validation evidence, and known limits. It
is intentionally more detailed than a release-note summary because this project
is an active reverse-engineering and platform-porting effort.

Status labels used below:

- **Validated** — built and exercised on the stated hardware or by the stated test.
- **Opt-in** — implemented behind a build or launch switch; not the normal default.
- **Rejected** — tested and found unsuitable as the normal path; retained only
  when it remains useful for controlled experiments.
- **Pending visual verification** — telemetry or logs are promising, but a human
  has not yet confirmed the complete rendered result at the acceptance checkpoint.

Private game files, title updates, saves, screenshots, GPU captures, signing
material, and device logs are never part of this changelog or repository.

## Unreleased — work after `440d505c` — 2026-09-16

Comparison base: [`440d505c`](https://github.com/KoreanSeats1/Theft4/commit/440d505c964bbe4cb51a0217e162bf1eaa4de23e),
the public native-renderer and performance-documentation checkpoint.

### Installed, device acceptance pending — motion-blur option — 2026-09-17

- Added **Display → Motion blur**, default **on**, persisted independently as
  `Theft4MotionBlur`. Existing users retain the original appearance. The launcher
  publishes `THEFT4_MOTION_BLUR=1|0` before startup and locks the switch once the
  one-shot runtime launches. Output resolution, SMAA, FSR, 4× filtering, audio,
  two-slot scheduling and resource-retirement fixes are unchanged.
- Traced TU8 `sub_822CFC00` through its final `sub_822CF300` call at LR
  `0x822D0C48`: blur uses `base+11`, non-blur uses `base+10`, retaining the
  DOF/noise/alternate-composite base. An iOS-only strong AOT wrapper remaps
  `11→10`, `13→12`, `25→24`, `27→26`, `29→28` only at this exact caller, with
  the expected target, readable effect and matching technique handle. Unknown
  passes remain unchanged. The original helper binds each variant's own shader,
  constants and sampler layout. No generated game code or installed assets edited.
- This avoids merely zeroing a blur scalar while leaving the blur shader's
  samples active. It does **not** remove upstream preparation, motion-vector
  work, depth of field, bloom or tone mapping. The iOS shader override table is
  empty, so editing the desktop replacement HLSL would not implement this feature.
  Desktop presentation hooks are not linked into this iOS target; their existing
  pass-selection boundary informed this separate, narrowly scoped wrapper.
- Optional `THEFT4_MOTION_BLUR_TRACE=1` records at most 32 composite observations;
  it is off in normal play. No continuous per-frame retail logging was added.
- Validation: exhaustive small-domain pass/gate/idempotence checks and strict
  setting-parser tests passed with warnings-as-errors; isolated simulator UI
  preview compiled and was visually checked; signed device Release build passed;
  symbol inspection confirms the strong wrapper and original AOT implementation
  coexist. Initial link attempt used a stale generated project; regenerated CMake
  with its bin directory on PATH (required by the setup prerequisite check), then
  built successfully. Existing SDK precision/assembly and UIKit deprecation
  warnings remain. Executable SHA-256:
  `ff1dc78846d5d741a142a1d34aaebc800321536a599a6c44911fd81c60a0d840`.
- Initially built without installing; subsequently **installed in place on the
  user's cue**, with the app confirmed closed afterward. No game launch or data
  edits performed during installation. No measured FPS gain
  claimed. Blur/no-blur visual equivalence for other effects and sustained city
  A/B performance still require device playtests. See the
  [investigation and test plan](docs/THEFT4_MOTION_BLUR_AND_STREAMING.md).

### Installed, acceptance pending — two-frame GPU buffer quiescence

- First short device playtest: user reports good performance and no foreground
  crash, with occasional high-speed-driving hitches. Approximately three minutes
  including startup/loading reached presentation 5100 and survived five buffer
  drains without Invalid Resource. Extra fence waits peaked at 121.2 microseconds;
  this excludes destruction cost. Actual output was Boost 2416×1359, not 1080p.
  Most sampled intervals were near 29–30 FPS, with two near 24–25; locked frame
  pacing and the ten-minute foreground gate are not established.
- Backgrounding then reproduced the separate Metal permission failure, after
  recorded pause/background callbacks. Captured evidence and stopped the app.
  Motion-blur causation is unproven. The user's latest instruction is build
  only—no new installation or launch. No further runtime optimization was
  stacked onto the lifetime candidate during this observation pass.
- The texture-quarantine candidate also failed: Metal `Invalid Resource` at
  native frame/submission 4137, about 34 seconds before the first app pause.
  The incoming call is not established as the trigger. Presentation milestones
  support approximately 30 FPS average before failure, not a locked/stable 30.
- The linked MoltenVK implementation declares all live addressable buffers for
  physical-address shaders, including other-slot allocations. The trace shows
  completed-slot buffer destruction shortly before failure. This is the next
  hypothesis, not a proven root cause or evidence of defective texture assets.
- On Apple, batch retired upload/constants/persistent buffers for cleanup before
  recording with both native slots complete. Failed completion keeps allocations
  alive. Drain triggers are 32 MiB, 256 buffers, 120 submissions, or an already
  completed other slot. Normal frames retain overlap. These triggers are not a
  hard memory cap. Geometric constant-arena growth reduces resize churn.
- Diagnostics report drain counts/bytes/wait cost; memory accounting includes
  the pending batch. No shared XeniOS driver change or visual/audio downgrade.
  Host checks passed 36 cases / 461,192 assertions; signed Release build passed.
  SHA-256 `e20a6dc7dda50e043ba6f9699c5f41b43fd1541e4fb2eb1e23bfa06f8714e6f5`,
  UUID `63C3D9BD-F75B-3188-9F33-3C093DE1ADE4`. Device stability and reclamation
  hitch cost remain unverified. See the [investigation/test protocol](docs/THEFT4_TWO_FRAME_RESOURCE_LIFETIME.md).
- Installed in place and launched with the bounded GPU failure recorder, without
  LLDB/Metal validation. App data was not uninstalled or removed. Launcher
  startup is not gameplay acceptance; the foreground city soak is still pending.

### Rejected as sufficient — two-frame texture-retirement-only candidate

- The first two-frame candidate delivered the intended performance gain—the
  user reported a solid 30 FPS—but failed the stability gate with frozen video
  and continuing audio. The GPU flight recorder identified the first concrete
  failure at native slot 1 / frame 1649 / submission 1649: MoltenVK reported
  Metal command-buffer error 9 (`Invalid Resource`) and lost the Vulkan device.
- Correlation against the preceding frame showed frame 1650 completing slot 0
  / submission 1648 and immediately releasing a large group of texture images
  and views while slot 1 / submission 1649 was still executing. Explicit
  last-use tracking contained no exact hazard match, consistent with Metal
  argument-buffer resource references surviving beyond the renderer's direct
  image-use record.
- Changed `RetireNativeTextureImage` conservatively: a retired texture is now
  quarantined through the latest already-committed submission as well as its
  explicit last use. With two slots this delays destruction by at most one
  submission while preserving CPU/GPU overlap; it does not serialize every
  frame or disable the performance gain.
- Added a focused regression test for the observed sequence
  (`last_used=1648`, `current=1650` retires through submission 1649). All 33
  focused frame, submission and descriptor cases passed (459,117 assertions).
- Built, signed, installed in place and launched the corrected ARM64 Release
  app without removing saves or imported data. Executable SHA-256 is
  `df914b60f653d1969bcd3bdd910bbbdaabc993b7b9f790a11d8bc5ddd64469d7`;
  Mach-O UUID is `18BD2405-8D9B-32BB-8AB8-69C989FA4F53`. Foreground city stress
  subsequently failed at submission 4137. The preceding correlation did not
  prove texture causation; this change alone was insufficient.

### Rejected on device — initial isolated two-frame overlap candidate

- Changed only the iOS native-frame-slot default from one to two for the first
  paced-30 implementation experiment. The existing
  `THEFT4_NATIVE_FRAMES_IN_FLIGHT=1|2` launch override remains the immediate
  control/rollback mechanism; graphics quality, 1080p FSR output, 4× filtering,
  audio policy, game logic and corruption checks are unchanged.
- Reviewed the live integration before building: the two slots independently
  own command pools/buffers, upload and constant storage, descriptor pools,
  query/readback state and submission tracking. Exact-slot completion remains
  required before reset/reuse. This establishes a credible preflight, not proof
  of device stability.
- Built the ARM64 host test target and passed 32 relevant frame-context,
  submission-lifetime and descriptor-lifetime test cases (459,111 assertions).
- Preserved the prior signed one-frame app before building: executable SHA-256
  `1368354c3ec751eded3027cf606927b6d1e2650fd6c72a4723137f4b9c80cab5`.
  Signed Release candidate SHA-256 is
  `ca4c777f718d7c32fc412688554761021cec416edd5ea3f8815b9917a17124a5`;
  Mach-O UUID is `23AA50C7-AC3E-38D8-BF1B-62ABBB6CFF5E`.
- Installed in place on the 11-inch M5 iPad without uninstalling or deleting
  app data. It produced the expected near-30 FPS gameplay but is rejected in
  this form because video froze while audio continued. Its captured failure is
  the evidence used by the corrected texture-retirement build above.

### Planned, with P0b now executing — ordered paced-30 CPU performance passes

- Turned the annotated native-renderer CPU audit into a concrete implementation
  sequence: preserve/reference timing; canonical state fingerprints with checks
  retained; reduced command movement; reuse of warm pipeline preparation;
  conditional residual bottlenecks; matched city/soak acceptance.
- Specified source/test files, correctness boundaries, measurement fallback,
  baseline preservation, per-candidate rollback and keep/reject/inconclusive
  decisions. CPU savings and demonstrated frame-pacing gains are reported
  separately. No promise of locked 30 from a single optimization.
- User follow-up adds an early **one vs two frames in flight** A/B via the
  existing launch override. One is a rollback baseline, not a permanent
  restriction. Documented lifetime preflight, effective-slot verification,
  timing/latency/memory checks and stability gates before changing the default.
  The later P0b execution above implements the slot-count candidate; the rest
  of the plan remains unimplemented.
- Added the [implementation plan](docs/THEFT4_30FPS_IMPLEMENTATION_PLAN.md) to
  README navigation and the older performance plan; updated the local handoff.
  This bullet records the earlier planning pass; see the installed P0b entry
  above for the subsequent runtime change, build and device action.

### Diagnosed, not implemented — native-renderer CPU efficiency audit

- Profiled the existing signed Release build during user-confirmed 1080p city
  gameplay with Instruments Time Profiler, verifying executable UUID before
  symbolication. About 58.853 sampled running CPU-seconds over a 21.003-second
  trace: native render worker 14.945, game render producer 10.460, audio mixer
  7.380 and main guest thread 4.537. These are CPU weights, not frame latencies.
  Missing stacks/system symbols and sampling limitations are documented.
- The two main rendering CPU threads account for roughly 43% of app CPU.
  Command ownership/moves, hashing, state preparation and repeated pipeline
  preparation are measured candidates. Older generic-renderer profiles are not
  used as the current native backend's ranking.
- Normal runtime logging emitted six messages (~1.5 KB) in the CPU capture;
  recognizable output paths contributed ~0.01% of sampled CPU. Detailed
  diagnostics are gated, but fixed-state integrity fingerprints still perform
  49 chained hashes per calculation in normal execution. Recommended first A/B:
  a canonical packed fingerprint preserving all corruption checks, followed by
  reduced command movement and warm pipeline preparation. **Not implemented.**
- Confirmed Release/O3, AOT, `-mtune=apple-m5`, existing batching, audio backoffs,
  one-frame resource safety, 30 cap and menu-scene retirement. No speculative
  priority changes, fast-math, visual reductions or extra frames in flight.
- Game Performance and System Trace captures disconnected; no valid new GPU
  wait/scheduling timeline was obtained. Sparse frame milestones show missed
  30 FPS windows, not locked 30 or a measured tail percentile. Audio summaries
  remained free of underruns/rebuffering. Stop claiming causal CPU/GPU timing
  until the missing trace or bounded in-engine timings are available.
- Added an ordered, source-linked audit and experiment/acceptance plan to the
  README engineering record. No runtime source changes, build or install in
  this pass. Collected final logs, closed Theft4 and verified it stopped; private
  captures and game data are not committed. See
  [CPU performance audit](docs/THEFT4_CPU_PERFORMANCE_AUDIT.md).

### Installed after cue — After Hours launcher and Experimental FSR Boost

- Replaced the bring-up scroll screen with an original **After Hours / Liberty
  City Archive** launcher: condensed editorial typography, ink/bone/sodium-light
  palette, numbered Play / Display / System navigation and one real game-start
  action. Existing preparation, core restart, diagnostics, FPS, touch and
  independent 4× filtering settings remain connected to their original owners.
- Added an isolated SceneKit/Metal miniature city: procedural window textures,
  custom suspension-bridge geometry, moving headlights, reflective water,
  shadowed lighting, fog, HDR bloom, depth of field, slow camera drift and
  drag-to-orbit. Rain is a Core Animation emitter. All artwork is generated by
  repository code; no proprietary assets, remote services or music are used.
- Menu rendering pauses on scene deactivation and respects Reduce Motion.
  The scene, camera, emitter and view are released **before** starting the game
  runtime. This is a launcher-only renderer; it does not replace or modify the
  game's Vulkan/MoltenVK path. SceneKit is deprecated in newer Apple SDKs but
  remains available; its use is deliberately confined to a replaceable view.
- **1080p FSR now defaults on** when no value was saved; explicit user preferences
  remain intact. User reports that the installed 1080p mode looks excellent.
  This is subjective visual confirmation, not a new measured FPS result.
- Added default-off **Experimental FSR Boost**. It selects the largest integer
  16:9 output fitting the game view's native pixel extent at launch (for example,
  2752×1548 on a full-landscape 2752-pixel-wide display). A 1080p floor handles
  small windows/missing geometry; a 3840×2160 ceiling bounds future displays.
  The selected extent stays latched for that game session, including rotation.
- Separated logical video mode from physical output extent. Both FSR modes keep
  logical video at 1920×1080 so the existing Quality hooks still produce
  **1280×720** scene targets. Only the swapchain/presentation target grows.
  Existing chained EASU passes handle >2× enlargement, followed by RCAS;
  SMAA/high, 4× filtering, one native frame in flight, audio and saves are untouched.
  Boost is not temporal reconstruction, frame generation or native-resolution
  scene detail. Larger output may cost GPU time and is experimental.
- Added a standalone simulator launcher harness under `ios/launcher-preview`,
  with a separate bundle ID and no game/runtime linkage. Play and Display screens
  rendered and were visually inspected on an iPad simulator. Strict host tests
  pass for native-fit/fallback/4K-cap/unchanged-render-budget policies and the
  publication FPS counter. Signed ARM64 Release build and signature validation
  pass. No installation or launch on the physical iPad in this pass.
- Subsequent **"Install now"** cue: stopped the old Theft4 process and installed
  the verified candidate in place, retaining bundle identity and app data.
  No uninstall or automatic game launch. System/scene-retirement preview also
  inspected; the dedicated simulator was shut down after verification.
- Remaining acceptance: physical-device menu navigation, landscape/compact and
  accessibility layouts, menu-to-game handoff, Boost output-route log, moving
  camera clarity, HUD text and heavy-city pacing compared to regular 1080p.
  See [launcher and Boost notes](docs/THEFT4_LAUNCHER_AND_FSR_BOOST.md).

### Installed — optional 1080p FSR 1 output and FPS correction

- Added a persisted **1080p enhanced output (FSR 1)** switch to the Options tab,
  initially default off (now on, as documented above). The shared iOS output policy keeps the game at 1280×720 in both
  modes; enabled output uses 1920×1080 through the existing EASU/RCAS shaders.
  The native hook's 1.5× Quality ratio, presenter effect and CAMetalLayer size
  are selected coherently before startup; later UIKit layout retains the mode.
- Existing SMAA/high, independent 4× filtering switch, 16:9, one native frame in
  flight, 30 cap, shadows, reflections, physics, audio and save handling are
  unchanged. This is spatial upscaling of the game image (including HUD), not
  frame generation or true native 1080p. Extra GPU cost remains to be measured.
- Replaced image-allocation-ID FPS detection with a content-publication sequence
  carried in the existing release/acquire mailbox. Counts advance only for new
  successful presentations; first image, repeats, failed presents, inactive
  output and dropped publications are handled without altering resource IDs.
- Strict-warning host tests pass for the two output policies and counter logic,
  including the former 30→20 undercount scenario. Sparse existing milestones
  now report content and unique counts; one startup route log identifies actual
  input/output sizes and FSR passes. No per-frame logging/readback is added.
- Build-only handoff requested: do not install or launch until the user's cue.
  Xcode 27 signed ARM64 Release build succeeded; signature verified and new
  settings/diagnostic strings confirmed in the bundle. Existing development
  bundle identity is preserved. No installation or launch was performed.
  **Subsequent installation:** after the user's cue, rechecked the candidate
  hash and installed it in place under the same bundle identity, without
  uninstalling or deleting app data/saves. App verified stopped afterward;
  no launch or debugger attachment performed.
  Visual correctness, in-game counter behavior and heavy-city pacing are pending.
  [Implementation and A/B acceptance plan](THEFT4_1080P_OUTPUT_TEST_PLAN.md).

### Diagnosis preceding the FPS correction

- Source inspection found that the overlay uses reusable image-allocation IDs
  as frame freshness and excludes valid allocation zero. A synthetic rotation
  through three image IDs counts 20 of 30 fresh frames. This invalidates earlier
  claims that the overlay is an authoritative distinct-game-frame measurement;
  it does not establish a measured 30-FPS device result.
- The latest city session used 4× filtering and one active native frame slot.
  User reports 4× subjectively comparable to 1×, with remaining slowdown in fast
  driving/long architectural views. Audio counters remained healthy. Coarse
  presentation-call windows were approximately 23–28 calls/s; no live CPU/GPU
  trace was captured before the user left the app. No optimization gain claimed.
- The publication-sequence correction is implemented above; bounded CPU/GPU
  capture of that route remains pending, preserving visuals and 4× filtering. Details and
  verification criteria: [city performance checkpoint](THEFT4_CITY_PERFORMANCE_2026-09-16.md).

### Validated packaging — Theft4 app icon

- Added an original Theft4 icon built around a graphite numeral 4 and suspension-
  bridge silhouette, with no Rockstar or game artwork. The master artwork and a
  complete iPhone/iPad/App Store `AppIcon.appiconset` are versioned under
  `ios/Theft4`.
- Wired the asset catalog into the CMake-generated Xcode target using
  `ASSETCATALOG_COMPILER_APPICON_NAME=AppIcon`. The signed Release bundle was
  verified to contain the compiled `Assets.car` and installed in place on the
  iPad as `com.theft4.bringup`, preserving the existing app container and saves.
- TestFlight archiving temporarily modified generated Xcode output while this
  build was in progress. Regenerating from `ios/CMakeLists.txt` restored the
  project source of truth and prevented the development build from changing
  application identity.

### Installed — balanced 4× anisotropic filtering; subjective device comparisons

- Theft4 now selects 4× material anisotropic filtering before the native iOS
  renderer initializes. This sharpens roads, sidewalks and other textured
  surfaces viewed at oblique angles without changing output resolution, SMAA,
  the 30 FPS cap, frame scheduling, game data or saves.
- A same-device visual A/B found 1× smooth in heavy views while forced 8× made
  camera rotation visibly choppier for a barely noticeable fidelity gain. The
  enhanced launcher mode therefore uses 4× as the balanced intermediate point;
  8× is no longer the user-facing default.
- The startup bridge accepts `1x`, `2x`, `4x`, `8x` and `16x` values for
  `THEFT4_ANISOTROPY`, but the current UIKit launcher overwrites that environment
  value from its switch before startup. External launch overrides therefore
  are not a reliable A/B control until precedence is corrected; verify the
  effective runtime log. The shared renderer's upstream 1× default remains
  unchanged for non-iOS frontends.
- The launcher now separates **Play** and **Options** with a native UIKit tab.
  Options contains persistent switches for the FPS counter, on-screen controls,
  and enhanced 4× anisotropic filtering. Disabling the filtering switch selects the
  renderer's original 1× material sampling before game startup; it does not
  modify game files, saves or the shared desktop renderer default.

### Validated diagnostic — bounded iOS GPU wait and flight recorder

- Added a timeout-capable `VulkanSubmissionTracker::AwaitSubmissionCompletion`
  overload while preserving the existing infinite-wait API for other callers.
  GTA-IV-native frame-slot reuse now uses a five-second fence timeout on iOS
  only; desktop behavior is unchanged. `VK_TIMEOUT` records the pending fence,
  requested submission, and completed submission through the GPU flight
  recorder instead of silently blocking the render thread forever.
- `THEFT4_GPU_FLIGHT_TRACE=1` now creates a unique trace path under Theft4's
  sandbox for each controlled launch when no explicit recorder path is supplied.
  Normal icon launches do not enable the recorder, so its high-volume event ring
  is not a permanent retail-play cost.
- **Signed-device validation:** the Release build completed and was installed in
  place without replacing app data. The controlled one-slot run reached city
  gameplay and remained healthy through native submission 4,779. Audio reported
  zero underruns, rebuffers, drops, clipping, or non-finite samples.
- This run ended on a separate, conclusive lifecycle failure rather than the
  watchdog: the lifecycle log recorded `core.paused` and `scene.background`,
  after which iOS rejected Metal work with
  `kIOGPUCommandBufferCallbackErrorBackgroundExecutionNotPermitted`. MoltenVK
  reported `VK_ERROR_DEVICE_LOST`; the 65,536-event flight capture identifies
  `driver.device-lost` as its first failure. Therefore this run does not
  reproduce or disprove the earlier foreground fence hang. It does prove that
  app deactivation currently continues submitting GPU work and irrecoverably
  loses the device. The dead process was terminated after evidence capture;
  saves were preserved.
- **Post-run health audit:** before that lifecycle event there were no producer
  stalls, allocation failures, shader/pipeline failures, placeholder draws,
  positive audio-underrun counters, or native fence timeouts. The persistent
  shader cache loaded 1,356 shaders and its Vulkan pipeline cache opened
  successfully. `SGTA400` was opened, mounted, closed, and reopened cleanly,
  completing the previously pending relaunch/read side of save persistence.
  Missing `cache:`/`cache1:` probes, optional update shader files, leaderboard
  data, and variant-suffixed audio configuration probes remain non-fatal guest
  fallback behavior in this observed run. `surface-address-alias-create` lines
  are currently emitted at error severity for renderer diagnosis even when
  resource creation succeeds; they expose real Xbox EDRAM alias complexity but
  are not themselves API failures.

### Experimental — native transport batching and device-loss reproduction

- **Current iOS default — one native frame in flight:** per user request,
  `ios/bridge/theft4_startup.cpp` now explicitly selects one native frame slot
  before runtime initialization even when no launch environment is supplied.
  App-icon, Xcode and test launches therefore use the same one-slot path unless
  explicitly overridden with `THEFT4_NATIVE_FRAMES_IN_FLIGHT=2`. Values other
  than `1` or `2` still fail startup. Logs distinguish the default from an
  override. Native renderer selection, resolution, effects, save paths and the
  shared desktop renderer default are unchanged. This supersedes the two-slot
  default described in the historical test entries below; sustained stability
  and app-switch correctness remain unproven.
- **One-slot city hang reproduced:** after successful saving and several minutes
  of city gameplay, the user saw a van intersect the ground, heard an off-screen
  explosion/fire, then video stopped while fire audio looped. The current
  one-slot launch was confirmed in the log. Presentation stopped at count 7,500
  at 15:58:12; audio continued cleanly through at least block 60,416 at 15:59:13.
  There was no Vulkan error, device-loss callback, failed-publish record, or GPU
  wait failure. Lifecycle evidence shows a brief app deactivation occurred about
  23 seconds *after* presentation stopped, ruling it out as the initiating event.
  The scene correlation is not yet proof of an explosion/fire renderer defect.
- Code inspection explains a plausible silent failure mode: one-slot reuse calls
  `VulkanSubmissionTracker::AwaitSubmissionCompletion`, whose Vulkan fence wait
  currently uses `UINT64_MAX`. A submitted command buffer whose fence never
  signals can therefore block the refresh/render thread forever without logging
  a timeout. This precisely fits the evidence but does not yet identify the bad
  command/resource. The next diagnostic should add a bounded native fence
  watchdog plus a failure-triggered GPU flight trace, then reproduce; normal
  frame performance must remain unaffected. Frozen PID 3568 was stopped after
  logs were preserved.

- **Built, pending gameplay verification — launch display/input settings:**
  `ios/Theft4/main.m` now has persistent **Show FPS counter** (default on) and
  **On-screen controls** (default off) switches above Start GTA IV. FPS measures
  the existing published-game-frame counter; no renderer options or resolution
  are changed by these switches.
- `ios/Theft4/Theft4TouchControls.m` adapts XeniOS's BSD-licensed FPS Compact
  button positions and basic analog math into a standalone UIKit multi-touch
  overlay: movement stick, swipe-to-look, ABXY, bumpers, triggers, Back/Start,
  explicit D-pad and stick clicks. It does not import the XeniOS editor,
  emulator, TOML layout store, or secondary hold/double-tap gestures. Provenance
  and full license are in `ios/Theft4/XeniOS-Touch-LICENSE.txt`, also bundled in
  the app. Touches reset on hiding, cancellation, geometry changes, and scene
  deactivation. The camera timer exists only while touch controls are active.
- `ios/bridge/theft4_touch_input.{h,cpp}` provides mutex-protected host-endian
  snapshots. The user-0 Xbox adapter merges digital buttons, maximum trigger
  values and the strongest complete stick vector with physical input, then
  converts to guest-endian fields. Packet numbers change with merged state.
  Other controller slots and physical-controller haptics are unchanged.
  `ios/tests/touch_input_test.cpp` passes with Clang C++17 and warnings-as-errors:
  simultaneous touch/physical input, neutral release, stable/changed packet
  numbers, and signed-axis magnitude overflow. Signed iOS Release build passes;
  layout comfort, simultaneous multi-touch gameplay and settings persistence
  still need witnessed device checks.
- **Built — storage selector:** extracted the existing desktop headless
  `XamShowDeviceSelectorUI` and shared dispatch helper into common
  `src/kernel/xam/xam_storage_ui.cpp`. Embedded iOS now selects the existing
  virtual HDD and preserves asynchronous completion/UI notification semantics
  instead of hitting the generated abort guard. Manual/autosave requests still
  go through ContentManager and the normal sandbox save path; no fabricated
  save success or save-file migration. **Device-validated write:** on the
  one-slot-default build, GTA IV requested the selector, received the virtual
  HDD, created and mounted `SGTA412` as `save0:` under Theft4's sandbox save
  root, then closed and unmounted it cleanly. The user confirmed the in-game
  save succeeded. Relaunch/load persistence remains the final end-to-end check.
- **Unresolved GPU/lifecycle defects:** a subsequent ordinary two-slot launch
  reproduced the driving visual freeze: native submission 1838 did not complete
  after 1837, producer queue reached 12,687, and recovery latched rendering off.
  Audio continued cleanly. This happened before installation of the touch UI
  changes. The frozen process was stopped after diagnosis. A separate user
  report describes a freeze after app switching; inspection confirms
  `theft4_core_pause` only changes shell state, not the live game/GPU runtime.
  Touch-state cleanup is implemented, but actual renderer suspend/drain/resume
  is NOT fixed by this UI pass. Retest gameplay with the new one-slot default,
  then separately validate lifecycle transitions after wiring runtime gating.

- Initially added a launch-only `THEFT4_NATIVE_FRAMES_IN_FLIGHT=1|2` control for the
  resource-lifetime A/B. At that checkpoint the default remained two slots;
  `1` serializes native frame resources to test whether the reproducible Metal
  Invalid Resource failure depends on cross-frame reuse. Invalid values fail
  startup explicitly, and the selected override is recorded in the runtime log.
- **Promising bounded result:** the signed Release one-slot run remained GPU-clean
  for approximately three minutes, passed the earlier roughly 100-second failure
  window, reached gameplay and the apartment's second cutscene, and advanced to
  presenter count 5,400. No GPU wait, device loss, publish failure, or producer
  stall was recorded. Audio reached block 33,792 with zero underruns, rebuffers,
  drops, clipping, or non-finite samples. The user reported only small frame drops.
- That run ended for a separate, deterministic reason: GTA IV requested
  `XamShowDeviceSelectorUI` after entering the apartment, and the current headless
  kernel intentionally aborts every export from `xam_ui.cpp`. The device crash
  report confirms `SIGABRT`/`abort() called`; this was not a renderer failure and
  Codex did not terminate the app. Implementing the existing dummy storage-device
  selection behavior in the embedded kernel is the next platform-service blocker.
- The one-slot survival strongly implicates cross-frame native resource reuse,
  but it is not yet proof of a complete renderer fix: the unrelated selector
  abort prevented a longer run. At that checkpoint normal icon launches used
  native rendering with two slots; the one-slot mode has since become the iOS
  default as documented above.

- Follow-up failure recording captured Metal **Invalid Resource (code 9)** at
  14:51:11 in both the swapchain acquisition command buffer and native frame
  1696/submission 1714, followed by GPU timeouts and the device-loss latch. User
  reports this visual failure coincided with starting iPad screen recording.
  Recording-specific causation remains unproven pending a no-recording control.
  MoltenVK maps this error through its generic out-of-device-memory result;
  that wrapper does not establish actual memory exhaustion. The bounded trace
  and runtime log were saved locally, and the frozen app was terminated.
- A subsequent ordinary-play control with no screen recording reproduced the
  visual freeze, so recording is not the root cause. The run remained healthy
  through native frame 2989. At 14:57:42 GPU submission 2994 stopped completing
  (tracker completed 2993); the producer queue reached 13,592 commands, and
  recovery received `VK_ERROR_DEVICE_LOST` five seconds later. The native
  renderer latched off while the process and audio remained alive. This makes
  native GPU resource/submission correctness the highest-priority blocker.
- Audio was clean throughout that control: 19,456 reported blocks, an 8,192-frame
  queue, and zero underrun frames, rebuffers, dropped blocks, clipped samples,
  or non-finite samples. The app was terminated only after the freeze and its
  complete runtime log was preserved locally.

- The render worker now dequeues up to 64 title commands per queue-lock
  acquisition, keeping staged texture generations protected until consumed.
  Initial frame storage reserves 8,192 commands. Producer backpressure logs
  bounded stall breadcrumbs after 500 ms and periodically thereafter. These
  changes built successfully in signed iPhoneOS Release; their individual
  performance contribution has not been isolated in an A/B test.
- iOS now selects the GTA-IV-specific native renderer on normal launches when
  compiled in. `THEFT4_GRAPHICS_BACKEND=generic` retains the generic control.
  Both paths remain AOT CPU execution and Vulkan → MoltenVK → Metal graphics.
  The faster native default is **experimental, not stability-qualified**.
- The 14:40:59 native session reached the playable car scene. The user reported
  better visuals and approximately 25–30 FPS while turning the camera, then a
  frozen image with continuing audio. This is human-observed performance, not
  a sustained instrumented 30-FPS acceptance result.
- At 14:42:16 the render worker failed to complete GPU submission 2274
  (completed 2273). Recovery subsequently received `VK_ERROR_DEVICE_LOST`
  (`-4`) from device-idle and permanently disabled native rendering at frame
  2270. The process remained alive. Later renderer-initialization messages are
  consequences of that failure latch, not evidence that initial setup failed.
- This supersedes a CPU-only transport/livelock explanation for this run.
  The original Metal fault is not in the ordinary runtime log; a GPU resource
  lifetime, invalid command, shader fault, or timeout is not yet distinguished.
  Surface-address-alias diagnostics alone do not prove the cause.
- Saved the failure log locally and terminated the frozen process. Next test
  uses the existing opt-in, bounded GPU flight recorder and Vulkan error
  callback without changing rendering quality, frame overlap, or shader code.
  Do not mask device loss by treating failed fences as completed.

### Validated — full-intro visual test exposed a deterministic heavy-scene stall

- Ran the ordinary ARM64 iPhoneOS Release build on an M5 iPad with the native
  GTA-IV-specific backend, retail-equivalent resource sizes, 1280×720 output,
  16:9 presentation, SMAA, vsync, and the 30 FPS cap. No diagnostic renderer
  switch or GPU timing instrumentation was enabled for the launch.
- Human observation confirmed materially improved texture quality and smooth
  motion for approximately the first 20 seconds of the 3D intro. The first
  expensive wide ship view triggered a pause; execution advanced to a later
  ship-interior frame and then stopped publishing images.
- Before the stall, the on-screen counter held near 20 FPS while motion appeared
  notably smooth and evenly paced. This counter advances only for a new guest
  mailbox version and is the best available measure of newly rendered game
  frames in this build.
- The present milestone log advanced at approximately 30 calls per second:
  presents 900→1024 took 4.139 seconds, 1024→1200 took 5.864 seconds, and
  1200→1500 took 10.004 seconds. Source review after the visual test confirmed
  that this log counts every successful presenter invocation, including repeated
  presentation of the same guest mailbox version. It is swapchain handoff cadence,
  **not** distinct game FPS. Earlier 30-FPS interpretations of this telemetry are
  superseded by the approximately 20-FPS on-screen measurement.
- Audio reached block 9,216 with zero underrun frames, rebuffers, dropped blocks,
  clipped samples, or non-finite samples.
- Two device captures taken around a 10-second profile showed the same intact
  game frame and a 0.0 FPS counter. The process remained alive; this was a hang
  in forward frame production, not a crash, black frame, or corrupt output.
- A 10-second Time Profiler attachment recorded 4.359 CPU-seconds on the busy
  title command-submission thread and 3.986 CPU-seconds on the native render
  worker, including 3.950 CPU-seconds under `RenderWorkerMain`. Recurrent hot
  paths included pthread mutex slow paths, `ValidateAndCopyCommand`,
  `NativeCommand` move/destruction, shared-pointer array movement, XXH3 fixed-
  function hashing, `CaptureBufferResource`, `CaptureTextureResource`, and
  pipeline/target lookup.
- **Leading diagnosis, not yet a final root cause:** the heavy scene saturates or
  livelocks the CPU-side producer/consumer command path through lock contention
  and per-command capture/copy/hash work. The profile does not look like a sleeping
  deadlock or a process/GPU crash. A short correlated Metal trace is still needed
  to prove whether the GPU becomes idle while the CPU path fails to publish.
- **Next scoped experiment:** add bounded queue-depth, producer-wait, consumer-
  wait, commands-per-frame, copied-byte, resource-capture, and pipeline-lookup
  counters; reproduce the same transition; then remove the dominant verified
  ownership/copy or lock cost without changing draw contents, shaders, textures,
  effects, resolution, or synchronization semantics.
- No renderer code, game data, save data, or device configuration changed during
  this test. The app was terminated after evidence collection and process absence
  was verified.

### Validated — generic-renderer gameplay control and live CPU diagnosis

- A subsequent manual app launch selected the production Liberty Vulkan command
  processor (generic Xenos/PM4 translation through Vulkan → MoltenVK → Metal),
  not the GTA-IV-specific native backend. The runtime log explicitly records
  `Theft4 selected the production Liberty Vulkan command processor`.
- Human validation reached stable controller-driven open-world gameplay. The
  on-screen distinct-frame counter generally reported 8–12 FPS, with occasional
  zero-FPS pauses followed by recovery. This establishes a slower but substantially
  more robust gameplay control path for comparison with the faster native backend.
- Representative gameplay frames contained roughly 2,000–7,700 draw attempts,
  commonly more than 5,000 submitted raster draws, and 41–45 recurring resolve/
  copy/dump operations. New pipeline combinations continued to appear during
  traversal; affected frames temporarily reported placeholder skips and recovered
  after asynchronous pipeline creation.
- A 12-second on-device Time Profiler recording measured 9.851 CPU-seconds on a
  major AOT guest thread and 8.426 CPU-seconds on `GPU Commands`. The five largest
  MoltenVK/Metal driver worker entries shown by the summary consumed another
  10.904 CPU-seconds combined. This is a heavily CPU-parallel workload, not proof
  of an exclusively GPU-limited frame.
- Generic GPU-command hot paths included `_platform_memmove`, base and Vulkan
  register writes, `VulkanCommandProcessor::UpdateBindings`, Type-0 packet
  execution, and `RenderTargetCache::Update`. Driver workers repeatedly created
  Metal render/compute contexts and encoded MoltenVK command buffers.
- Audio degradation correlated with the worst gameplay intervals: the ring fell
  to 256 queued frames and recovery added 256 underrun frames plus one rebuffer
  repeatedly, sometimes several times per second. The cumulative session reached
  83,712 underrun frames and 327 rebuffers while retaining zero dropped blocks,
  clipped samples, or non-finite samples. This supports CPU starvation as a major
  contributor to audible chopping rather than a malformed audio stream.
- A broader Apple Game Performance capture failed to finalize and a follow-up
  Metal System Trace could not reattach to the still-running process. No GPU-time
  or limiter conclusion is claimed from those failed captures.
- No code or settings changed during this diagnostic. The app was deliberately
  left running because the user was actively playing it; normal bounded-test
  termination remains the rule when the user is not using the app.

## Public checkpoint `440d505c` — 2026-09-16

Comparison base: [`16e76b9e`](https://github.com/KoreanSeats1/Theft4/commit/16e76b9ea230920317436159258c745df706361b),
the last commit on `origin/main` when this entry was prepared.

### Current outcome

- **Validated build:** unsigned and development-signed ARM64 iPhoneOS Release
  builds compile and link with the GTA-IV-specific renderer embedded.
- **Validated runtime initialization:** an M5 iPad accepted the required Vulkan
  feature set through MoltenVK, selected the GTA-IV-specific renderer, created a
  three-image 1280×720 swapchain, loaded 1,356 cached shaders containing
  11,599,741 bytes of SPIR-V, applied TU8, and entered title-specific 3D/deferred
  rendering without a startup crash.
- **Measured presentation cadence:** in the latest bounded native-renderer run,
  presents 900→1024 took 4.135 seconds (29.99 FPS) and presents 1024→1200 took
  5.869 seconds (29.99 FPS). These windows include the transition into active 3D.
- **Measured audio health:** the same run reached audio block 8,192 with zero
  underrun frames, zero rebuffers, zero dropped blocks, zero clipped samples, and
  zero non-finite samples.
- **Pending visual verification:** the latest retail-fidelity native build has
  not yet been watched through the entire 2D intro, first 3D cutscene, and first
  player-control state. Thirty-FPS telemetry is therefore not yet a claim of
  visually correct, sustained gameplay.
- **Device hygiene:** every device probe is bounded as launch → collect evidence
  → terminate → verify process absence. Theft4 is not left running between tests.

### Added — GTA-IV-specific iOS renderer

- Added separate compile and runtime gates:
  - `THEFT4_COMPILE_GTA4_NATIVE_BACKEND` compile-checks the renderer without
    selecting it.
  - `THEFT4_ENABLE_GTA4_NATIVE_BACKEND` embeds the complete renderer.
  - `THEFT4_GRAPHICS_BACKEND=native` selects it for a launch.
- Integrated the mature title-specific Vulkan renderer from
  `glue/rexglue-sdk-main/src/graphics/gta4_native/` instead of replacing it with
  a speculative renderer. This path consumes GTA IV's known graphics hooks and
  checked-in native shader cache while retaining Vulkan → MoltenVK → Metal.
- Added `theft4_gta4_native_compile`, an isolated static archive containing the
  native renderer, GTA IV hook layer, shader cache, SMOL-V decoder, post-effects,
  SMAA, and supporting native-renderer subsystems.
- Linked zstd for compressed shader-cache data and force-loaded the native archive
  so weak hook wrappers that interpose generated AOT entry points are retained by
  Apple's dead-strip linker.
- Added `ios/bridge/theft4_gta4_native_graphics.cpp` and `.h`:
  - wraps the UIKit-owned `CAMetalLayer` in a ReXGlue `Surface`;
  - creates the existing Vulkan provider and presenter on the Apple GPU;
  - attaches the surface on the main thread;
  - supplies the renderer with externally owned provider/presenter/surface objects;
  - keeps the generic Xenos/Vulkan backend as a fallback if native setup fails.
- Extended `Gta4NativeGraphicsSystem` with an external-presentation constructor
  so the UIKit application continues to own lifecycle and presentation instead
  of importing the desktop frontend.
- Added an iOS-safe Apple memory-diagnostics path. Public iOS SDKs do not expose
  `mach_vm_region_recurse`; task VM and malloc-zone counters remain available,
  while only the optional per-region walk is omitted on iOS.
- Added a valid empty modern-shader override table. The stock cached SPIR-V remains
  fully available; optional hash-specific overrides stay disabled until a
  compatible host DXC toolchain is deliberately provisioned.

### Changed — native renderer defaults for iPad retail fidelity

The renderer's original desktop-enhancement defaults were substantially more
expensive than the Xbox 360 workload. The iOS shell now publishes launch-time
settings that preserve the retail workload while presenting at 1280×720:

| Setting | Previous desktop-oriented value | iOS native value |
| --- | ---: | ---: |
| Mirror and water reflections | 1920×1080 | original 320×180 |
| Environment reflection map | 1024×1024 | original 256×256 |
| Base shadow-map size | 512 | 256 |
| Shadow-distance multiplier | 2.0× | 1.0× |
| Forced highest model LOD | enabled | disabled |
| World draw-distance multiplier | 3.0× | 1.0× |
| Drawable-reference limit | 20,000 | 13,000 |
| Output | — | 1280×720, 16:9, vsync, 30 FPS limit |
| Post-process antialiasing | — | SMAA high |

This removes optional desktop enhancements; it does not lower the renderer below
the original game's resource sizes. Scene MSAA remains title-controlled, and the
latest log reported one active deferred/forward sample at the measured transition.

- Disabled optional high-resolution vector-font atlases in the iOS native shell
  because those assets are not bundled. The game uses its stock font path rather
  than issuing failed asset probes or losing text.
- Moved high-volume native virtual-resource, resolve, vertex-declaration, and
  alpha-pipeline messages behind the existing `kNativeTrace` diagnostic category.
  The clean run reduced recurring log work dramatically without removing the
  diagnostics from explicit trace builds.
- Avoided hot-path unknown-register metadata lookup unless GPU debug logging is
  actually enabled.

### Added — on-screen frame-rate measurement

- Added a top-right UIKit FPS indicator using monospaced digits and a 500 ms
  sampling window.
- The counter advances only when a distinct guest mailbox image is successfully
  handed to the Vulkan swapchain (`VK_SUCCESS` or `VK_SUBOPTIMAL_KHR`). Repainting
  the same mailbox image is not counted as a new game frame.
- The counter uses a relaxed atomic and performs no GPU readback or per-frame file
  I/O. The same publication point works for both generic and native renderers.

### Fixed — persistent shader and pipeline cache preload

- Corrected append/update stream positioning so both cache readers seek to the
  beginning before validation and resume writes at EOF afterward.
- Added header, version, record-hash, unsupported-requirement, corrupt-tail, and
  truncated-shader accounting.
- Conservatively truncates only invalid tails, preserves valid records, and emits
  one bounded preload summary containing bytes, accepted records, requested
  pipelines, created pipelines, and elapsed load time.
- A representative warm device run created all 332 requested cached pipelines.
  This is persistent Vulkan/Xenos pipeline preparation, not CPU JIT and not a
  promise that every later gameplay shader is already cached.

### Added — controlled generic-renderer experiments

All controls below validate their values and are dormant unless explicitly set.
The normal Release path does not pay for timing clocks or detailed metrics.

| Control | Purpose | Default/status |
| --- | --- | --- |
| `THEFT4_READBACK_RESOLVE=none|some|fast|full` | Compare guest-visible resolve coherence policies | `fast`; validated working baseline |
| `THEFT4_SUBMIT_PRIMARY_END=0|1` | Compare PM4-primary-boundary submission policy | upstream value; one-submission candidate rejected |
| `THEFT4_DRAW_BOUNDS=0|1` | Run the existing CPU vertex interpreter for eligible unclipped draws | opt-in experiment |
| `THEFT4_DRAW_BOUNDS_METRICS=0|1` | Log bounded estimator eligibility/reduction counters | off |
| `THEFT4_TRANSFER_METRICS=0|1` | Count ownership-transfer passes, rectangles, pixels, formats, and fallbacks | off |
| `THEFT4_TRANSFER_IN_DRAW_PASS=0|1` | Merge compatible ownership transfers into the next guest render pass | opt-in experiment |
| `THEFT4_TIGHT_RENDER_AREA=0|1` | Patch deferred dynamic passes to the union of conservative draw areas | opt-in experiment |
| `THEFT4_GPU_TIMING=0|1` | Aggregate CPU frame preparation, deferred encoding, submit, fence wait, and in-flight depth | off |

#### Draw-extent safety and telemetry

- Extended the existing draw estimator with low-rate counts for eligible,
  interpreted, reduced, accepted-fetch, and unsafe-input-rejected draws.
- Added `SharedMemory::IsRangeGpuWritten` so the CPU interpreter refuses index or
  vertex ranges for which the GPU owns newer data. It never forces a synchronous
  download merely to enable the optimization.
- Retains the original conservative full-extent fallback for unsupported shaders,
  invalid ranges, GPU-newer inputs, and unhandled draw types.

#### Transfer-in-draw-pass experiment

- Backported the useful structure of XeniOS's compatible ownership-transfer
  merging into ReXGlue's Vulkan renderer.
- Added transfer planning, format/sample/depth compatibility checks, pipeline
  preflight, source transitions before pass entry, encoding before the guest draw,
  and restoration of guest pipeline/descriptors/dynamic state.
- Preserves the standalone transfer fallback for aliasing, unsupported layouts,
  failed preflight, early returns, resolves, and submission boundaries.
- Added cumulative metrics separating standalone and merged passes, objects,
  rectangles, pixels, queue fallbacks, and source/destination format properties.
- The mechanism remains opt-in. It is not part of the current 30-FPS native run.

#### Conservative dynamic render-area experiment

- `DeferredCommandBuffer::CmdVkBeginRendering` now returns a stable command-stream
  index; the renderer can patch the copied `VkRenderingInfo::renderArea` safely
  after all draws in the pass are known.
- Added conservative rectangle union, clamping, pass-finalization coverage, and
  full-area fallback when coverage is unknown.
- Added aggregate full-versus-tight pass and pixel telemetry.
- The mechanism remains opt-in. It is not the source of the native-renderer
  30-FPS measurement.

#### Direct host resolve experiment

- Added a build-gated path that can consume public, generated XeniOS SPIR-V for
  selected color and depth resolve variants and write directly from host render
  targets to shared guest memory.
- Added the required compute descriptor layout, pipeline families, format/MSAA
  selection, barriers, dispatch geometry, dirty-range publication, and existing
  EDRAM fallback.
- **Rejected as a default:** the measured path produced correct output and fewer
  EDRAM dumps but no frame-rate improvement. Both the CMake gate
  `THEFT4_ENABLE_XENIOS_DIRECT_RESOLVE` and launch gate `THEFT4_DIRECT_RESOLVE`
  must be enabled for further experiments.

### Added — profiling and replay tools

- Added `tools/summarize_ios_game_trace.py` to join Apple Game Performance GPU
  intervals, Metal application submissions, command-buffer frame assignments,
  and encoder labels. It reports elapsed spans separately from active interval
  unions so overlapping channels are not incorrectly summed.
- Extended `tools/summarize_ios_time_profile.py` with optional binary validation
  bypass for already-validated exports and caller aggregation for a selected hot
  leaf function.
- Added `tools/ios-metal-replay/`, a separate UIKit experiment that replays one
  lawful private XTR frame through XeniOS's Metal or Vulkan graphics backend with
  a null CPU backend. It contains no game executable and uses no CPU JIT.
- The replay forces synchronous shader compilation because one-shot playback
  cannot recover draws skipped while asynchronous pipelines are pending.
- Replay completion separates command-processor completion from GPU/readback
  synchronization and writes a developer-only PPM for parity inspection.
- Cold replay duration is explicitly not reported as FPS. Trace files, images,
  and logs remain private and ignored.

### Experiment results retained in the record

- **Pipeline preload — kept:** 332/332 requested pipelines created in a
  representative warm run.
- **One submission per guest frame — rejected:** approximately 2–4% slower in
  the tested workload.
- **Direct host resolve — rejected as default:** correct and reduced dump work,
  but no measured frame-rate gain.
- **Fragment-shader-interlock render-target path — rejected for normal play:**
  it rendered broad bands, overlays, and black regions in 3D despite real draws.
- **Apple Game Performance diagnosis:** the short profiled sample showed a median
  37.33 ms active GPU union for selected interior frame groups, with dynamic
  render work dominating at 32.11 ms median. Compute, copy, and render intervals
  overlap and must not be added. The capture also reported an induced Medium GPU
  condition, so it is diagnostic evidence rather than normal-play throughput.
- **Native Metal one-frame replay — research-only:** synchronous compilation and
  EDRAM restoration must be correct before it can serve as a backend performance
  oracle. It does not replace Theft4's current renderer.
- **GTA-IV-specific Vulkan renderer — current leading path:** after restoring
  retail-equivalent resource sizes and gating trace chatter, bounded presentation
  telemetry held approximately 30 FPS across the observed 3D transition with
  healthy audio. Human visual confirmation through player control is next.

### Build-system additions

- Added optional Release ThinLTO for Theft4-owned AOT, bridge, and application
  targets via `THEFT4_ENABLE_THIN_LTO`. It remains off in the measured build.
- Kept `-mtune=apple-m5` as a scheduling choice only; it does not select an
  M5-exclusive ARM ISA and does not replace broader-device testing.
- The measured local configuration used:
  - ARM64 iPhoneOS Release;
  - `THEFT4_BUILD_GAME_CODE=ON`;
  - `THEFT4_ENABLE_GAME_STARTUP=ON`;
  - native backend compiled and embedded;
  - ThinLTO and direct-host-resolve experiment disabled;
  - normal code signing for the test device.

### File map for this checkpoint

| Area | Files | Responsibility |
| --- | --- | --- |
| Public record | `CHANGELOG.md`, `README.md`, `THEFT4_3D_PERFORMANCE_PLAN.md`, `THEFT4_GPU_DIAGNOSTIC_2026-09-16.md` | Current status, evidence, experiment history, next acceptance gate |
| iOS build | `ios/CMakeLists.txt` | Native renderer archive, zstd/SMOL-V/shader-cache linkage, direct-resolve and ThinLTO gates |
| iOS app | `ios/Theft4/main.m` | Top-right distinct-frame FPS overlay |
| iOS renderer selection | `ios/bridge/theft4_bootstrap_graphics.cpp` | Native selection and generic fallback |
| Native iOS bridge | `ios/bridge/theft4_gta4_native_graphics.{h,cpp}`, `ios/bridge/theft4_empty_shader_overrides.cpp` | CAMetalLayer surface, provider/presenter creation, retail settings, shader override stub |
| Presentation counter | `ios/bridge/theft4_metal_presenter.{h,mm}`, `src/ui/vulkan/vulkan_presenter.cpp` | Atomic distinct-mailbox publication count |
| Launch controls | `ios/bridge/theft4_startup.cpp` | Validated opt-in experiment environment variables |
| Native renderer portability | `src/graphics/gta4_native/graphics_system.{h,cpp}` | External presentation ownership, iOS memory diagnostics, log gating |
| GTA IV hooks | `gta4-recomp/src/gta4_native_hooks.cpp` | Native vertex-declaration trace gating |
| Generic GPU diagnostics | `include/rex/graphics/flags.h`, `include/rex/logging/macros.h`, `src/graphics/{command_processor,graphics_system}.cpp` | Flags, conditional logging, hot unknown-register guard |
| Draw-bound safety | `include/rex/graphics/{shared_memory.h,util/draw_extent_estimator.h,pipeline/render_target/cache.h}`, `src/graphics/{shared_memory.cpp,util/draw_extent_estimator.cpp}` | GPU-newer range rejection and aggregate estimator telemetry |
| Vulkan experiments | `include/rex/graphics/vulkan/{command_processor,deferred_command_buffer,render_target_cache}.h`, `src/graphics/vulkan/{command_processor,deferred_command_buffer,render_target_cache}.cpp` | Timing, transfer merge, tight areas, direct resolves, telemetry |
| Cache preload | `src/graphics/vulkan/pipeline_cache.cpp` | Correct stream positioning, validation, tail repair, preload summary |
| Analysis tools | `tools/summarize_ios_time_profile.py`, `tools/summarize_ios_game_trace.py`, `tools/ios-metal-replay/` | CPU caller analysis, Metal/GPU correlation, isolated backend parity replay |

Paths beginning with `src/` or `include/` in this table are under
`glue/rexglue-sdk-main/`.

### Explicitly unchanged or excluded

- The CPU execution model remains static AOT PowerPC → C++ → signed ARM64. No
  runtime CPU JIT or executable-memory generator was added.
- There is no native-Metal game renderer in the production app. The leading path
  remains Vulkan → MoltenVK → Metal, now with GTA-IV-specific rendering knowledge.
- The generic Xenos renderer remains available as the fallback and comparison path.
- No game payload, TU, save, shader capture, GPU trace, screenshot, signing asset,
  or device identifier is included.
- No submodule commit pointer is changed by this checkpoint. Several dependency
  worktrees contain local build/port patches; they are deliberately excluded from
  the top-level commit until each is separately reviewed and made reproducible.
- A signed IPA is not distributed.

### Verification still required before calling the 30-FPS goal complete

1. Watch the ordinary native Release build through the complete 2D intro, first
   3D cutscene, and first player-control state on the physical iPad.
2. Confirm the output is correctly composed at 16:9 with no corruption, tearing,
   missing geometry, broken UI, or black frames.
3. Confirm the on-screen distinct-frame counter remains paced near 30 FPS at the
   heavy wide shots and first playable state, not only the transition window.
4. Confirm dialogue, radio, and mixed 3D audio remain free of recurring skips.
5. Repeat a warm run and a longer thermal/stability run before changing the native
   backend from opt-in to the normal default.

## Public checkpoint `16e76b9e` — 2026-09-15

- Changed generated Xcode schemes to default to optimized Release execution.
- Disabled debugger attachment, Metal capture, and validation for normal play;
  kept Debug as an explicit developer choice.
- Documented signing, lawful prepared-game transfer, app-data preservation,
  normal launch, and diagnostic opt-in.
- Marked older bring-up guides as historical and validated helper routing with
  isolated tests.
- Did not include game payloads, device captures, signing material, or dependency
  revision changes.

## Maintenance policy

For every future material change:

1. Add it under **Unreleased** before or with the code change.
2. State whether it is default, opt-in, rejected, or pending verification.
3. Record the exact verification scope and avoid extrapolating beyond it.
4. Record failed experiments and why they were rejected so they are not repeated.
5. Keep private/copyrighted evidence out of Git and reference only reproducible,
   public techniques and aggregate results.
6. At a public checkpoint, replace the comparison base with the new commit and
   preserve the previous entry below it.
