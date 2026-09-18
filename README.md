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
the Xbox services the title expects. Graphics can use the generic Xenos command
processor or an experimental GTA-IV-specific renderer; both currently present
through Vulkan and MoltenVK to Metal.

> [!WARNING]
> Theft4 is an experimental release and is not broadly validated. Version
> 0.1.3(a) is available as an unsigned sideload IPA and as TestFlight build 6,
> but expect incomplete services, compatibility issues, and uneven heavy-scene
> performance. Full 3D and decoded audio have run on the test M5 iPad and A19
> iPhone hardware.

## AI-assisted engineering and bespoke technology

Theft4 is deliberately a stress test of what is possible with agent-assisted
software engineering. OpenAI Codex agents—including GPT-5.6 Sol and GPT-6
Astra—were heavily used for investigation, design, coding, debugging,
documentation, build tooling, and device-test iteration.

That work produced project-specific technology, not just routine integration:

- a GTA-IV-aware native renderer that operates alongside the generic Xenos
  command processor and uses title-specific graphics hooks plus a native SPIR-V
  shader cache;
- an iOS rendering/presentation path that owns the UIKit `CAMetalLayer` and
  swapchain lifecycle, then presents through Vulkan and MoltenVK to Metal;
- signed ARM64 AOT application integration for the recompiled game runtime,
  native audio/input, launcher controls, device-safe defaults, and reproducible
  iOS build and verification tooling; and
- focused renderer investigations and performance experiments, with accepted
  defaults kept separate from device-specific or unfinished Lab work.

This is an active experiment in both game-runtime engineering and human-directed
AI collaboration. Every change remains subject to human review and real-device
validation; agent assistance does not turn experimental results into shipping
guarantees.

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

The GTA-IV-specific renderer has initialized on both the M5 iPad and an A19
iPhone, loaded its 1,356-shader SPIR-V cache, and entered title-specific 3D
rendering. Version 0.1.3(a) promotes device-independent command-transport and
CPU hot-path improvements while selecting only conservative defaults at launch.
It does not claim sustained 30 FPS: the first combined A19 experiment regressed
under render-worker backlog, and the revised phone profile still needs a matched
gameplay acceptance run.

The game has visibly booted on the test iPad, but this does **not** mean the port
is complete or generally playable. Broader physical-controller acceptance,
frontend/import UX, correctness, compatibility, performance, and
long-duration stability remain active work.

## Engineering record

### Before starting the game

The **After Hours** launcher places a procedural 3D city and suspension bridge
behind Play / Display / System navigation. Drag the city to change the view.
Its scene and effects are released before the game runtime starts.

**Display** offers remembered settings: **Frame counter** (on), **Touch controls**
(off), **Texture filtering · 4×** (on), **Motion blur** (on), **1080p enhanced output** (on) and
**Experimental FSR Boost** (off). These are defaults; existing preferences are
retained. Both FSR modes keep scene rendering at 720p and add spatial
upscaling/sharpening at presentation—not native-resolution detail or frame
generation. Boost targets a native-pixel 16:9 image for the current window,
bounded from 1080p to 4K; it may cost performance. Output is selected at launch.
User feedback on 1080p is positive; Boost still needs device visual/performance
validation. See [launcher and Boost notes](docs/THEFT4_LAUNCHER_AND_FSR_BOOST.md)
and the [1080p A/B playtest plan](THEFT4_1080P_OUTPUT_TEST_PLAN.md).
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

Theft4 defaults to **two native frame-resource slots** so the CPU can record the
next frame while the GPU finishes the current frame. This preserves the existing
paced-30 configuration but is not proof that all GPU freezes are fixed.
Developers can explicitly set `THEFT4_NATIVE_FRAMES_IN_FLIGHT=1` for the
stability-control path. The normal enhanced-output default renders at 720p and
uses FSR1 quality presentation to 1080p.

The project keeps a detailed public record of implementation work, experiments,
measured outcomes, rejected approaches, and remaining verification:

- [Engineering changelog](CHANGELOG.md) — the running, file-mapped technical record;
- [0.1.3(a) release notes](docs/RELEASE_0.1.3A.md) — promoted optimizations,
  device defaults, validation and known limits;
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

## Installing a release IPA

The unsigned IPA attached to a GitHub release must be re-signed with AltStore,
SideStore, or another compatible sideloading tool. It does not contain game
files. After installation, launch Theft4 once, then place the **contents** of a
validated prepared game folder in **Files → Browse → On My iPhone/iPad → Theft4
→ game**. The final layout must put `default.xex` and the matching TU8
`default.xexp` directly beside each other; do not create `game/game` and do not
copy a raw ISO or unopened title-update package to the device.

See [Install a sideloaded Theft4 IPA](docs/IOS_SIDELOAD_INSTALL.md) for the exact
supported base/TU8 versions and hashes, staging step, complete file tree, Finder
and Files transfer paths, verification step, update-in-place warning, and common
failure fixes. These instructions apply to release 0.1.3 and later unless a
newer release explicitly says otherwise.

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
