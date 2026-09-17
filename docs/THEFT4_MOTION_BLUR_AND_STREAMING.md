# Motion blur and fast-driving performance

2026-09-17. Toggle implemented, Release-built and subsequently installed on the
user's cue; **device validation pending**. In-place update succeeded on the M5
iPad; app left closed. No streaming changes or game-data edits.

## What is known

The user reports nearly 30 FPS with two native frames in flight, with roughly
5–10 FPS dips during fast driving. Motion blur and loading/pop-in are plausible
contributors, not established causes. A wide moving view also increases visible
geometry, draw preparation, reflections, resource uploads and memory bandwidth.
Unchanged physics speed is not proof that the CPU has adequate rendering headroom.

An older **one-slot** 1080p CPU profile found significant native texture preparation,
draw-state binding and command-copy costs (see the CPU audit). It does not identify
the bottleneck of the current two-slot build. The latest short two-slot trace used
Boost output, not the 1080p reference. Do not merge these into one baseline.

## Implemented option

**Display → Motion blur**, default on, saved in `NSUserDefaults/Theft4MotionBlur`.
Applies on the next game launch. Fully quit and relaunch to change it; do not
attempt to reinitialize the running guest in place. Settings survive in-place
app updates and do not modify a save, game resource, profile or title update.

The launcher sets `THEFT4_MOTION_BLUR=1|0`; startup validates it and configures
an atomic snapshot. An unset value defaults on. The launcher preference owns
this variable during normal launches, so use the UI for A/B selection.
`THEFT4_MOTION_BLUR_TRACE=1` separately enables a maximum of 32 observations.
The startup log records the selected mode even without that bounded trace.

Source evidence:

- `gta4-recomp/generated/gta4_recomp.13.cpp`, `sub_822CFC00`, block
  `0x822D0BD0–0x822D0C48`: directional-motion-blur conditions select `base+11`
  versus `base+10`. Bases 0/2, 14/16 and 18 retain the other composite choices.
- `sub_822CF300` forwards the selected technique to `sub_828C6568`, then the
  selected pass to `sub_828C64C8`. Keep that helper intact; do not swap only a
  pixel shader after binding, because variant sampler layouts differ.
- New `ios/bridge/theft4_motion_blur.{h,cpp}` interposes that exact call and maps
  only 11→10, 13→12, 25→24, 27→26 and 29→28. Wrong callers, nonzero targets,
  invalid/mismatched effect handles and unknown passes are not modified.
- Default-on goes directly to the original helper (unless observation enabled).
  Off removes directional-blur sampling in the final composite. It does not
  promise removal of upstream vector/preparation work or every scripted blur
  effect. Depth of field, bloom, tone mapping and grain selection remain intact.
- Existing desktop `gta4_presentation_hooks.cpp` uses the same selection boundary
  for TLAD grain, but is **not in the iOS target**. The new wrapper is iOS-only.
- `ios/bridge/theft4_empty_shader_overrides.cpp` publishes zero overrides. The
  desktop modern motion-blur HLSL and its time-scale constant are therefore not
  the place to implement the current iOS toggle. No shader recompilation needed.

## Ordered next experiments

1. **Functional A/B.** Install after the user's cue. Use the same save, vehicle,
   route, approximate time/weather, output mode and 4× filtering. Compare blur on
   versus off during camera pans and fast driving. Verify HUD, depth of field,
   cutscenes, color, water and fades; test both modes after a full process restart.
   A short bounded trace can verify requested 11/13 becoming 10/12; if only other
   passes appear, capture a scene that actually requests blur. Do not label a
   no-op comparison a successful blur performance test.
2. **Warm-route control.** Drive the same route twice per mode without restarting
   between laps. Repeat with reversed on/off order and comparable thermal state.
   Improvement on lap two points toward cache/streaming/first-use work, not proof
   of any specific layer. Stationary camera rotation helps separate motion from
   entering new world cells. Keep Boost versus 1080p as a separate A/B variable.
3. **Bounded attribution.** Reuse native phase profiling after the game reaches
   the route, not during splash screens. Existing `rex_gta4_native_profile_start`
   arms a bounded capture when the diagnostic profile is configured; only one
   exported capture per process is supported. Compare GPU composite time and
   GPU frame envelope against CPU texture preparation, command production,
   pipeline waits, descriptor work, upload bytes and slot/cleanup waits. Exclude
   missing/invalid timestamps. Do not sum nested CPU ranges or count repeated
   swapchain presentations as new guest frames. Benchmark again without the
   profiler/debugger after identifying the cost.
