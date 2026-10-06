# Theft4 0.3 — direct Metal and a rebuilt rendering path

Release candidate: Apple version **0.3.0**, build **122**, launcher label **0.3**.
This is preparation for promotion to main, not an announcement that a public IPA
or TestFlight build has been published.

0.3 is the largest rendering change in Theft4 so far. The running game now sends
its prepared frames directly to Metal. This includes the actual city, characters,
lighting, effects and presentation, rather than just the separate Metal Lab.
The renderer and its CPU preparation, resource lifetimes and upload caches have
been substantially rebuilt around Apple's graphics API.

On the M5 iPad, the tester reports a huge improvement and very good gameplay at
the selected native resolution, with a remaining spike that now recovers. The latest retrieved log confirms a
2416×1359 native internal/output target with no upscaling. This
is much closer to the desired console experience. It does not establish a locked
30 FPS in every scene, uninterrupted long-session stability, or equivalent
performance on other hardware. The engineering target remains under 30 ms of
frame work, leaving margin inside the 33.3 ms interval for 30 FPS.

## The direct Metal change

The previous production rendering route used the GTA IV frontend, Vulkan and
MoltenVK before reaching Metal. 0.3 introduces a live Metal worker, native render
passes, native resource bindings, completion-owned frame storage and direct
CAMetalLayer presentation. Normal Metal gameplay does not realize its draws
through the Vulkan provider or presenter.

Game shaders are prepared offline from the project's stock shader corpus. Their
constant banks and reflected texture/sampler fetches become explicit Metal
bindings. The game no longer needs to compile that corpus through a live Vulkan
translation path to produce its Metal draws. Pipeline variants and Metal driver
compilation can still require work when a new scene is encountered.

The game remains an ahead-of-time recompilation of the supported Xbox 360 release.
Its compiled ARM64 code still uses the compatibility runtime for the title's
memory, service and calling conventions. This release does not replace all game
logic with handwritten native code, rewrite physics or traffic, or turn the
project into a PC source port. Legacy Vulkan code and libraries remain in the
build for comparison and compatibility; direct rendering is a runtime route,
not a claim that the application has no Vulkan symbols or dependencies.

## Rendering correctness and clarity

- Complete ordered frames now include the game's render passes, color resolves,
  depth handoffs, reflections and host effects. Draw order and required prior
  attachment contents are preserved across internal submissions.
- The port handles reversed viewport depth, the negative-one-to-one vertex depth
  convention, attachmentless draws, depth-only pipelines, per-instance stream 16,
  buffered/indexed rectangles and rotating output drawables.
- Packed lighting aliases and the title's float-pair multiple render targets
  retain their intended channel masks and storage roles. GPU-produced surfaces
  are distinct from immutable uploaded textures.
- Sampling preserves mip ranges, row/plane pitches, layers, cube faces and channel
  swizzles. Reflection updates wait for valid written content rather than making
  an entire otherwise valid scene fail.
- Blending, alpha discard, sample coverage, output scaling and clamping are checked
  against the game shader contract. Normal presentation uses GPU textures directly.
- Color targets use render-appropriate private storage without unnecessary usage
  flags that defeated compression eligibility in early Metal builds.

These changes restore the intended image through the new API. The tester reports
excellent visual quality and clarity; this is not a promise of new art assets,
rewritten lighting, HDR support, or a quantified image-quality advantage over
the original console.

## Frame-time and memory work

- Draw preparation and encoding stream through the backend without building a
  second full vector of ready draws.
- Completed GPU frames recycle constant upload storage. Transient constants no
  longer compete with persistent geometry in the same upload cache.
- Vertex conversion is identified by the byte transformations actually required,
  so equivalent shader/layout combinations can reuse the same converted geometry.
  Whole-record offsets share conversion work while draw offsets stay exact.
- Converted vertex and index allocations are shared by the CPU conversion cache,
  draw packets and GPU upload. Full-buffer copies into a second CPU packet are
  eliminated, including when the original cache entry retires.
- Large immutable geometry participates in the bounded upload-view memo. Warm
  lookups avoid repeated resource-cache work while version, owner, size and range
  checks remain authoritative.
- Shader constants reuse immutable parent snapshots and projections. Only required
  changed ranges are materialized; constant vectors and draw storage are pooled.
- Registry updates are journaled. Pressure maintenance evicts incrementally rather
  than sorting entire caches every frame. In-flight GPU resources survive CPU
  retirement until their completion receipt releases them.
- Metal bindings skip redundant state changes, and depth/stencil variants reuse
  compatible render pipelines. Host utility pipelines have persistent archives.
- The final 0.3 candidate adds bounded index-range eviction instead of clearing
  all 8,192 entries, plus a fast repeat lookup and cheaper bucket hashing. Hot
  ranges survive saturation without weakening index, vertex or restart checks.
- A selected LOD routine is expressed as native C++ with tests for the original
  comparisons, missing resident meshes and unusual inputs. It preserves the
  title's streaming, fades and transition ownership.

