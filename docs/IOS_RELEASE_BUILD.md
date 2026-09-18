# Theft4 — build and play the Release app

Use **Release for normal play**. Debug is for development, not representative
performance. Both configurations build the same Theft4 application: native AOT
game code plus the ReXGlue runtime and Vulkan → MoltenVK → Metal graphics path.
No CPU JIT setup or debugger attachment is required to play.

The device Release preset explicitly sets C/C++ flags to `-O3 -DNDEBUG`.
The configuration name alone does not guarantee an optimized executable: an
empty cached Release flag previously produced an `-O0` native renderer with an
oversized stack frame, followed by very low gameplay FPS after its stack was
enlarged. The release script now checks actual compiler response files for the
AOT game, bridge, and native renderer before packaging. To check a local build:

```sh
python3 tests/ios/verify_release_build.py out/build/ios-device-release
```

The native render worker also has an explicit 2 MiB stack on Apple platforms.
The native backend, two-frame resource ring, and motion-blur option are retained.

The latest on-device Release tests reached the opening 3D cutscene and the first
driving/player-control state on an M5 iPad Pro, with substantially improved audio.
This remains experimental: heavy views can be slow; other devices, long sessions,
controller combinations and lifecycle recovery need further validation.

## What is and isn't provided

- Source and an Xcode-project generator, **not a signed IPA or TestFlight release**.
- Real game startup, rendering and audio, not just the old core probe.
- No ISO, title update, extracted game files, saves or private shader caches.
- No requirement to run XeniOS alongside Theft4. Its public MoltenVK build is
  currently a build-time dependency source, described below.
- This guide's generator routing has automated checks. It does **not** establish
  a successful clean-machine build of the entire dependency stack.

## 1. Requirements and source

Use a Mac with full Xcode, its command-line tools and a physical ARM64 iPad/iPhone
configured for development. The tested setup used Xcode 27 / Apple Clang 21 and
an M5 iPad on iPadOS 27; the generator sets deployment minimum 26.0. That minimum
does not prove every iOS 26 device is supported. CMake 3.29+ and Python 3.10+ must
be on PATH. Supply your own Apple development-team ID and device signing account.

```sh
git clone --recurse-submodules https://github.com/KoreanSeats1/Theft4.git
cd Theft4
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
```

For an existing clone, pull your branch normally first. Do not discard local
changes to update. The generator runs the repository's pinned-dependency setup
and verification; plain GitHub ZIP downloads do not contain submodule sources.

## 2. Graphics dependency prerequisite

Before running the generator, build the public iOS MoltenVK archives described
near the top of [IOS_GAME_STARTUP.md](IOS_GAME_STARTUP.md). The default lookup is
the sibling `../XeniOS/build-ios-xcode/obj/iOS/Release` directory. It must contain:

```text
libMoltenVK.a
libMoltenVK_ShaderConverter.a
libMoltenVK_Common.a
libspirv-cross.a
libSPIRV-Tools.a
```

Alternatively point at your compatible **iPhoneOS ARM64 Release** archives:

```sh
export THEFT4_MOLTENVK_IOS_LIB_DIR=/absolute/path/to/ios-release-libraries
```

Do not substitute macOS or simulator archives. The helper checks presence, not
full binary compatibility; incompatible archives will fail at link/run time.
Do not overwrite an unrelated working XeniOS build to experiment with Theft4.
Self-contained dependency packaging is still pending.

## 3. Generate, build and sign Release

```sh
./Generate-Theft4-Xcode.command YOUR_TEAM_ID
```

The default selects `ios-device-release`, enables the game-code/runtime startup
targets, configures development signing, and opens:

```text
out/build/ios-device-release/LibertyRecomp-ALL.xcodeproj
```

In Xcode:

1. Select the **Theft4** scheme and your connected physical device, not a simulator
   or the aggregate ALL_BUILD target.
2. In **Product → Scheme → Edit Scheme → Run → Info**, verify **Release** and
   **uncheck Debug executable**. Release optimization and debugger attachment
   are separate settings. The generator does not automatically uncheck this box.
3. Leave Metal capture, API/shader validation, sanitizers and profiling tools off
   for ordinary play. The Release-generated scheme disables Metal capture and
   API/shader validation by default. If reusing an old scheme, check it manually.
4. Build/install. If signing fails, resolve your account/team/device provisioning
   in Xcode; no personal signing identity is shipped in this repository.

An equivalent terminal build after generating:

```sh
cmake --build --preset theft4-device-release --parallel 4 -- -allowProvisioningUpdates
```

Output: `out/build/ios-device-release/theft4/Release/Theft4.app`.
Release is still **development signed**, not an App Store/distribution build.
Symbol information and ordinary startup logs may remain; that does not make it
a Debug build. No `get-task-allow` entitlement should be confused with CPU JIT.

### Build a public sideload package

A development-signed app cannot be installed broadly: its provisioning profile
only authorizes registered devices and contains developer/device metadata. For a
GitHub release, build with stable source paths and package an unsigned IPA that
the user's sideloading tool will re-sign with that user's Apple account:

```sh
cmake --preset ios-device-release \
  -DREXGLUE_RUNTIME_ONLY=ON \
  -DREXGLUE_HEADLESS_KERNEL=ON \
  -DTHEFT4_BUILD_GAME_CODE=ON \
  -DTHEFT4_ENABLE_GAME_STARTUP=ON \
  -DTHEFT4_PUBLIC_BUILD=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DTHEFT4_XENIOS_IOS_LIB_DIR="$THEFT4_MOLTENVK_IOS_LIB_DIR"
cmake --build --preset theft4-device-release --parallel 4
./tools/package_ios_ipa.sh \
  out/build/ios-device-release/theft4/Release/Theft4.app
```

