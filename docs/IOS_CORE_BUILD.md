# Theft4: M1 iOS core build

Status: implemented and verified on 2026-09-14, following the reconnaissance at
`36b729dcc166910c88a2960164707ba9d9e5138c`.

The new `ios-device-debug` and `ios-simulator-debug` presets build a static
ReXGlue core and an unsigned link/loader probe. No game files are required.
The simulator probe executes actual core functions. This is the M1 foundation
for Theft4; it does not initialize `rex::Runtime`, execute generated GTA IV
functions, or provide a UIKit application lifecycle.

## Build

Requirements: full Xcode, iPhoneOS and iPhoneSimulator SDKs, CMake 3.29+,
Python 3.10+, and the initialized repository dependencies. Use the same
public dependency setup as the upstream build:

```sh
python3 tools/setup_repo.py
python3 tools/setup_repo.py --check
```

Select full Xcode for every command. In the audit environment, CMake and Ninja
are installed outside the default shell path; add their installation directory
to PATH if they are not already discoverable. The root setup check also invokes
CMake by name, so invoking only its absolute path is insufficient.

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export PATH=/path/to/cmake-and-ninja/bin:$PATH
cmake --preset ios-device-debug
cmake --build --preset ios-device-debug --parallel 4
cmake --preset ios-simulator-debug
cmake --build --preset ios-simulator-debug --parallel 4
```

Run the configure command again after changing CMake files; do not assume the
Xcode build regenerated them. `--fresh` may be added to configure to discard
only that build directory's CMake cache.

The existing `ios-debug`/`ios-release` presets still select the legacy full
application. They are not made functional by M1. The new presets opt into
`LIBERTY_RECOMP_IOS_CORE_ONLY`, bypassing the embedded payload and application
dependency graph before either is entered.

## Artifacts and checks

Device core archive:

```text
glue/rexglue-sdk-main/out/ios-arm64/Debug/librexcored.a
```

Simulator core archive:

```text
glue/rexglue-sdk-main/out/ios-simulator-arm64/Debug/librexcored.a
```

CMake's iOS generator wraps the probe executable in a minimal `.app` directory:

```text
out/build/ios-device-debug/ios-smoke/Debug/rexcore_link_smoke.app/rexcore_link_smoke
out/build/ios-simulator-debug/ios-smoke/Debug/rexcore_link_smoke.app/rexcore_link_smoke
```

That wrapper is a link-test artifact with a synchronous `main`, not the Theft4
UIKit shell. Signing is disabled for M1. Do not install it as a normal device
application; M2 adds the scene lifecycle and device signing configuration.

The probe uses `-force_load` on the core archive. This forces every core object
through the final linker, including the selected iOS platform implementations.
An archive-only success therefore cannot hide undefined platform symbols.
Compile-time checks require iOS, Darwin/POSIX, arm64, the correct simulator
flag, and `REX_PLATFORM_MAC == 0`. Configure also rejects unwanted runtime,
renderer, SDL, native-dialog, media, profiling, and multiplayer targets.

```sh
python3 tests/ios/verify_core_artifact.py \
  out/build/ios-device-debug/ios-smoke/Debug/rexcore_link_smoke.app/rexcore_link_smoke \
  --platform device
python3 tests/ios/verify_core_artifact.py \
  out/build/ios-simulator-debug/ios-smoke/Debug/rexcore_link_smoke.app/rexcore_link_smoke \
  --platform simulator
```

The verifier checks arm64, Mach-O platform identity, the dynamic-library
closure, and absence of dynamic-code loader imports. Both binaries link only
Foundation, libSystem, libc++, and libobjc. The device uses Mach-O platform 2;
the simulator uses platform 7. Both were built with SDK 27.0 and deployment
target 16.0.

To execute the probe in an already booted simulator, obtain its UUID with
`xcrun simctl list devices booted` and pass it explicitly:

```sh
xcrun simctl spawn SIMULATOR_UUID \
  /absolute/path/to/LibertyRecomp/out/build/ios-simulator-debug/ios-smoke/Debug/rexcore_link_smoke.app/rexcore_link_smoke
```

The verified iOS 27 simulator run returned exit status 0 and reported a 16,384
byte host page. This validates loading and calls to the linked iOS core; it
does not validate the 4.5 GiB guest layout or device behavior.

## Changes and boundaries

- Root CMake enters an opt-in core build before the game payload check.
- The SDK supports `REXGLUE_CORE_ONLY`, emits a static `rexcore`, and excludes
  runtime, UI, media, GPU, code-generation, and CLI subdirectories.
- iOS detection precedes macOS; shared Darwin and POSIX guards describe common
  APIs without pretending iOS is macOS. Device/simulator output directories
  are distinct even though both use arm64.
- Core CMake selects the existing iOS memory, filesystem, logging, fiber, and
  dynamic-library files. Darwin signal/thread/file-mapping implementations are
  reused where their public SDK interfaces compile.
- iOS dynamic-library loading is explicitly unsupported; optional services
  must be linked statically. The stale path whitelist and incorrect API
  signature were removed.
- The fiber header now stores a typed `jmp_buf` and the fields its iOS backend
  needs. Unimplemented x86 and unvalidated arm64e builds fail explicitly.
  Ordinary arm64 remains supported on hardware that also supports arm64e.
- iOS filesystem `OpenExisting` matches the current API, handles combined
  read/write access correctly, and resolves user data under Application
  Support. Foundation is sufficient; the unused UIKit import was removed.

The shared Darwin fiber/thread/fault behavior is not certified by a successful
link. M3 must test it before enabling guest execution. In particular, Darwin's
unnamed `sem_init` usage, async thread suspension, page-aligned protections,
signal recovery, and stack switching still require investigation. The existing
FPCR operand-width and narrowing warnings are recorded rather than treated as
proof of PPC floating-point correctness.

The macOS `rexcore` regression target also rebuilt successfully. This does not
change the earlier full macOS application's DXC blocker. Repository dependency
patch verification and `git diff --check` pass.

## M2 follow-up and next milestone

M2 is now implemented: a scene-based Theft4 shell calls the small versioned C
interface, with launch/lifecycle checks on simulator and the physical iPhone.
See [IOS_APP_BUILD.md](IOS_APP_BUILD.md) for targets, signing, and evidence.
The core-only configuration also generates these optional shell/test targets;
the original M1 build presets continue to build only the core link probe.
M3 now owns guest memory, faults, fibers,
threading, and headless runtime initialization. M6 owns generated game-code
execution. No title code or renderer has been activated by M1.
