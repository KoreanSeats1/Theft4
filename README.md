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
the Xbox services the title expects. The existing Xenos renderer translates the
game's graphics workload through Vulkan and MoltenVK to Metal.

> [!WARNING]
> Theft4 is an experimental source release, not a packaged or broadly validated release. It currently
> targets developers comfortable with Xcode, CMake, dependency patching, and
> device logs. Full 3D and decoded audio have run on the test M5 iPad, but expect
> incomplete services, compatibility issues, and uneven heavy-scene performance.

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

The game has visibly booted on the test iPad, but this does **not** mean the port
is complete or generally playable. Broader physical-controller acceptance,
frontend/import UX, correctness, compatibility, performance, and
long-duration stability remain active work.

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
        +--> Xenos command processor and shader translation
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
