<p align="center">
  <a href="https://www.paypal.com/donate/?hosted_button_id=XSBF9HXAS89EL"><img src="https://img.shields.io/badge/Support%20our%20work-Donate-0070ba?logo=paypal&logoColor=white" alt="Support our work via PayPal"></a>
</p>

> **[Support our work via PayPal](https://www.paypal.com/donate/?hosted_button_id=XSBF9HXAS89EL)** — donations support continued development, testing, and future projects: MCLA, Max Payne 3, Call of Duty: World at War Zombies, and Saints Row.

<p align="center">
  <img src="docs/images/banner_repo.png" alt="Liberty Recompiled" width="800">
</p>

# Theft4

Theft4 is an experimental, title-specific static ahead-of-time recompilation of
the Xbox 360 release of Grand Theft Auto IV for ARM64 iOS and iPadOS. The
original PowerPC game instructions are translated to C++ ahead of time and
compiled into the signed application. Theft4 does not generate or download CPU
code at runtime and does not require a JIT entitlement.

This is not a source port and it is not a complete Xbox 360 emulator. It combines
ahead-of-time translated game code with a compatibility runtime that recreates
the Xbox services the title expects. The 0.2.0 iOS release promotes the GTA IV
native renderer developed in Theft4 Lab. It presents through Vulkan, MoltenVK
and Metal. Direct device builds keep `com.theft4.bringup`; the existing
TestFlight app keeps `com.lukebrosious.theft4`.

> [!WARNING]
> Theft4 0.2.0 remains experimental. Dense scenes, extended play, physics,
> heat and device compatibility still need testing. The GitHub IPA is an
> unsigned sideload package; TestFlight uses a separately signed build.

## What changed in 0.2.0

Theft4 Lab is now the regular Theft4 codebase. Direct device/sideload builds
retain `com.theft4.bringup`; the existing TestFlight app retains
`com.lukebrosious.theft4`. Each 0.2.0 build updates its corresponding 0.1.3
installation in place, preserving that app's data. They do not share a data
container. The historical Lab app (`com.theft4.m5lab`) remains separate.

| Area | Substantial change | Effect |
| --- | --- | --- |
| Command delivery | Batched producer-to-worker transfer, tighter queue locks, bounded storage reuse and compact state packets instead of full draw packets for common state changes | Less allocation, copying and lock waiting under dense command loads. |
| Redundant state | Skip identical binding notifications and repeated vertex/index binds after validating resource identity; reuse immutable vertex-layout requirements | Avoid work that leaves GPU state unchanged while retaining draw order and resource ownership. |
| Shader constants | Reuse validated parent snapshots and apply only changed ranges into fenced frame allocations | Avoid whole-block materialization and byte conversion for small updates. |
| Textures | Separate texture content identity from sampling state; cache verified generations and limit A19 stage walks to shader-used slots | Reduce decode, hashing, upload planning and unused-stage traversal while still validating dirty resources. |
| Pipelines and worker | Bound A19 prewarming, defer draws whose new pipeline is compiling, and record worker phases and queue pressure | Reduce avoidable stalls and make remaining hitches diagnosable. Newly visited areas still need visual checks. |
| Frame lifetime | Two completion-owned native frame slots and bounded resource retirement | Preserve CPU/GPU overlap while protecting in-flight buffers and textures. |
| Output and power controls | Independent 540p/720p/900p/1080p scene selection, FSR, graphics presets, a performance preset and a 1080p A19 output cap | Let testers reduce pixel work and tune quality against sustained speed and heat. |
| Diagnosis | Frame-time graph, bounded 600-frame CPU/GPU capture, runtime logs and System-tab export to Files | Make TestFlight slow-frame reports actionable without a debugger. |

One useful instrumented comparison is the M5 iPad build 32 to 33 command
transfer change at 900p. Mean renderer interval fell from 40.57 to 37.84 ms,
p95 from 47.58 to 45.58 ms, and intervals over 40 ms from 357/599 to 182/599.
Mean batch-transfer time fell from 4.53 to 0.16 ms and queue-lock wait from
7.40 to 1.54 ms. The routes and GPU work differed, so the whole-frame numbers
are directional rather than a controlled speedup. The targeted transfer and
lock spans show the clearest improvement. See the
[build 33 review](docs/lab-experiments/build33-device-review.md).

Later heavy scenes still miss the 33.3 ms target. An A19 iPhone Air capture in
serious thermal state had a 44.5 ms median renderer interval versus 33.2 ms
in a different, mostly nominal run. The scenes and output sizes differed as
well. We cannot yet claim a measured temperature reduction or locked 30 FPS.
Lower resolution reduces planned pixel work, while the CPU changes reduce
measured command overhead. Same-route, same-settings play after warmup is needed
to measure sustained frame times, comfort and battery use. See the
[A19 capture review](docs/lab-experiments/build40-a19-air-two-capture-review.md)
and [0.2.0 release notes](docs/RELEASE_0.2.0.md).

### Capture and share a slow scene

Before starting the game, open **System** and turn on **Detailed performance
capture**. In the slow scene, double-tap the frame-time graph to start the
600-frame capture. Wait for completion, then quit and reopen Theft4. Tap
**Download Latest Log Capture**. The app saves
a dated text bundle in **Files → On My iPhone/iPad → Theft4 → Diagnostics** and
opens the share sheet. Send the file with the device model, scene/route,
graphics settings and whether the device felt hot. Profiling adds overhead;
repeat normal play with capture off when judging FPS.

## Current status

The following has been demonstrated on a physical ARM64 iPad:

- a development-signed UIKit application containing the statically compiled GTA IV AOT code;
- Xbox guest memory, kernel, threading, filesystem, XEX loading, and TU8 patch application;
- execution reaching and continuing beyond the recompiled title entry point;
- real GTA IV PM4 command processing and on-device Xenos shader translation;
- Vulkan shader and pipeline creation through statically linked MoltenVK;
- a UIKit-owned `CAMetalLayer`, three-image swapchain, and repeated Metal presentation;
- native GameController and RemoteIO integration at the host boundary;
- real XMA decoding, improved audio delivery, and centered 16:9 presentation;
- the full opening 3D sequence and first player-control state in a Release build.

The promoted native renderer has reached gameplay on the M5 iPad. The measured
captures above show real improvement in command delivery, while frame time
still varies substantially in heavy scenes.

The game has visibly booted on the test iPad, but this does **not** mean the port
is complete or generally playable. Broader physical-controller acceptance,
frontend/import UX, correctness, compatibility, performance, and
long-duration stability remain active work.

## Engineering record

### Before starting the game

The **After Hours** launcher places a procedural 3D city and suspension bridge
behind Play / Graphics / Interface / System navigation. Drag the city to change the view.
Its scene and effects are released before the game runtime starts.

**Graphics** offers independent scene resolution and FSR choices, texture
filtering, shadows, draw distance, model detail, reflection quality, edge
smoothing and motion blur. The performance preset starts with 540p + FSR and
conservative quality settings; all controls can be adjusted afterward. Changes
apply at the next game launch. **Interface** provides the FPS counter,
frame-time graph and touch controls. Existing preferences remain in the app
data after an in-place update.
Motion blur applies at the next game launch. Turning it off selects the stock
non-blur composite variant; it does not disable depth of field or the entire
post-processing pass. Device visual/performance acceptance is pending. See the
[motion-blur and fast-driving test plan](docs/THEFT4_MOTION_BLUR_AND_STREAMING.md).

Enable touch controls for
a movement stick, swipe-to-look on empty screen space, Xbox buttons/triggers,
D-pad, and L3/R3. Physical controllers remain supported with either setting.
The compact touch layout is adapted from XeniOS; its attribution and license
are bundled with Theft4. The full XeniOS layout editor is not included.

Touch gameplay and save/reload are still undergoing device verification.
GPU freezes during extended play and app switching are known issues; the
input/display switches do not change renderer stability settings.

The promoted renderer uses two completion-owned native frame slots. Scene
resolution and output mode are selected in Graphics before launch; 720p remains
available. Long-session stability and background/foreground recovery still
need device testing.

The project keeps a detailed public record of implementation work, experiments,
measured outcomes, rejected approaches, and remaining verification:

- [Engineering changelog](CHANGELOG.md) — the running, file-mapped technical record;
- [3D performance execution plan](THEFT4_3D_PERFORMANCE_PLAN.md) — ordered work and
  the latest renderer checkpoint;
- [CPU-first native-renderer audit](docs/THEFT4_CPU_PERFORMANCE_AUDIT.md) — the
  current 1080p city CPU profile, logging/validation costs, safe optimization
  experiments and paced-30 acceptance criteria;
- [Current paced-30 implementation plan](docs/THEFT4_30FPS_IMPLEMENTATION_PLAN.md)
  — ordered CPU optimization passes, tests and keep/revert gates; planned,
  not yet implemented;
- [September 16 GPU diagnosis](THEFT4_GPU_DIAGNOSTIC_2026-09-16.md) — trace-backed
  generic-renderer analysis and experiment design;
- [iOS architecture report](LIBERTYRECOMP_IOS_ARCHITECTURE.md) and
  [implementation plan](LIBERTYRECOMP_IOS_PLAN.md) — the original port audit and
  milestone architecture.

The changelog distinguishes validated defaults from opt-in experiments and
records failed approaches so they are not rediscovered. Private game data,
captures, logs, device identifiers, and signing material are deliberately excluded.

## Architecture

```text
Theft4 UIKit application
        |
        v
Versioned C bridge and iOS lifecycle adapters
        |
        v
LibertyRecomp / ReXGlue compatibility runtime
        |
        +--> statically recompiled GTA IV code (PowerPC -> C++ -> ARM64)
        +--> Xbox kernel, memory, threading, filesystem, input and audio services
        +--> generic Xenos command processor + runtime shader translation
        |
        +--> opt-in GTA-IV-specific renderer + cached native SPIR-V
                                   |
                                   v
                            Vulkan / MoltenVK
                                   |
                                   v
                                 Metal
```

The iOS application owns `UIApplication`/`UIScene`, the visible view and
`CAMetalLayer`, device storage, user interaction, and lifecycle. The runtime is
embedded in-process as statically linked code behind a small C interface.

## No game files are included

This repository contains no ISO, title update, extracted game assets, saves, or
Rockstar Games source code. You must supply files from your own legally obtained
Xbox 360 copy. Do not open issues requesting copyrighted game files, download
links, signature-check bypasses, or prebuilt bundles containing game data.

The currently validated input is the supported USA retail Xbox 360 base and its
matching title update. Input validation is intentionally strict; files accepted
by an emulator are not automatically compatible with this title-specific AOT
build.

## Building

**For normal play, follow [Build and run the Release app](docs/IOS_RELEASE_BUILD.md).**
The generator defaults to optimized **Release**, not the old Debug/core-probe
configuration. This is the same app/runtime source used for the latest on-device
tests, not a second emulator. A fresh-clone end-to-end build is not yet certified;
the external MoltenVK prerequisite is documented explicitly.

Initialize the pinned public dependencies and apply the reviewed dependency
patches:

```sh
git clone --recurse-submodules https://github.com/KoreanSeats1/Theft4.git
cd Theft4
python3 tools/setup_repo.py
python3 tools/setup_repo.py --check
```

On macOS, `Generate-Theft4-Xcode.command` validates the dependencies and
generates the Release Xcode project. Pass your Apple development-team ID as
its optional first argument to configure device signing:

```sh
./Generate-Theft4-Xcode.command YOUR_TEAM_ID
```

The current graphics bring-up expects the documented public MoltenVK archives;
the generator fails with an explicit explanation if they have not been built.
Then follow:

- [iOS core build](docs/IOS_CORE_BUILD.md)
- [iOS application and device build](docs/IOS_APP_BUILD.md)
- [real AOT startup status](docs/IOS_GAME_STARTUP.md)
- [desktop/upstream build guide](docs/BUILDING.md)
- [lawful dumping guide](docs/DUMPING-en.md)

The default Xcode project is generated under `out/build/ios-device-release/`.
Select **Theft4**, your physical device, and **Release**. For normal play,
uncheck **Edit Scheme → Run → Info → Debug executable**, or launch the installed
app directly on the iPad. Leave capture/validation and opt-in diagnostic flags off.
The new guide covers signing, game-file transfer, starting the game, and the
explicit Debug opt-in. CMake files, not generated project build settings, remain
the source of truth.

The GTA-IV-specific renderer is currently an experimental build/launch option,
not the broadly validated default. Its switches, exact measured result, retail
fidelity settings, and fallback behavior are documented in the
[engineering changelog](CHANGELOG.md).

GitHub's automatic source ZIP does not contain the contents of Git submodules.
For a complete checkout, use the recursive clone command above. A signed IPA is
not distributed: every developer must build and sign with their own Apple
account, and the application never contains game files.

## Origins and credits

Theft4 is built on substantial existing open-source work. It began as an iOS
porting branch of [LibertyRecomp](https://github.com/OZORDI/LibertyRecomp) and
preserves that project's Git history and GPL license. The runtime and translation
stack draws heavily from ReXGlue and Xenia; its ahead-of-time approach was
inspired by XenonRecomp; graphics uses XenosRecomp, Vulkan, SPIR-V tooling, and
MoltenVK. FFmpeg, SDL, and numerous smaller libraries are included or referenced
through pinned dependencies.

See [Third-party projects and attribution](docs/ATTRIBUTION.md) and the license
files in each dependency for details. XeniOS was used as an iOS behavior and
debugging reference during bring-up; XeniOS is not embedded as Theft4's runtime.

## License and trademarks

The project-level license is [GPL-3.0](COPYING), inherited from LibertyRecomp.
Vendored and submodule dependencies retain their respective licenses.

Theft4 is an unofficial community research project. It is not affiliated with or
endorsed by Rockstar Games, Take-Two Interactive, Microsoft, Xbox, Apple, or the
maintainers of the upstream projects. Grand Theft Auto, GTA, Xbox, iOS, iPadOS,
Metal, and other names are trademarks of their respective owners.
