# Theft4 0.3.1 — performance improvements

**Version 0.3.1 · build 142 · iPhone and iPad · 2026-10-09**

[Download the unsigned ARM64 IPA and checksum](https://github.com/KoreanSeats1/Theft4/releases/tag/v0.3.1).
Update the existing app in place to retain game files, saves, settings and
prepared ASTC textures. This GitHub release is separate from TestFlight.

0.3.1 follows the direct Metal rebuild in 0.3 with reductions in CPU draw work,
cold geometry preparation, allocation and cache-pressure overhead. It also
includes the optional Mods menu and the viewport/interface fixes developed
during that performance work. The original app icon is retained.

## Performance and frame pacing

- **Cheaper first-use geometry conversion.** Exact admitted conversion recipes
  execute in cache-friendly, record-aligned tiles when safe. Unaligned,
  overlapping and unusual declarations retain their required order and fallback
  behavior. Converted vertex bytes are checked against the original converter.
- **Fewer geometry allocations.** Small immutable geometry appends to aligned,
  unused upload-page regions across frames. Eligible larger meshes reuse VM
  backing only after Metal releases its final reference. Pending GPU views are
  never overwritten; real allocation sizes remain charged to bounded budgets.
- **Bounded source retirement.** Incremental cleanup replaces repeated broad
  source scans. Live meshes and pending GPU resources keep their ownership.
- **Less work per draw.** Shader constant-layout metadata and exact resolved
  shader interfaces are reused. Reflection realizes only required bindings,
  and texture/sampler binding storage is pooled. Required range and resource
  validation remains in place.
- **Less frame bookkeeping allocation.** Frame admission and attachment
  dependency analysis reuse bounded storage, with ordinary safe allocation
  for overflow. This reduces setup work without dropping commands.
- **Less redundant driver work.** Cached immutable device traits avoid repeated
  Objective-C capability queries. Clear-prefix fusion and proven full-overwrite
  cases reduce redundant attachment loads/stores while preserving draw order,
  contents and independent offscreen passes.
- **Warm pipeline recipes survive cache pressure.** The 4,096-entry CPU pipeline
  template cache evicts cold entries individually instead of clearing every
  entry at once. Prepared draws retain their own immutable dependencies.
- **Shorter allocation critical sections.** Upload-page VM allocation and
  deallocation run outside the pool mutex; accounting and reuse remain locked.
- **Less CPU burned while waiting.** Legacy Darwin multi-object waits retain
  lock-vector capacity and sleep the final sub-millisecond remainder instead of
  repeatedly polling it. Contested finite waits honor their timeout. Signal
  order, wait-all consumption and manual-reset semantics are retained.

These changes target avoidable work in the port rather than lowering default
texture precision or hiding the city. Existing graphics presets still have
their documented quality and distance choices. Performance varies with device,
scene, selected resolution, effects and the amount of visible geometry.

## Optional mods and interface improvements

All mods are independently selectable in the launcher before Play.

- **Native Aspect Ratio:** expands the 3D viewport to the launch-window aspect
  instead of stretching a 16:9 image. Camera projection, visibility and copied
  lighting/post-effect cameras use consistent reconstruction. Independent
  shadow/reflection views retain their own projection. Odd-size effects resolves,
  pause-panel clipping and curved radar/UI proportions have dedicated fixes.
  More pixels and visible scenery can increase CPU/GPU load. Rotation or window
  resizing after Play proportionally fits the latched launch shape; reopen to
  launch at a new full-window shape.
- **Custom Time Cycle:** the supplied lighting/weather table works with both
  loose and packed game installations. The original game files remain intact;
  a private archive override is reused on later launches. **Credit and thanks
  to SBerrix** for the custom time cycle. The screenshot is an example; appearance
  depends on time and weather.
- **God Mode:** optional player damage/death protection.
- **Unlimited Ammo:** preserves magazine depletion and reloading while retaining
  reserve ammunition.
- **iPhone launcher:** compact navigation, reflowing cards, shorter labels and
  safe-area-aware layouts improve the settings experience on narrow displays.

Realistic Vehicle Handling is still a research/implementation plan. It is not
included as a working feature in this release.

## Older-device texture support and updates

The GPU capability check remains authoritative. GPUs without direct BC texture
support use the one-time static BC-to-ASTC preparation flow with progress, an
estimate, pause/resume and a saved cache. The tested A12Z needs this path;
the tested M5 supports BC textures directly and skips it. This is not an
iPhone-generation cutoff and does not promise equal performance on older chips.

Normal app updates reuse the existing prepared cache. Deleting it, changing the
relevant game data or invalidating its cache identity can require preparation
again. Normal launches suppress development telemetry; optional graphs and Long
Performance Capture remain available independently.

## Validation and remaining limits

The performance candidate underlying this release passed 33 native contract
tests, all 45 real-Metal graphics cases and wait address/undefined sanitizer
checks. Additional mod contracts cover actual camera/UI adapters, resolution
selection, packed time-cycle mounting and gameplay-hook behavior. The release
build uses verified `-O3`/`NDEBUG` compiler arguments and includes 2,736 compiled
game/host Metal libraries.

The latest aligned M5 build-140 city-view capture contained approximately
4,210 commands per frame. Its final five seconds measured 37.80 ms median
publication interval and 18.73 ms median renderer wall time. Renderer timing
alone is not total frame time, simulation cost or display scanout timing. The
subsequent build-141 changes remove proven source-level overhead; host fixture
percentages are not whole-game FPS gains and are not added together.

The target remains **under 30 ms of total frame work** within the 33.3 ms
interval for 30 FPS. Remaining first-use spikes, heavy city views, long-session
behavior and device-specific performance still need captures. A universal
locked 30 FPS or complete visual validation of every expanded-viewport scene is
not claimed. This remains an AOT Xbox 360 recompilation using the compatibility
runtime; physics, traffic and all game logic have not been rewritten from source.

The source-backed history, measurements, rejected experiments and title-specific
assumptions are in [the detailed engineering record](IOS_MODS.md). The broader
renderer architecture is documented in [the 0.3 architecture record](THEFT4_0.3_ARCHITECTURE.md).

## Install the sideloaded IPA

1. Download `Theft4-0.3.1-142-ios-arm64.ipa` from the release assets. GitHub's
   **Source code** ZIP is not the IPA.
2. Install with a compatible sideloading tool such as AltStore or SideStore.
   The public IPA is unsigned; the tool supplies your signature and provisioning.
   Update in place rather than deleting the existing app and its data.
3. For a new installation, launch once to create **Files → On My iPhone/iPad →
   Theft4 → game**, then close the app.
4. Prepare your legally obtained supported Xbox 360 USA retail game: title
   `545407F2`, media `6AC07221`, base `0.0.0.5`, matching TU8 patch to `0.0.8.5`.
   Raw ISO images and unopened update packages are not ready-to-play folders.
5. Copy the contents of the prepared folder into Theft4's existing `game` folder.
   `default.xex` and `default.xexp` belong at its root, not inside `game/game`.
6. Reopen, use **Check Game Files**, complete texture preparation if required
   by the GPU, select optional mods, and Play.

Requires ARM64 iOS/iPadOS 26.0 or later. The IPA contains the application,
compiled shaders and the credited custom mod resources; it contains no retail
game installation, title update, save, texture cache or developer provisioning.
The attached SHA-256 file verifies the download. See the
[full installation instructions](IOS_SIDELOAD_INSTALL.md).