The packager verifies ARM64, rejects simulator slices, local checkout paths and
bundled game files, removes the developer signature and provisioning profile,
and writes the `.ipa` plus its `.sha256` file under `dist/`. Do not upload a
development-signed `.app` or export your signing certificate for this workflow.
The resulting IPA is not directly installable by itself; AltStore, SideStore or
another compatible tool must re-sign it for the destination device.

For subsequent releases, after updating the two version keys in
`ios/Theft4/Info.plist.in`, the checked-in pipeline performs the same configure,
unsigned Release build, privacy audit and IPA packaging in one command. The
optional argument fails fast if the intended version and plist disagree:

```sh
THEFT4_MOLTENVK_IOS_LIB_DIR=/absolute/path/to/ios-release-libraries \
  ./tools/build_ios_release.sh 0.1.3
```

You can install without attaching Xcode's debugger:

```sh
xcrun devicectl list devices
xcrun devicectl device install app --device YOUR_DEVICE_ID \
  out/build/ios-device-release/theft4/Release/Theft4.app
```

## 4. Prepare and transfer your game files

If you installed a published IPA rather than building in Xcode, use the
step-by-step [sideload installation guide](IOS_SIDELOAD_INSTALL.md). It is the
canonical release-user guide and includes the exact accepted base/update
identity, final file tree, and transfer troubleshooting.

Use your legally obtained supported Xbox 360 USA retail base (media ID
`6AC07221`) and the matching TU8 patch for **0.0.0.5 → 0.0.8.5**. An ISO working
in an emulator is not sufficient proof of this exact version match. Intermediate
updates are unnecessary for that validated patch. Never bypass identity checks.

The current iOS shell expects a **prepared installation**, not a raw ISO or TU
package. The repository installer validates/stages the game and update; the
developer `theft4_inspect`/`theft4_stage` tools and their earlier simulator usage
are documented in [IOS_APP_BUILD.md](IOS_APP_BUILD.md). There is not yet a polished
on-device ISO import flow. If you do not have a validated prepared installation,
complete that developer staging step first; copying an ISO into Files won't boot.

The transferred layout must be:

```text
Theft4 Documents/
  game/
    default.xex
    default.xexp
    update/  # present when staging an STFS/SVOD title-update package
    ...the rest of the validated game installation...
```

`default.xex` and `default.xexp` are always required as siblings. If staging
used a raw `default.xexp` rather than a packaged update, an `update/` directory
is not required.

Launch Theft4 once. It creates **Files → On My iPhone/iPad → Theft4 → game**
and a short instruction file automatically. Open `game` and copy the **contents**
of the prepared game folder into it. On a Mac, the same location is available at
**Finder → your device → Files → Theft4 → game**. Wait until transfer finishes.
Do not create another nested `game` folder. Preserve the original ISO/update and
existing saves.

The current startup path stores runtime user/save/cache data under
`Library/Application Support/Theft4/startup`, not a transferred `Documents/User`
folder. Copying `User` alongside `game` does not import those saves automatically.
Game files are not baked into the application by this build path.

## 5. Start and play without a debugger

Open Theft4 on the iPad and tap **Attempt game startup**. Some UI text still
describes the older bring-up phase; this button invokes the real AOT game.
**Prepare transferred game** is validation/loading only and deliberately stops
before execution. **Restart core probe** does not restart the running game;
close and reopen the app for a fresh game launch.

For a direct terminal launch after transfer:

```sh
xcrun devicectl device process launch --device YOUR_DEVICE_ID --activate \
  com.theft4.bringup --theft4-start-game
```

Use the default bundle ID above unless you changed it when configuring. Start
from a closed app for this one-shot startup path. Pair a compatible controller;
the GameController backend is connected, but the shell has no touch driving
controls and controller acceptance coverage remains limited.

For normal play, do **not** add the opt-in flags `THEFT4_DIAGNOSTICS`,
`THEFT4_AUDIO_TIMING`, `THEFT4_TRACE_SCENE`, `THEFT4_FRAME_CAPTURE_DIR`, or
`THEFT4_RASTER_OPEN`. Do not copy old experimental environment overrides into
your scheme. Leave XMA/recovery, render-target and occlusion settings at their
source defaults. Raster-open intentionally renders incorrectly; it is not a fix.

If Xcode stops on a guest memory protection fault, stop that debugging session
and launch without the debugger as above. Do not globally suppress every signal
as a workaround. If the independently launched app actually exits, retain its
runtime log and report the failure; not all faults are intentional.

## 6. Debug is optional and separate

Developers who specifically need Debug can request it:

```sh
THEFT4_BUILD_CONFIGURATION=Debug ./Generate-Theft4-Xcode.command YOUR_TEAM_ID
```

This generates `out/build/ios-device-debug/LibertyRecomp-ALL.xcodeproj` without
changing the Release build tree. Do not use Debug FPS as a Release benchmark.
Missing team ID produces an unsigned project, not an installable device app.

## Known limits / next work

The Release code contains the current audio and aspect improvements. The next
shader-cache loading fix and subsequent graphics optimization passes are only
planned: see [the performance plan](../THEFT4_3D_PERFORMANCE_PLAN.md). Preloading
is not yet proven to work correctly across launches. Do not advertise this as a
finished, full-speed or clean-machine-validated public binary release.