4. **Target the measured layer**, changing one factor at a time:

| Evidence | Next candidate | Safety condition |
|---|---|---|
| GPU composite dominates; off consistently helps | Keep user toggle; inspect stock composite and redundant texture samples for quality-preserving optimization | No wholesale removal of post-processing; blur-off is an optional visual tradeoff, not a free identical-quality optimization |
| New assets coincide with CPU untile/copy and GPU upload spikes | Reuse immutable decoded/staging data and deduplicate preparation; then evaluate bounded ahead-of-need warming | Correct content-generation invalidation, memory budget and two-slot completion ownership |
| Disk/decompression or guest request completion dominates | Instrument request-to-read-to-fixup-to-publish latency, then improve batching/cache or targeted prefetch | Preserve guest dependency ordering, collision/physics publication and save semantics |
| Pipeline creation/wait dominates | Extend existing cache/prewarm using observed pipeline keys | No unbounded startup compilation or guessed pipelines |
| Cleanup coincides with spikes | Measure total destruction time, not just fence wait; tune batching/reuse | Keep the all-native-slots-complete destruction boundary that addressed visual freezes |
| Geometry/draw preparation dominates in already-resident views | Resume canonical fingerprint / redundant state work from the CPU plan | No reduced distance, geometry or texture quality |

Use frame-time distributions, long-frame count and consecutive missed 33.3 ms
deadlines, not only average FPS. Target 30 unique, evenly presented game frames
per second with some headroom; verify the result on prolonged city driving.
Preserve the previous known-good build and stop the app when capture is finished.

## Streaming investigation boundaries

The existing [asset-streaming reconstruction](GTA_IV_ASSET_STREAMING_SYSTEM.md)
separates guest demand/dependencies/admission, queued archive reads, resource
fixup/publication and host renderer uploads. Revalidate its specific assumptions
against the iOS build rather than treating old desktop traces as current evidence.

In active `graphics_system.cpp`, the texture-resource capture path handles conversion,
`PrepareFrameTextures` does host preparation, and image creation/upload uses
`vkCreateImageView`, an upload arena and `vkCmdCopyBufferToImage`. Renderer
texture residency is not the same as whether the guest has loaded the asset.

`gta4_streaming_diagnostics.cpp` observes effective `stream.ini` limits after
`sub_821CFD10` and disk-cache worker setup after `sub_8284DAD8`; it is currently
not linked by `ios/CMakeLists.txt`. These are possible bounded probes, **not
evidence that those metrics were captured on this iPad**. Measure before raising
budgets: guest allocators/addressing and host resources have separate limits.
Do not preload the whole map, bypass dependencies, arbitrarily enlarge load-slot
arrays, change guest pool limits, or assume SSD speed eliminates conversion costs.

Apple's [graphics performance guidance](https://developer.apple.com/documentation/metal/improving-your-games-graphics-performance-and-settings)
supports separating CPU and GPU hitches with frame timelines. Its
[resource loading presentation](https://developer.apple.com/videos/play/wwdc2022/10104/)
is useful for asynchronous upload and residency concepts, but Metal fast resource
loading is **not a drop-in switch** for this Vulkan/MoltenVK renderer or GTA IV's
guest archive/resource formats. Prefer improving the existing abstractions first.

## Verification and build

Host test:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun clang++ \
  -std=c++20 -Wall -Wextra -Werror -Iios/bridge \
  ios/tests/motion_blur_policy_test.cpp -o /private/tmp/theft4-motion-blur-policy-test
/private/tmp/theft4-motion-blur-policy-test
```

Passed: explicit pass pairs, 128-pass cross-product over enabled/valid/caller/target
guards, idempotence, unknown maximum pass, strict override parsing and default-on.
Simulator-only launcher preview built and visually inspected. Device Release build
passed after CMake regeneration; `nm -m` confirms a strong `_sub_822CF300` hook and
the original weak `___imp__sub_822CF300` are present. These tests establish wiring,
not an on-device visual result or FPS improvement.

Artifact: `out/build/ios-device-release/theft4/Release/Theft4.app`.
Executable SHA-256:
`ff1dc78846d5d741a142a1d34aaebc800321536a599a6c44911fd81c60a0d840`.
Private build log: `/private/tmp/theft4-motion-blur-build.log`.
No game files, shader binaries, saves or device captures added to source control.
