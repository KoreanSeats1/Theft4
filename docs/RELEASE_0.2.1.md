# Theft4 0.2.1 — important iOS update

Version 0.2.1 (build 53) is an important update for iPhone and iPad. It keeps
the official `com.lukebrosious.theft4` app identity, so an in-place update
preserves the existing app container, game installation, settings, caches, and
saves. Do not delete the old app before updating.

The GitHub `ios-arm64.ipa` is unsigned and must be signed by the user's
sideloading tool. The TestFlight build is separately signed by Apple. Neither
artifact contains copyrighted game files, title updates, user saves, signing
identities, or provisioning profiles.

Public sideload artifact: `Theft4-0.2.1-53-ios-arm64.ipa`
SHA-256: `3f278f0d3b07cfdda0e40a3672594d17bcd0499c01484d86b0a1845b7f571f9a`

## User-facing changes

- New After Hours launcher presentation and updated Theft4 app icon.
- Gameplay uses the proven centered 16:9 presentation on iPhone and iPad. The
  initial build 52 device-aspect experiment was withdrawn after testing exposed
  stretched world geometry, misaligned shadows, incomplete HUD correction, and
  increased frame-time instability. It will return only after the world,
  shadow, post-process and HUD projections are validated together.
- New **Auto Optimize for This Device** and **Original Xbox 360 Settings**
  buttons. Both are explicit, reversible starting points; manual changes still
  persist.
- Graphics choices include Native Pixels, 540p, 720p, 900p, and 1080p scene
  resolution, with FSR controlled independently where supported. Native Pixels
  uses the largest centered 16:9 physical-pixel target that fits the display.
- Expanded controls for shadows, draw distance, model detail, reflections,
  edge smoothing, anisotropic filtering, motion blur, and depth of field.
- New save export/import in **System**. Import validates the selected export and
  creates a dated backup of the current saves before replacement.
- Improved touch-control, frame-counter, frame-time graph, diagnostic status,
  and Files export behavior.
- iOS Game Mode metadata is declared for the official app.

## Limited-memory device protection

iPhones and iPads with less than 7 GiB of usable memory are treated as
limited-memory devices. This covers the 6 GB class and older/lower-memory
models without changing the defaults on devices with 8 GB or more.

On a limited-memory device, settings that would create unnecessary rendering
or memory pressure are constrained at launch and whenever graphics settings are
changed:

- 1080p and Native requests are capped at 900p with FSR and fixed 1080p output;
- existing lower 540p and 720p selections remain lower rather than being raised;
- original shadow size, optimized draw distance, original model detail and
  reflections are used;
- additional edge smoothing, anisotropic filtering, motion blur, depth of
  field, and the optional FSR sharpening boost are disabled;
- launcher choices above the supported ceiling are visibly unavailable.

This is a pressure-control policy, not proof that resolution caused every
reported failure. In particular, tester logs associated guest texture handle
`D8FFC2B0` with an image-creation failure on 6 GB iPhones, but the handle alone
does not identify the allocation root cause. The cap is a safe mitigation while
the new capture evidence is used to separate memory, pipeline, CPU, and GPU
causes. Newer 8 GB-and-up devices keep their existing device-optimized choices.

## Performance and renderer work

- Reduced producer/worker command-transfer copying, allocation, and lock hold
  time with batched transfer and bounded storage reuse.
- Avoided redundant resource-binding notifications and repeated vertex/index
  binding when validated state is unchanged.
- Reused immutable vertex-layout requirements and validated shader-constant
  parent snapshots; changed constant ranges are projected into fenced frame
  allocations instead of rematerializing whole blocks.
- Improved persistent-buffer reuse accounting, resource retirement, descriptor
  handling, pipeline diagnostics, resolve synchronization, and frame-slot
  ownership.
- Preserved native-renderer draw order and resource lifetime rules while adding
  stricter reporting for missing or invalid bindings.
- Added lightweight frame-stage/publication tracing and bounded analysis tools
  for pass chronology and frame pacing.

These changes target work that is redundant or diagnostically opaque. They do
not constitute a claim of locked 30 FPS, lower temperature, or equivalent
performance in every scene. Heavy city views and first-visit streaming still
need matched-route device testing.

## Lower-overhead, higher-value capture

The short detailed capture now collects 120 detailed samples across roughly
360 submitted frames instead of profiling every frame in a 600-frame window.
GPU query boundaries are capped at 128 per sampled frame. Expensive image-memory
and process-memory inventories are collected at capture start and periodically,
not every sampled frame. Query reset work is limited to the active budget.

Each detailed capture also starts an aligned lightweight all-frame stage trace,
then stops only the trace it started when the detailed capture completes.
Background trace draining is throttled to about once per second. The combined
export can correlate:

- GPU pass ranges, shaders, pipeline identities, query overflow, and queue time;
- CPU inclusive/self timing, worker wall/on-core time, command dwell, and guest
  production gaps;
- pipeline compile/wait/miss/create activity;
- uploads, descriptors, persistent-buffer hits/misses, fences, presentation,
  thermal state, effective output settings, and periodic resource inventory.

This design is intended to make under-33.3 ms investigations more concrete
while reducing the profiler's own pressure. Profiling still adds overhead, so
ordinary capture-off play remains the reference for perceived performance.

## Save compatibility and installation

Update the existing official Theft4 app in place. The historical Lab app
(`com.theft4.m5lab`) has a separate container and is not migrated automatically.
Keep it until its data has been copied or backed up. Full sideload and game-data
instructions are in [IOS_SIDELOAD_INSTALL.md](IOS_SIDELOAD_INSTALL.md).

## Verification scope

The release source includes host tests for device profiling and limited-memory
resolution policy, constant projection/generation, publication tracing, native
GPU attribution, native performance sampling, and resolve policy. The release
build is produced with the official bundle ID and the repository's reproducible
dependency patch set. Physical-device installation verifies packaging and
signing; broad gameplay, visual-equivalence, thermal, and long-duration testing
remain ongoing.
