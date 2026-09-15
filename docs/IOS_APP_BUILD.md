# Theft4: M2 iOS application shell

> **Historical milestone notes, not the current play guide.** For the optimized
> full-game build, use [Release build and play instructions](IOS_RELEASE_BUILD.md).
> The current app has reached full 3D and decoded audio; the Debug/probe-only
> limitations below record earlier development stages.

**Latest result:** the subsequent [real AOT startup bring-up](IOS_GAME_STARTUP.md)
now runs the recompiled GTA IV entry point on the physical M5 iPad. It stops at
an explicit missing-graphics guard; no gameplay frame is rendered. The historical
shell-only and loader-only limitations below describe earlier stages.

Verified 2026-09-14. Theft4 now launches as a development-signed UIKit app on
the connected iPhone Air and as an ARM64 iOS 27 simulator app. It calls the
statically linked LibertyRecomp core through an opaque C interface. This is
one iOS app, not a macOS launcher plus a separate runtime process.

**Current user direction:** simulator work plus builds/launches on the user's
physical M5 iPad Pro are now authorized. The earlier simulator-only restriction
is lifted for that iPad, not the iPhone. Historical iPhone observations below
remain separate from the current iPad work.

This milestone does **not** initialize `rex::Runtime`, guest mappings, fibers,
fault handlers, GPU/audio devices, or generated game functions. No game files
are included. “Core active” describes the small host probe, not a running game.

## Architecture and files

- `ios/CMakeLists.txt`: separate `Theft4` bundle, static `theft4_core_bridge`,
  and pure-C consumer test target in the M1 opt-in graph.
- `ios/Theft4/main.m`: UIKit app/scene delegates, diagnostic view controller,
  scene-owned core handle, restart control, and bounded sandbox diagnostics.
- `ios/Theft4/Info.plist.in`: single-scene lifecycle, iPhone/iPad orientations,
  generated launch screen, and bundle metadata. No background modes or privacy
  permission prompts are requested.
- `ios/bridge/theft4_core.h`: version 1 bring-up C ABI; no UIKit/C++ types.
- `ios/bridge/theft4_core.cpp`: copied paths, state validation, synchronous
  callbacks, and calls to real ReXGlue version/platform/page-size functions.
- `tests/ios/theft4_bridge_tests.c`: API validation, thread guards, native
  diagnostics, and repeated lifecycle/teardown tests, compiled as C.
- `tests/ios/run_shell_smoke.py`: installs into a booted simulator and verifies
  actual scene transitions while switching to Settings and back.
- `tests/ios/verify_shell_log.py`: verifies simulator or physical-device logs.
- `tests/ios/verify_core_artifact.py --uikit`: checks ARM64 platform identity,
  system-library closure, UIKit entry point, and linked C bridge symbols.

Objective-C UIKit plus a plain C++ bridge replaces the plan's proposed Swift /
Objective-C++ sketch for this milestone. No Apple UI objects enter the bridge,
so Objective-C++ and a Swift bridging configuration are unnecessary here. A
future Swift frontend can consume the same C header. No vendored runtime
source was changed for M2.

## Lifecycle contract

`create` produces READY. Activation moves READY/PAUSED to ACTIVE. Resigning
active pauses before background entry. Stopping is final and idempotent;
restart destroys the old handle and creates another. Scene disconnect stops
and destroys the handle. iOS can kill a suspended app without a termination
callback; future save handling must not depend on `destroy` being called.

All M2 operations run on the main thread and return immediately: there is no
game loop or worker to wait for. Wrong-thread calls are rejected. Callbacks
are synchronous and may query a snapshot but cannot mutate/destroy the handle.
No callbacks are scheduled after destruction. Version and struct-size checks
reject incompatible clients. Config paths are copied; the app retains callback
context until teardown completes. This is a bring-up API, not a frozen complete
engine contract. M3 must introduce an owned worker and asynchronous commands
before adding potentially blocking runtime initialization.

Lifecycle routing follows Apple's [UISceneDelegate contract](https://developer.apple.com/documentation/uikit/uiscenedelegate/).
UIApplication owns process startup; the scene owns the visible core session.

