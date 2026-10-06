# Build Theft4 0.3 for iPhone and iPad

Use Release for gameplay. The default build entry points now select the live
**direct Metal** game renderer explicitly. They require offline iPhoneOS game
and host shader libraries and do not silently choose the legacy Vulkan route
when those inputs are missing. Legacy Vulkan/MoltenVK archives remain build
prerequisites; their presence does not mean normal Metal gameplay submits through
that backend.

0.3 source is committed to main. The local M5 build and host checks do not establish
that a completely fresh machine or public sideload package has been validated.
See [release status](THEFT4_0.3_MAIN_INTEGRATION.md),
[architecture](THEFT4_0.3_ARCHITECTURE.md) and [release notes](RELEASE_0.3.md).

## Prerequisites

- macOS, full Xcode with its installed Metal compiler component, CMake 3.29+,
  Python 3.10+, and the repository's pinned submodules.
- ARM64 iOS/iPadOS 26.0 or later and a development-enabled physical device for
  local installation. A deployment minimum is not a performance guarantee.
- An Apple team/account for development signing, or a compatible sideloading
  tool for signing a public unsigned IPA.
- The existing iPhoneOS Release archives: `libMoltenVK.a`,
  `libMoltenVK_ShaderConverter.a`, `libMoltenVK_Common.a`, `libspirv-cross.a`,
  `libSPIRV-Tools.a`. Set `THEFT4_MOLTENVK_IOS_LIB_DIR` if they are not under the
  sibling `XeniOS/build-ios-xcode/obj/iOS/Release` directory. Do not substitute
  simulator/macOS archives. Self-contained dependency packaging remains work.

Initialize dependencies without discarding local changes:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
python3 tools/setup_repo.py
python3 tools/setup_repo.py --check
```

## Prepare the offline Metal libraries

Use the complete stock corpus, not the small `gta_im` Lab filter. These commands
export game shader metadata, host utility metadata and MSL, then compile for iOS:

```sh
cmake -S tools/direct-metal -B out/direct-metal/build -DCMAKE_BUILD_TYPE=Release
cmake --build out/direct-metal/build --parallel 6
out/direct-metal/build/metal_shader_export out/direct-metal/corpus
out/direct-metal/build/metal_host_shader_export out/direct-metal/host-corpus
python3 tools/direct-metal/compile_shaders.py out/direct-metal/corpus out/direct-metal/ios --platform ios
python3 tools/direct-metal/compile_shaders.py out/direct-metal/host-corpus out/direct-metal/ios/Host --platform ios
export THEFT4_METAL_LIBRARIES="$PWD/out/direct-metal/ios"
export THEFT4_METAL_HOST_LIBRARIES="$PWD/out/direct-metal/ios/Host"
```

Check both compilation reports for zero rejections. The output needs the complete
compiled libraries and `manifest.tsv`; Host needs `HOST_SHADER_MANIFEST.json` and
its compiled programs. Shader compile success alone is not frame/gameplay proof.
Safe math is the shipping compiler policy; do not introduce relaxed/fast-math or
experimental uniform/depth variants without a separate fidelity comparison.

The local tested shader bundle contains 2,712 stock/late/depth libraries plus
24 host utilities. The exporter is the source of truth when the corpus changes.
Do not bundle private draw captures, user texture caches or extracted game data.

## Generate and install a development app

```sh
./Generate-Theft4-Xcode.command YOUR_TEAM_ID
cmake --build --preset theft4-device-release --parallel 6
python3 tests/ios/verify_release_build.py out/build/ios-device-release
```

The generator opens `out/build/ios-device-release/LibertyRecomp-ALL.xcodeproj`.
Select Theft4, Release and the physical device. For normal play, uncheck **Run →
Info → Debug executable**; keep Metal API/shader validation and GPU capture Off.
The output is `out/build/ios-device-release/theft4/Release/Theft4.app`.
The generator requests sustained execution by default, matching the tested M5
profile; `THEFT4_SUSTAINED_EXECUTION=OFF` is available when comparing that setting
or when the signing profile requires it. Game Mode is declared separately.
Neither setting guarantees identical OS scheduling on every device.

Install and open the launcher without attaching the debugger:

```sh
xcrun devicectl device install app --device YOUR_DEVICE_ID out/build/ios-device-release/theft4/Release/Theft4.app
xcrun devicectl device process launch --device YOUR_DEVICE_ID com.lukebrosious.theft4
```

Close an existing game safely first; update the app in place. Never delete its
container to install a performance candidate. Do not use automatic Play flags
when installing or verifying a launcher-only update.

## Prepare a public unsigned sideload package

```sh
THEFT4_MOLTENVK_IOS_LIB_DIR=/absolute/path/to/ios-release-libraries \
  ./tools/build_ios_release.sh 0.3.0
```

The script explicitly enables direct Metal and its normal-launch default, selects
the supplied offline shaders, disables private game-asset draw capture, applies
public source-prefix maps, verifies real `-O3 -DNDEBUG` compiler arguments and
runs the existing IPA privacy/architecture audit. It disables development signing
and packages under `dist/`; it does not publish a GitHub or TestFlight release.
The resulting unsigned IPA must be re-signed by the recipient's sideloading tool.
Public signing/entitlement behavior is a separate validation from the M5's local
Apple Development profile.

The build entry points record the source commit in `Theft4SourceRevision`.
A clean source checkpoint, exact bundle version, required shader assets and
artifact hash are part of promotion review. CMake's configuration name alone is
insufficient: an earlier empty Release flag built the renderer at `-O0`.

## Diagnostics, comparisons and game installation

Normal icon launches always suppress development telemetry, independent of old
Retail Mode preferences; the toggle has been removed. Optional FPS, CPU and
frame-time overlays do not enable development probes. Enable Long Performance
Capture before Play for bounded timings; it resets Off on each app launch. Runtime
correctness checks and resource synchronization remain required.
Ordinary builds omit the private asset draw recorder; engineering replay builds
can enable `THEFT4_NATIVE_METAL_CAPTURE` deliberately.

An explicit comparison build uses `THEFT4_RENDERER=vulkan`. The helper sets both
Metal options Off so a cached Metal selection cannot contaminate that comparison.
Its game/host library environment is not required, but existing legacy archives
are. Do not label the comparison binary as a verified direct Metal release.

Use [sideload installation](IOS_SIDELOAD_INSTALL.md) for the supported lawful game
revision, title update, prepared directory layout and Files/Finder transfer.
**Check Game Files** validates an already-updated installation before asking for
an unnecessary update package. Existing app data lives in the same official
container; the historical ASTC/Metal Lab apps have separate containers.

Initial spikes, focus recovery, long-session behavior and non-M5 performance still
need testing. Local compilation, saved draw replays and a Lab pass do not prove
locked 30 FPS or complete native source rewrites of physics/traffic.
