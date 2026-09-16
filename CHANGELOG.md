# Theft4 engineering changelog

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

## Unreleased — work after `16e76b9e` — 2026-09-16

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