## Build and simulator verification

Use the public dependency setup and full-Xcode requirements in
[IOS_CORE_BUILD.md](IOS_CORE_BUILD.md). In this environment:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export PATH=/path/to/cmake-and-ninja/bin:$PATH
cmake --preset ios-simulator-debug
cmake --build --preset theft4-simulator-debug --parallel 4

xcrun simctl list devices booted
xcrun simctl spawn SIMULATOR_UUID \
  "$PWD/out/build/ios-simulator-debug/ios-smoke/Debug/theft4_bridge_tests.app/theft4_bridge_tests"

python3 tests/ios/run_shell_smoke.py --simulator SIMULATOR_UUID \
  --app out/build/ios-simulator-debug/theft4/Debug/Theft4.app \
  --screenshot out/build/ios-simulator-debug/theft4-m2.png

python3 tests/ios/verify_core_artifact.py \
  out/build/ios-simulator-debug/theft4/Debug/Theft4.app/Theft4 \
  --platform simulator --uikit
```

The simulator runner terminates/relaunches only `com.theft4.bringup`, switches
between it and Settings without changing settings, and leaves Theft4 active.
It verifies three startup teardown/recreation cycles plus twenty real UIKit
background/foreground cycles, requiring PAUSED/ACTIVE state respectively and
no error or guest-runtime initialization event. Use an explicit simulator UUID
because multiple booted simulators can exist.

## Device build, signing, and installation

Signing is opt-in; no personal signing team is committed to source. The existing
`ios-device-debug` configuration can still build the unsigned M1 probe. Only
the Theft4 application target opts into device signing when requested.

```sh
cmake --preset ios-device-debug \
  -DTHEFT4_SIGN_DEVICE=ON \
  -DLIBERTY_IOS_DEVELOPMENT_TEAM=YOUR_TEAM_ID
cmake --build --preset theft4-device-debug --parallel 4 -- -allowProvisioningUpdates

codesign --verify --deep --strict out/build/ios-device-debug/theft4/Debug/Theft4.app
python3 tests/ios/verify_core_artifact.py \
  out/build/ios-device-debug/theft4/Debug/Theft4.app/Theft4 --platform device --uikit

xcrun devicectl list devices
xcrun devicectl device install app --device DEVICE_ID \
  out/build/ios-device-debug/theft4/Debug/Theft4.app
xcrun devicectl device process launch --device DEVICE_ID \
  com.theft4.bringup --theft4-self-test
```

Default bundle ID is `com.theft4.bringup`; change it with
`-DTHEFT4_BUNDLE_IDENTIFIER=...` and use that ID in commands. Xcode may need an
authenticated developer account and permission to provision the target. The
verified build has only development signing entitlements: app identifier,
team identifier, and `get-task-allow`. No JIT, extended virtual-address-space,
increased-memory-limit, background execution, or special game entitlement was
used. This does not establish App Store eligibility or M3 memory feasibility.

For the physical-device lifecycle check, switch to Settings and back twenty
times (manually, or with the following commands on a test phone):

```sh
for cycle in {1..20}; do
  xcrun devicectl device process launch --quiet --timeout 20 --device DEVICE_ID com.apple.Preferences || break
  xcrun devicectl device process launch --quiet --timeout 20 --device DEVICE_ID com.theft4.bringup || break
done
xcrun devicectl device copy from --device DEVICE_ID \
  --domain-type appDataContainer --domain-identifier com.theft4.bringup \
  --source 'Library/Application Support/Theft4/lifecycle.jsonl' \
  --destination out/build/ios-device-debug/theft4-device-lifecycle.jsonl
python3 tests/ios/verify_shell_log.py \
  out/build/ios-device-debug/theft4-device-lifecycle.jsonl --platform ios-arm64