The smaller CPU benchmarks are engineering evidence for individual paths. Their
percentages must not be added together or presented as a whole-game FPS gain.
Removing an API layer alone was not sufficient: early live Metal builds were
slower until their preparation, uploads, storage and lifetime behavior were fixed.

## Graphics and everyday use

- **Sharpening strength** is independently adjustable, with bounded output and
  preserved alpha. It supplements the existing resolution and FSR 1 choices.
- **AA Off** is honored before the headless renderer starts. FXAA and SMAA remain
  choices; edge filtering has a performance cost. FSR 1 upscaling is not temporal
  antialiasing and does not replace every AA function.
- **Native Pixels** retains a centered 16:9 physical-pixel target, with no FSR for
  that selection. It does not render the game across the iPad's full non-16:9 panel.
- **Frame Speed** preserves the selected resolution, uses original shadows,
  earlier resident LOD and shorter world distance, and disables optional blur and
  edge filters. **Optimized distance** is 0.70× world distance with the title's
  existing LOD transitions; it is not a new occlusion or fading algorithm.
- **Skip Intro Videos** defaults On, skipping the opening credits and logos while
  retaining the game's normal loading and initialization.
- **Retail Mode** defaults On for new settings. It disables development logging,
  profiling, captures and debug overlays before runtime startup. Turn it Off and
  reopen to restore diagnostics and saved overlay choices. Required rendering
  validation and synchronization remain enabled.
- Game Mode is declared, and the tested development build retains the sustained
  execution request. These settings do not prove that the OS keeps Game Mode active
  or grants identical scheduling on every device or signing profile.
- Existing save export/import, automatic pre-import backup, controller/touch
  controls and launcher profiles are retained. These are carried-forward features,
  not all newly introduced in 0.3.

## Devices without BC texture support

The compatibility path checks the GPU's BC-texture capability; it does not guess
from a device's age, name or an iPhone-generation cutoff. The tested M5 supports
BC textures directly. The tested A12Z requires prepared ASTC textures.

On a GPU that lacks BC support, a one-time preparation sweep converts supported
static BC1/BC2/BC3 textures to saved ASTC copies before gameplay. Setup explains
the storage/time requirement, shows progress and an estimate, and can pause and
resume. Later launches reuse the prepared cache. System provides cache deletion
with confirmation. Deleting it or changing the relevant source/cache identity can
require preparation again; dynamically created or uncatalogued textures can
still use the supported runtime conversion path.

This saves repeated gameplay conversion and avoids retaining large RGBA copies
as the default static-texture solution. It improves compatibility, not the A12Z's
underlying CPU/GPU capability. The newer-device BC route skips this requirement.
After transferring already-updated game files, **Check Game Files** verifies the
installation before requesting an update package it may not need.

## Verification and remaining work

The previous installed build 121 passed 26 CPU checks, 45 GPU graphics checks and
32 saved game draw replays in both Diagnostic and Retail modes, plus backend-worker
and upload-lifetime tests. Build 122 adds a dedicated index-cache contract test.
The candidate's final build and installation evidence is kept with its artifact;
do not treat a Lab check or saved draw replay as a complete gameplay test.

Initial hitches remain. App-focus/multitasking recovery, dense scenes, long sessions,
thermal behavior and non-M5 devices still require device validation. In particular,
no identical-route controlled test establishes the spike's sole cause or a
universal native-resolution performance guarantee. The build 115 uniform/depth
shader experiment regressed and was reverted; it is not part of this release.

The detailed provenance, implementation map and lessons for future recompilation
projects are in [the 0.3 architecture record](THEFT4_0.3_ARCHITECTURE.md).
Promotion and packaging status are in [the main integration plan](THEFT4_0.3_MAIN_INTEGRATION.md).

## Install the sideloaded IPA

A public 0.3 IPA has not been published by this preparation. When an unsigned
`ios-arm64.ipa` is attached to the release, install it using AltStore, SideStore
or a compatible tool that re-signs it with your account. GitHub's Source code ZIP
is not the app. Update the existing `com.lukebrosious.theft4` installation in place
to preserve game files, saves, settings and prepared caches; do not delete it first.

For a first install, launch once to create **Files → On My iPhone/iPad → Theft4 →
game**, close the app, then copy the contents of the prepared game directory.
`default.xex` and `default.xexp` belong directly in `game`, not `game/game` or only
`update`. Use the legally obtained supported Xbox 360 USA base (title `545407F2`,
media `6AC07221`, base `0.0.0.5`) and matching TU8 (`0.0.0.5 → 0.0.8.5`). Reopen,
use **Check Game Files**, finish any required texture preparation, and press Play.
Raw ISOs and unopened update packages are not a prepared installation.

See [the complete sideload instructions](IOS_SIDELOAD_INSTALL.md) for staging,
the validated update hash, Files/Finder paths and troubleshooting. The deployment
minimum is ARM64 iOS/iPadOS 26.0; it is not proof of playability on every eligible
device. Game files, title updates, saves and private captures are not distributed.
The public artifact's SHA-256 will be recorded after it is built and audited.