```

The final verifier—not successful dispatch of the commands—is the pass gate.
It selects the latest completed startup self-test process, or accepts `--pid`
to inspect a specific run. Every new cold launch needs `--theft4-self-test`
to produce the three teardown checkpoints used by this verifier.

## Observed results and limitations

- Both iOS SDK targets compile and link. UIKit/CoreFoundation are allowed only
  for the app verification mode; the M1 library probe retains its tighter check.
- C consumer tests: exit 0, 100 lifecycle/teardown cycles and 700 events,
  including bad inputs, ABI mismatch, idempotent stop, and wrong-thread calls.
- Simulator: twenty actual UIKit background/foreground cycles plus three
  explicit startup teardown cycles; visible layout inspected from screenshot.
- Physical iPhone Air: signed installation and cold launch succeeded. Captured
  PID 17584 reported `ios-arm64`, ABI 1, core `0.9.0.0-dev.unknown`, and 16,384
  byte pages. It logged 20 background entries, 21 active entries, 3 destroys,
  and 1 completed self-test with no failure events. Guest runtime remained off.
- The final bundle was rebuilt/reinstalled after adding the missing upside-down
  iPad orientation declaration. Device signing verification succeeds with
  keychain access; the restricted-shell check reported `CSSMERR_TP_NOT_TRUSTED`
  until repeated outside that sandbox. No trust settings were changed.
- Build configuration still reports the upstream missing-version-tag fallback.
  Existing core warnings from M1 remain; this does not validate PPC FP behavior.
- Natural scene-disconnect and OS memory-pressure delivery were not forced.
  Their handlers exist; synthetic API teardown and memory-warning state were
  tested. No Instruments leak/performance run or iPad UI validation was done.
- Diagnostics use the `com.theft4.bringup` / `lifecycle` unified-log category and
  a JSONL file under Application Support/Theft4. The probe bounds this file to
  approximately 256 KiB by restarting it at the limit. No save/import data is
  touched. Production log rotation and crash capture belong to later work.
- No app icon/product artwork or game-import UI is included in this harness.

## Original M3 proposal (boot-priority direction below supersedes the sequence)

Add isolated, bounded simulator probes behind the C interface for the
guest address-space reservation/alias model, 16 KiB protection granularity,
fault recovery, fibers, and thread/synchronization primitives. Put this work
on an owned worker with explicit cancellation. Capture each result without
starting the full guest, and stop at a failed architectural gate. Physical-device
validation is now permitted on the M5 iPad; simulator results alone cannot close
the real-device memory/fault/fiber feasibility gate. The current
success proves UIKit/static-core integration, not the game's memory/runtime
or renderer compatibility.

## Boot-priority build work

The user subsequently requested prioritizing a real game boot over the full
test-gate sequence. Use the simulator and authorized M5 iPad, and build the actual game
and loader dependencies now and defer repeated stress suites.

```sh
cmake --preset ios-simulator-debug \
  -DTHEFT4_BUILD_GAME_CODE=ON -DREXGLUE_RUNTIME_ONLY=ON
cmake --build out/build/ios-simulator-debug --config Debug \
  --target theft4_game_code rexruntime theft4_inspect --parallel 2
```

The complete AOT source corpus (84 chunks plus initialization and registration)
has compiled into `out/build/ios-simulator-debug/game-code/Debug/libtheft4_game_code.a`
(ARM64, 86 objects, approximately 323 MiB Debug). No generated source was changed.
Apple Clang 21 spent over ten minutes in `llvm::Localizer::localizeIntraBlock`
for `gta4_register.cpp`; sampling identified this specifically. A source-local
`-fno-global-isel` flag completed that translation unit without changing its
38,606 registration calls. All other files retain normal instruction selection.

`REXGLUE_RUNTIME_ONLY` extends the opt-in core graph with the real filesystem
and system-runtime sources as a static archive. It does not yet include the
complete kernel/media/UI implementations needed to link and boot the title.
The system archive built successfully for simulator ARM64 (approximately 77 MiB
Debug), and the ISO inspection executable linked successfully in the same build.
An archive's successful compilation is not proof of a working game executable.
The existing M2 shell does not link or start these optional game components yet.

`theft4_inspect` reuses the upstream installer's read-only ISO/folder inspection:

```sh
xcrun simctl spawn SIMULATOR_UUID \
  "$PWD/out/build/ios-simulator-debug/game-tools/Debug/theft4_inspect.app/theft4_inspect" \
  /absolute/path/to/your-own-game.iso
```

Exit 0 means the source matches upstream's supported identity, 1 means it is
rejected with a reason, and 2 means usage/inspection failed. This utility does
not extract, patch, or overwrite the dump. The upstream checks require the GTA IV
USA retail 1.00 source (media ID `6AC07221`) and the corresponding v8 update
workflow. An emulator-compatible ISO is not automatically the correct AOT input.
The user-supplied 7.3 GiB ISO has now passed this inspector with exit 0: title
545407F2, media 6AC07221, XEX/base 0.0.0.5, region 000000FF, disc 1/1, supported
USA retail 1.00. The ISO was mounted read-only and not modified. The separate
matching v8 update was subsequently supplied separately and validated below.
The existing signed Theft4 shell was installed and launched on the physical
iPad Pro 11-inch (M5). Its captured startup log (PID 11062) reports `ios-arm64`,
ABI 1, 16,384-byte host pages, and active core state; guest runtime initialization
remains 0. No repeated lifecycle suite was run, and no iPhone action was taken.
The ISO has not been transferred to an iOS app container yet. Transfer extracted
and validated runtime data into Application Support when the loader can consume
it, preserving saves and excluding game payloads from source control.

### Supplied update inspection

The subsequently supplied `TU_1A581VI_000000O000000.0000000000206` package was
read with the SDK's STFS reader, not accepted based on its folder label. The
embedded `default.xexp` is 2,580,480 bytes, targets source `0.0.0.6` to target
`0.0.8.6`, and has SHA-256
`158c18bab6d59058f388d10d0dfb9e51f803c3725888cb7f769e41b5484fd31a`.
Its required base RSA-signature digest is
`a24dfe5c651ff8538355ccc47f687533f56eea44`, which does not match the supplied
USA ISO. It was rejected with exit 1, not applied or copied to a device.

This build requires source `0.0.0.5` to target `0.0.8.5`, with patch SHA-256
`480aee5e2b42707791e7571bb8407c5bb3f6c7534f07f9beb426db4cfc648fd3`, as pinned
in `gta4_installer.cpp::ValidatePatch`. The matching update has now been
supplied and validated below. Do not bypass the signature/hash checks.
`Destination.txt` contains only `Partition 3\\Cache`, an Xbox placement note;
it was treated as file content, not instructions to modify the host/device.

The inspector now accepts an optional second path for the update package:
`theft4_inspect <base-ISO> <title-update-package>`. It compares version, title,
module flags, the pinned patch hash, and the actual base signature without
extracting or altering either source. The full installer remains authoritative
for installation and final payload verification.

### Matching update confirmed (2026-09-14)

A matching user-supplied update package passed the simulator inspector against
the original ISO with exit 0:

- Package title: `545407F2`.
- Embedded patch length: 2,582,528 bytes.
- Patch SHA-256: `480aee5e2b42707791e7571bb8407c5bb3f6c7534f07f9beb426db4cfc648fd3` (pinned match).
- Delta source `0.0.0.5`, target `0.0.8.5`.
- Required base signature SHA-1: `192b3f567c59360c6ce211820d776f6b25251a89` (actual base match).
- Inspector result: `Update compatibility: SUPPORTED`.

This specific patch applies directly to the supplied retail base; intermediate
updates 4, 5, and 6 are unnecessary. The inspection was read-only, not an
installation or game-boot test. No original files were modified and no game
payload was transferred to the iPad. Next, use the upstream validated staging
workflow and connect the real runtime, with safe iOS guest-memory reservation
before attempting guest execution.

### Real game staging and loader bring-up (2026-09-14)

`theft4_stage` now calls the upstream installer and final pair verification,
requiring a fresh destination directory. The supported ISO and matching TU8
were staged at `out/game-staging/usa-tu8-ready/game` (about 6.6 GiB, 1,647 files).
Original inputs were not changed. Game outputs remain ignored by Git.

The actual installer exposed a disc/archive casing conflict: disc directory
`xbox360/data/maps/interiors/Generic` versus archive directory `generic`.
`gta4_rpf_extractor.cpp` now merges existing children by case-insensitive name,
preserves their actual spelling, and rejects ambiguous names or symlinks. The
full real installation then completed with `Staging VERIFIED` and exit 0.
An earlier failed copy is retained only for diagnostics under
`out/game-staging/usa-tu8-20260914-d/.failed-staging`; the installer's normal
failure-cleanup behavior was restored after that investigation.

iOS memory initialization now shares the Darwin reserve-before-MAP_FIXED
path, avoiding fixed-address guesses over existing mappings. CPU memory
translation and its codegen template now include iOS in the 0xE physical-heap
4 KiB offset compensation. The first physical-device attempt failed creating
the named shared-memory backing (`Memory::Initialize`, assertion at line 148).
The iOS backend now uses a private sandbox temporary backing file, immediately
unlinked, with the same shared aliases and no executable-memory permissions.

The signed iPad app then successfully initialized guest memory, loaded the
retail XEX, applied TU8 using `XexModule::ApplyPatch`, and verified version
`0.0.8.5` and the loaded PE signature. Console evidence from the physical M5:

```text
Theft4 loader: Xbox guest memory initialized; loading executable
Theft4 loader: Xbox executable loaded; applying TU8 in memory
XEX patch applied successfully: base version: 0.0.0.5, new version: 0.0.8.5
Theft4 loader: TU8 executable loaded into Xbox memory; kernel and graphics integration still required
```

The same loader returned exit 0 in the simulator. This proves XEX data loading
and in-memory patching, **not execution of the game's AOT functions**.

The next `Runtime::Setup(tool_mode=true)` link initially failed on the existing
`xeKeKfAcquireSpinLock` and `xeKeKfReleaseSpinLock` helpers. Their implementations
and IRQL helpers were moved without semantic changes from the export-heavy
`xboxkrnl_threading.cpp` into `src/system/guest_spinlock.cpp`. The shared system
runtime now links them directly; desktop exports use the same definitions.
`theft4_start_runtime` then ran with exit 0 in the simulator, creating the real
KernelState, kernel host-task thread, dispatcher, memory, and game/update VFS
mounts, and shutting them down. It does not load full HLE export modules or GPU.
The app's loader now follows this same Runtime-owned preparation path, keeping
user data, saves, marketplace and caches separate from read-only game assets.

The first bulk iPad transfer failed opening `queens_w.xpl` after copying only
part of the assets; a later retry timed out. Listing the app container works,
but direct file copying/retrieval has been unreliable. Do not treat the current
private `Application Support/Theft4/game` directory as a complete installation.

### Manual Finder transfer

Theft4 enables `UIFileSharingEnabled` and `LSSupportsOpeningDocumentsInPlace`.
After installing this build, use Finder → the connected iPad → Files → Theft4
and copy the entire prepared folder **named `game`** from
`out/game-staging/usa-tu8-ready/game`. The intended result is `Documents/game`
inside the app (also visible in Files → On My iPad → Theft4 → game).
Do not transfer the ISO instead, and do not flatten the directory structure.

Wait for copying to finish, then tap **Prepare transferred game**. This button
uses the public Documents/game installation, not the incomplete private copy;
it validates the executable/patch and attempts runtime/XEX preparation. It is
not yet a full asset-integrity check or a gameplay button. Saves/settings stay
in private Application Support. Game folders are excluded from backup when
preparation starts. The `--theft4-prepare-game` development launch argument
continues to target the private installation used for earlier device work.

The Finder-enabled build was installed on the M5 iPad with both sharing flags
verified in its built Info.plist. Its **Runtime-owned** loader also completed
on physical hardware: KernelState's host worker started, game/update VFS mounts
initialized, TU8 patched successfully, and the worker reported clean completion
after Runtime teardown. Captured console evidence is
`out/build/ios-device-debug/runtime-ipad-console.log`, ending with:

```text
Theft4 loader: Runtime initialized and TU8 loaded successfully; stopped before game execution
```

The console capture uses a bounded timeout because the UIKit app remains open;
a capture timeout is not an engine failure when the above completion is present.
The manual Documents/game path still needs the user's complete transfer and
button invocation; it must not be inferred from the private-path success.
