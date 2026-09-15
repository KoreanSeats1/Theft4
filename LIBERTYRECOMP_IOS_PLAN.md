# Theft4 / LibertyRecomp iOS Implementation Plan

**Planning baseline:** LibertyRecomp `36b729dcc166910c88a2960164707ba9d9e5138c`, audited 2026-09-14
**Companion report:** `LIBERTYRECOMP_IOS_ARCHITECTURE.md`
**Implementation posture:** minimal, upstream-compatible, experiment-driven; no leaked/proprietary source or unlicensed game files.

**Validated renderer update (2026-09-14):** device bring-up has advanced beyond
the original plan. The signed Theft4 app now runs the GTA IV AOT code and real
PM4 stream on the M5 iPad. LibertyRecomp's Xenos shader translator produces
SPIR-V on-device, and MoltenVK accepts real translated GTA IV shader modules.
The production `VulkanInstance`/`VulkanDevice` code passes its GPU-emulation
requirements on the Apple M5 GPU. The full `VulkanCommandProcessor` and cache
closure now also compile for iPhoneOS ARM64 and run on the device. A bounded
launch created real GTA IV pipelines, submitted 871 draws on the first frame,
and remained stable through 1,024 swaps. Therefore the renderer decision is now the
existing LibertyRecomp Vulkan backend over statically linked MoltenVK; XeniOS
remains a separate reference implementation. On 2026-09-15, a bounded readback
from swap 4778 visually verified Niko in the opening 3D cutscene at 1280x720.
This came from Theft4's signed AOT ARM64 game path on the physical M5 iPad, not
from XeniOS or offline replay. M4-M7 are therefore integrated far enough to
produce real game 3D, though the long near-black transition, audio, controls,
lifecycle, performance, and meaningful playability gates remain open.

## Plan decisions

**Current execution constraint (2026-09-14):** the user has now authorized
builds and launches on their physical M5 iPad Pro, alongside simulator work.
This supersedes the prior simulator-only instruction for that iPad, not the
iPhone. The user-supplied Xbox 360 ISO passed upstream's read-only compatibility
inspection (USA retail 1.00, title 545407F2, media 6AC07221). The matching v8
title-update patch has now also passed read-only compatibility inspection.
Do not alter the original ISO.

The first supplied TU8 package was inspected and rejected: it targets
`0.0.0.6 → 0.0.8.6`, not this build's `0.0.0.5 → 0.0.8.5`, and its required
base signature differs from the user's ISO. The subsequently supplied
`6AC07221/TU_1A581VI_000000K000000.0000000000205` passed with exit 0: the
pinned patch SHA-256 and actual base signature match, and the delta applies
directly from `0.0.0.5` to `0.0.8.5`. Intermediate updates 4/5/6 are not needed
for this pair. The original sources remain untouched; the extracted/prepared
installation has subsequently been transferred to the iPad.

**Loader progress (2026-09-14):** the full game installation is validated on the
Mac at `out/game-staging/usa-tu8-ready/game`. The physical M5 iPad has successfully
initialized Xbox memory, loaded the retail executable and applied TU8 in memory.
Both the simulator and physical M5 iPad additionally passed `Runtime::Setup(tool_mode=true)`, including
KernelState, its host worker, dispatcher and VFS, followed by clean teardown.
The app preserves that Runtime-owned preparation path. The Finder-transferred
`Documents/game` inventory now matches all 1,689 staged files exactly.

**Execution progress (2026-09-14):** the opt-in headless kernel and complete AOT
archive now link into the signed Theft4 app. Actual game startup on the M5 iPad
reaches `VdSetGraphicsInterruptCallback`, where a diagnostic guard stops because
no graphics backend is connected. An Apple generated-body/import-symbol
collision discovered in the first run was fixed and the corrected app rerun;
its heap/error-state stub warnings are gone. This is partial M6/early startup
evidence, not completion of M4/M5, all M3 stress gates, or meaningful gameplay.
No CPU JIT or debugger attachment was needed for execution. See
[IOS_GAME_STARTUP.md](docs/IOS_GAME_STARTUP.md) for commands, logs and limitations.

**Current next step:** characterize and shorten/fix the roughly 1,800-swap
near-black interval between the first depth-heavy world frame and the recovered
3D color pass, then verify continuous cutscene motion and controller-driven
progress without diagnostics. Preserve the AOT CPU path and the in-tree
Vulkan/MoltenVK renderer; XeniOS remains a separate behavioral oracle.

**Boot-priority update:** the user now prioritizes reaching actual game execution
over completing the originally proposed exhaustive test gates. Compile the full
checked-in AOT corpus, build the real system/kernel dependencies, inspect the
user-supplied Xbox 360 ISO read-only, and attempt startup incrementally. Retain
only checks needed to diagnose the current boot blocker; defer repeated stress,
performance, and broad regression suites. This does not turn a source archive
or a core probe into evidence that the game has booted.

1. **Theft4 is the one shipped iOS application.** UIKit/UIScene, import UI, files, settings, permissions, lifecycle, and presentation belong to Theft4. The macOS build remains a separate upstream reference/development target and host-tool environment, not a companion process or a combined universal app.
2. **LibertyRecomp is an in-process engine.** Build it as static libraries or a static XCFramework; expose an opaque, versioned C ABI implemented in Objective-C++.
3. **The CPU path remains AOT.** Compile the checked-in generated C++ into the signed app. Do not add JIT, `MAP_JIT`, downloaded native code, or runtime compilation of PPC to ARM64.
4. **Use the minimal Theft4 UIKit host for bring-up.** It already runs the real AOT/runtime path on-device and avoids inheriting either desktop application's lifecycle assumptions.
5. **Use LibertyRecomp Vulkan over static MoltenVK for M4–M10.** Reuse the existing Xenos command processor, caches, and SPIR-V translator. Keep XeniOS separate as a comparison oracle and retain the direct-Metal shell only for UIKit presentation/lifecycle work.
6. **Prove the risky runtime assumptions early.** A physical-device memory/fault/fiber/thread gate in M3 precedes game loading and renderer investment.
7. **Optional desktop services stay off initially.** Native File Dialog, auto-update, community multiplayer, voice, GameNetworkingSockets, CURL, Discord, MetalFX, and dynamic plugins return only in milestones that own them.

## Dependency and gate map

```text
M0 baseline
  |
M1 clean iOS build graph ---- G1: no desktop contamination
  |
M2 Theft4 shell + C ABI
  |
M3 headless runtime --------- G2: device VA/fault/fiber/thread feasibility
  |
M4 Metal device ------------ G3a: Plume capability gate
  |
M5 clear frame ------------- G3b: lifecycle-safe presentation
  |
M6 generated AOT executes -- G4: signed PPCFunc dispatch, no JIT
  |
M7 lawful assets/VFS ------- G5: sandboxed game data loads
  |\
  | +-- M8 input
  +---- M9 audio
           \ /
           M10 meaningful state -- G6: device resource feasibility
             |
           M11 frontend integration
             |
           M12 performance/stability/distribution -- G7
```

M8 and M9 can proceed in parallel after M7. Every gate produces a written result and a small reproducible test. A failed gate is investigated at its abstraction layer; it is not bypassed by broad patches.

## Cross-cutting engineering rules

- Keep desktop behavior unchanged behind explicit `REX_PLATFORM_IOS`/`CMAKE_SYSTEM_NAME STREQUAL "iOS"` conditions.
- Never use broad `if(APPLE)` where macOS and iOS frameworks or bundle layouts differ.
- Never hand-edit the 84 generated game chunks. Fix a generator template/config or add a narrow runtime hook.
- Build generators/translators for the macOS host and libraries for the iOS target in separate graphs.
- Device and simulator are different platforms even when both use arm64. Never merge their binaries by architecture alone; package with XCFrameworks when needed.
- Add one feature/dependency at a time and require a measurable verification artifact.
- Treat user game sources as immutable. Import by transactional copy, validate, and keep code/resources out of Git.
- Use structured logging and deterministic trace checkpoints from M2 onward.
- Assert in CI/build tests that iOS artifacts contain no AppKit linkage, raw macOS dylib staging, `MAP_JIT`, or anonymous executable-memory path.
- Preserve setup-applied dependency patches. If a change belongs upstream in ReXGlue, Plume, SDL, or MoltenVK, isolate it as a reviewable patch with a test.

## M0 — Reproduce the current upstream build

**Difficulty:** 2/5
**Status after reconnaissance:** baseline captured; no runnable app produced on this host.

### Objective

Establish a repeatable, immutable baseline at the audited commit. Distinguish checkout/bootstrap, host prerequisite, host-tool, and iOS architecture failures before any port change.

### Files involved

- `README.md`
- `docs/BUILDING.md`
- `docs/PLATFORM_SETUP.md`
- `.gitmodules`
- `tools/setup_repo.py`
- `cmake/DependencyPatches.cmake`
- `cmake/dependency-patches/`
- `CMakeLists.txt`
- `CMakePresets.json`
- `glue/rexglue-sdk-main/src/graphics/CMakeLists.txt`

These remain read-only during the baseline.

### Dependencies

- Git and every recursive public submodule.
- Python 3.10+, CMake 3.29+, Ninja, C++23 compiler.
- Full Xcode for iOS SDKs.
- The documented macOS host dependencies: current/Homebrew LLVM, OpenSSL 3, CURL, Vulkan loader or SDK, pkg-config.
- Host `spirv-val` and a non-crashing DXC for current shader overrides.

### Implementation strategy

1. Pin and record repository and submodule revisions.
2. Run `python3 tools/setup_repo.py`, then offline `--check`.
3. Record OS, active developer directory, Xcode/SDK, compiler, CMake, Ninja, Python, and dependency versions.
4. Configure/build the documented macOS release target without source changes.
5. Configure the documented iOS release target without source changes.
6. Save the first error, exact failing command, complete relevant log, and category.
7. Use diagnostic dependency substitutions only after the baseline and label them nonproduction.

### Likely failure modes

- Incomplete nested submodules or un-applied reviewed patches.
- Missing Homebrew LLVM/OpenSSL/Vulkan components.
- No full Xcode selected.
- Missing host SPIR-V/DXC tooling.
- Build-generated sources overwhelming memory with excessive parallelism.
- iOS configure blocked by required local game payload or desktop dependency lookup.

### Verification criteria

- `setup_repo.py --check` succeeds.
- Toolchain/dependency inventory is written.
- macOS either builds or has one reproducible first supported-environment failure.
- iOS has an unmodified configure trace and independently categorized follow-up probes.
- Git status distinguishes project-managed dependency patches from audit output and user work.

### Reconnaissance result

- Setup passed.
- macOS initially failed at missing OpenSSL 3. A diagnostic configuration later compiled through 845/1109 and ultimately reproduced a bundled DXC exit 139 on `ps_bink.hlsl` after supplying `spirv-val`.
- iOS first failed on absent `tools/local_game_payload`, then Freetype; a configuration-only probe exposed Native File Dialog choosing macOS and the custom MoltenVK wrapper requiring AppKit, followed by GNS/OpenSSL.
- No runnable app was produced. Full evidence is in the companion report.

## M1 — Generate a clean iOS ARM64 target

**Implementation status (2026-09-14):** the minimal static core and forced-link
probe build for device and simulator; the simulator probe executes successfully.
See [build instructions and remaining runtime boundaries](docs/IOS_CORE_BUILD.md).
Full `rexruntime`/UIKit integration continues in M2–M3.

**Difficulty:** 5/5
**Gate G1:** both device and simulator graphs configure and compile without macOS-only APIs, binaries, or runtime plugins.

### Objective

Produce a minimal, linkable iOS static engine/core and test target for `iphoneos-arm64`, plus an `iphonesimulator-arm64` build for UI/compile smoke tests. This milestone does not load game files or initialize the full runtime.

### Files involved

- `CMakeLists.txt`
- `CMakePresets.json`
- `toolchains/ios.cmake`
- `glue/CMakeLists.txt`
- `glue/rexglue-sdk-main/CMakeLists.txt`
- `glue/rexglue-sdk-main/include/rex/platform.h`
- `glue/rexglue-sdk-main/src/core/CMakeLists.txt`
- `glue/rexglue-sdk-main/src/system/CMakeLists.txt`
- `glue/rexglue-sdk-main/src/input/CMakeLists.txt`
- `glue/rexglue-sdk-main/src/audio/CMakeLists.txt`
- `glue/rexglue-sdk-main/src/ui/CMakeLists.txt`
- `thirdparty/CMakeLists.txt`
- `thirdparty/plume/CMakeLists.txt`
- `LibertyRecompLib/CMakeLists.txt`
- `LibertyRecomp/CMakeLists.txt`
- `LibertyRecomp/main.cpp`
- Existing iOS core sources under `glue/rexglue-sdk-main/src/core/`
- Proposed `cmake/CheckIOSToolchain.cmake`
- Proposed `tests/ios/rexcore_link_smoke.cpp`

### Dependencies

- M0.
- Full Xcode and iOS/iPhoneSimulator SDKs.
- Vendored SDL3, fmt, spdlog, SIMDe, xxHash and other core static dependencies.
- macOS-hosted code/shader tools when generation is required.
- No game files for the smoke target.

### Implementation strategy

1. Add iOS-first platform detection before broad `TARGET_OS_MAC`; define `REX_PLATFORM_IOS`, POSIX/Darwin, and `ios-arm64` coherently.
2. Add an explicit iOS source-selection branch and compile only current-interface platform files. Use dormant iOS files as references, updating signatures rather than forcing them into the build unchanged.
3. Keep the current iOS legacy consumer as the initial app lineage, but factor a static core target that does not depend on the app bundle or payload.
4. Make `rexruntime` static for iOS and disable dynamic GPU discovery. Desktop targets retain their current shared/plugin model.
5. Exclude NFD, macOS FFmpeg prebuilts, GNS, CURL/update UI, Discord, Tracy, Game Center, voice, installer wizard, MetalFX, and MoltenVK from the minimal target.
6. Add iOS to the Freetype source/pre-generated-font policy; do not satisfy it with dummy targets.
7. Create separate device and simulator presets/toolchain settings.
8. Prevent target CMake from building/running `rexglue` or XenosRecomp as iOS executables. Add imported host-tool paths where needed.
9. Add a target that links core code and returns a version/platform string.
10. Inspect the linked artifact with `file`, `lipo`, `otool -L`, deployment metadata, and symbol checks.

### Likely failure modes

- iOS still falls through a macOS `APPLE` branch.
- Stale `dynlib_ios`, `fiber_ios`, logging, or filesystem signatures.
- An object/shared library silently pulls AppKit/IOKit or a macOS-only binary.
- Host tools are cross-compiled and then cannot execute during the build.
- Objective-C++ ARC/non-ARC mismatch.
- SDL builds two entry points or dynamic forms unexpectedly.
- CMake's multi-config Xcode generator exposes assumptions based only on `CMAKE_BUILD_TYPE`.
- Huge generated targets are accidentally included in the smoke archive.

### Verification criteria

- Clean `ios-device-debug` and `ios-simulator-debug` configure directories generate successfully without a payload.
- The core smoke target compiles and links for both SDKs.
- Device output is arm64 iPhoneOS; simulator output is explicitly a simulator platform.
- `otool -L` shows only permitted iOS system frameworks/libraries and no AppKit, IOKit-as-macOS-assumption, Homebrew path, raw `libMoltenVK.dylib`, or unsigned plugin closure.
- A build-time test verifies `REX_PLATFORM_IOS == 1`, `REX_PLATFORM_MAC == 0`, and expected page/platform macros.
- Desktop configure behavior is unchanged.

## M2 — Minimal iOS executable starts

**Bring-up status (2026-09-14):** implemented and verified on simulator and
physical iPhone, including twenty actual background/foreground cycles and
three core teardown/recreation cycles. See [app build evidence](docs/IOS_APP_BUILD.md).
The minimal host uses Objective-C UIKit and a plain C++ implementation of the C
interface; the proposed Swift/Objective-C++ paths below remain the original
design sketch. Bounded synchronous probe operations stay on the main thread;
M3 must add an owned worker before introducing blocking runtime work. Natural
scene-disconnect, OS memory pressure, and Instruments leak testing remain open.

**Difficulty:** 3/5

### Objective

Launch a signed Theft4/UIKit application on simulator and physical device, call a statically linked LibertyRecomp symbol through the proposed C ABI, and survive lifecycle transitions without initializing guest memory or graphics.

### Files involved

- Proposed `ios/Theft4/AppDelegate.swift`
- Proposed `ios/Theft4/SceneDelegate.swift`
- Proposed `ios/Theft4/EngineViewController.swift`
- Proposed `ios/Theft4/Info.plist`
- Proposed `ios/Theft4/Theft4.entitlements`
- Proposed `ios/Theft4/Assets.xcassets/`
- Proposed `LibertyRecomp/include/theft4_engine.h`
- Proposed `LibertyRecomp/os/ios/theft4_engine.mm`
- `LibertyRecomp/CMakeLists.txt`
- `LibertyRecomp/res/ios/` as migration/reference material
- Vendored SDL3 main documentation/integration only if the standalone CMake bundle is retained as a separate smoke harness.

### Dependencies

- M1 static core.
- Xcode signing team/profile for device; simulator needs no device provisioning.
- A module map/bridging header that exposes C, not C++.

### Implementation strategy

1. Create a scene-based UIKit host; do not run a desktop blocking `main` on the UI thread.
2. Define an opaque engine handle and `struct_size`/API-versioned configuration.
3. Implement only `api_version`, `create`, log callback, platform description, `stop`, and `destroy`.
4. Run engine work on one owned serial/worker context; marshal UI callbacks to the main queue.
5. Implement explicit state validation and idempotent teardown.
6. Add unified log categories and a visible debug status in the app.
7. Exercise active/inactive/background/foreground/scene-disconnect transitions.

### Likely failure modes

- Duplicate `main`/SDL application delegate.
- Swift importing C++ ABI or ownership-sensitive types.
- Callback after engine destruction.
- UI calls from engine threads.
- Scene manifest/signing/bundle-ID errors.
- Engine singleton assumptions preventing repeated creation.

### Verification criteria

- Cold launch on simulator and a physical iPhone.
- UI displays the exact engine/API/platform version obtained through C.
- Logs appear in Xcode/Console with stable categories.
- Twenty foreground/background cycles and three create/destroy cycles complete without crash, hang, late callback, or leaked engine instance.
- The app contains no game data and makes no network/permission request.

## M3 — LibertyRecomp runtime initializes headlessly

**Difficulty:** 5/5
**Gate G2:** the real device supports the runtime's address-space, protection, fault, fiber, synchronization, and shutdown model without executable guest memory.

### Objective

Initialize and shut down the minimum ReXGlue runtime off the UIKit thread, with no renderer or game module, and prove every dangerous platform primitive in a dedicated physical-device harness.

### Files involved

- `glue/rexglue-sdk-main/include/rex/runtime.h`
- `glue/rexglue-sdk-main/include/rex/thread/fiber.h`
- `glue/rexglue-sdk-main/include/rex/system/xmemory.h`
- `glue/rexglue-sdk-main/src/system/{runtime.cpp,xmemory.cpp,mmio_handler.cpp,thread.cpp,thread_state.cpp,xthread.cpp}`
- `glue/rexglue-sdk-main/src/core/{memory_ios.cpp,fiber_ios.cpp,filesystem_ios.mm,logging_ios.mm,dynlib_ios.cpp,net_ios.cpp}`
- `glue/rexglue-sdk-main/src/core/{exception_handler_mac.cpp,exception_handler_posix.cpp,threading_mac.cpp,threading_posix.cpp,mapped_memory_mac.cpp}` or new iOS-specific counterparts
- Engine C ABI and lifecycle bridge from M2
- Proposed `tests/ios/ios_memory_probe.cpp`
- Proposed `tests/ios/ios_fiber_thread_probe.cpp`
- Proposed `tests/ios/ios_fault_probe.cpp`

### Dependencies

- M2.
- Physical arm64 iPhone/iPad; simulator results are non-authoritative.
- Debug symbols and a way to collect crash/device logs.

### Implementation strategy

1. Wire the existing `Runtime` constructor roots—game, user, update, cache, metadata, marketplace, and saves—and the existing `RuntimeConfig::tool_mode`/headless `Setup` path through the C façade. Add a separate log-root parameter only if the logging backend cannot already be redirected by the host.
2. Query actual host page size and allocation granularity.
3. Reproduce `xmemory.cpp`'s full `map_info`: roughly 4.5 GiB contiguous VA and every shared alias, with exact file offsets.
4. Verify cross-alias reads/writes, reserve/commit/decommit/protect, page-boundary behavior, and the raw `0xE...` `0x1000` compensation.
5. Assert no guest range has `PROT_EXEC` and no `MAP_JIT` call exists in the iOS binary path.
6. Trigger a controlled protected/MMIO access and prove handler recovery without corrupting the host stack or crash reporter.
7. Update the iOS fiber implementation to the current class layout. Test callee-saved GPR, FP/SIMD, stack alignment, TLS, nested switches, and arm64e builds where available.
8. Exercise events, semaphores, timers, APC/suspend/resume policy, and cooperative shutdown under lifecycle interruptions.
9. Measure virtual allocation, resident memory before/after page touches, 512 MiB physical precommit cost, and memory-warning behavior.
10. Run initialization and teardown repeatedly, including failure injection at every stage.

### Likely failure modes

- `shm_open`, file size, or `MAP_FIXED` behavior differs from the dormant implementation's assumptions.
- A contiguous 4.5 GiB reservation collides or fails.
- The 512 MiB physical commit creates unacceptable resident pressure/jetsam.
- Guest 4 KiB protections corrupt adjacent data on 16 KiB host pages.
- Signal delivery/context editing is not safe or conflicts with crash handling.
- `ucontext` is unavailable/incomplete; `setjmp` stack pivot loses SIMD/PAC/unwind state.
- Host forced cancellation or priority APIs fail under iOS.
- Shutdown races leave a guest thread touching unmapped memory.

### Verification criteria

- A structured probe report records device model/OS/page size and every mapping/address/offset.
- Every alias test and protection transition passes over 100 init/teardown cycles.
- Controlled faults resume at the intended instruction and uncontrolled faults still crash/symbolicate normally.
- Fiber state fixtures pass over at least one million switches.
- Thread/event/semaphore/timer tests pass while repeatedly backgrounding the app.
- VM inspection shows signed Mach-O code only; no JIT entitlement, `MAP_JIT`, or writable-executable region.
- Written resident-memory/physical-commit budget is acceptable for the intended device floor.

### Stop/re-scope rule

If exact aliases or fault recovery are impossible or the physical commit causes unavoidable jetsam, stop renderer work. Estimate a segmented/translated memory model, lazy physical backing, or explicit MMIO rewrite before continuing.

## M4 — Rendering device initializes

**Current status (2026-09-15): achieved on physical M5 iPad.** The implemented
route differs from the older Plume proposal below: Theft4 now embeds the
production LibertyRecomp Vulkan device and command processor, with MoltenVK as
the thin Vulkan-to-Metal portability layer. XeniOS's public MoltenVK build is
used as a dependency/reference, but no emulator code is linked into Theft4.

**Difficulty:** 4/5
**Gate G3a:** Plume creates a valid iOS Metal device/queue/swapchain and exposes the capabilities/formats needed by the legacy GTA IV renderer.

### Objective

Initialize Plume's native Metal backend against a host-owned `CAMetalLayer`, without executing generated game code.

### Files involved

- `thirdparty/plume/CMakeLists.txt`
- `thirdparty/plume/{plume_apple.h,plume_apple.mm}`
- Proposed `thirdparty/plume/{plume_ios.h,plume_ios.mm}`
- `thirdparty/plume/{plume_metal.h,plume_metal.cpp,plume_render_interface_types.h}`
- `LibertyRecomp/ui/{game_window.h,game_window.cpp}`
- `LibertyRecomp/gpu/{video.h,video.cpp}`
- `LibertyRecomp/CMakeLists.txt`
- Engine layer/lifecycle C ABI
- Theft4 Metal-backed view

### Dependencies

- M3.
- Metal, QuartzCore, Foundation, UIKit, CoreGraphics, IOSurface as actually required.
- Physical Apple GPU for acceptance.

### Implementation strategy

1. Split Plume Apple helpers by target OS; leave macOS `NSWindow`/IOKit code unchanged.
2. On iOS, use the system-default Metal device, `CAMetalLayer`, injected pixel size/scale/refresh data, and UIKit screen APIs.
3. Remove display enumeration, desktop fullscreen/positioning, and Cocoa SDL property use from the iOS branch.
4. Make surface attachment/detachment explicit and main-thread-safe; GPU setup remains on the engine/render thread.
5. Disable MetalFX, HDR, high AA, shader cache loading, and optional post-processing.
6. Enumerate and log every required Plume/Metal format, sample count, heap/resource-storage mode, fence/event, and pipeline capability.
7. Add teardown/recreate behavior for scene/background/surface loss.

### Likely failure modes

- Plume CMake still compiles AppKit adapter or links desktop frameworks.
- `MTL::CopyAllDevices` or Cocoa wrappers remain in an iOS call chain.
- Layer ownership/lifetime crosses queues unsafely.
- Required depth/stencil/texture format or synchronization behavior is absent/different.
- Swapchain assumes a desktop window size/display-sync API.
- Objective-C/metal-cpp retain semantics leak or over-release device/layer objects.

### Verification criteria

- Physical device, command queue, and swapchain initialize against the exact layer supplied by Theft4.
- Capability matrix is saved and every required feature has pass/fallback/fail disposition.
- Device/surface teardown and recreation pass 20 background/foreground and rotation cycles.
- Metal validation is clean.
- No AppKit/IOKit desktop adapter or MoltenVK is linked on this path.

### Renderer convergence experiment

After G3a, time-box a separate static MoltenVK probe against the modern Graine renderer's required Vulkan extensions/formats. Record facts only. Do not port the whole modern host in this milestone. A Plume fundamental capability gap or an explicit upstream decision that the legacy consumer is obsolete can trigger a one-time architecture pivot.

## M5 — First frame / clear screen

**Current status (2026-09-15): exceeded.** A UIKit `CAMetalLayer` first passed a
direct-Metal clear test and now owns a 2816x1940 three-image Vulkan swapchain.
Real 1280x720 GTA IV guest images acquire, submit, and queue-present successfully
for hundreds/thousands of frames. The user visually confirmed the game boots.

**Difficulty:** 3/5
**Gate G3b:** lifecycle-safe drawable acquire/clear/present works continuously.

### Objective

Present a stable clear color through Plume Metal, resize correctly, and pause/recreate cleanly across UIKit lifecycle events.

### Files involved

- Plume Metal swapchain/command submission files from M4
- `LibertyRecomp/gpu/{video.cpp,video.h}`
- `LibertyRecomp/ui/{game_window.cpp,game_window.h}`
- Engine C ABI surface and lifecycle implementation
- `ios/Theft4/EngineViewController.swift`
- `LibertyRecomp/gpu/shader/msl/` only if the clear harness needs a pipeline

### Dependencies

- M4.
- A frame scheduler (`CADisplayLink` or a clearly owned engine loop) and foreground-state signal.

### Implementation strategy

1. Add the smallest Plume path: acquire drawable, encode clear, submit, present.
2. Let the host provide target frame rate and size changes; do not poll desktop displays.
3. Stop acquisition before scene resignation/background or layer detach.
4. Drain/retire in-flight work with a bounded wait, then release transient resources.
5. Recreate size-dependent resources only after a valid foreground layer is attached.
6. Collect frame interval, acquire latency, present completion, and drawable-unavailable counts.

### Likely failure modes

- `nextDrawable` is nil or blocks during transitions.
- Main-thread layer mutation races render-thread access.
- Rotation/scale creates zero or stale extents.
- Command buffer/fence deadlock on background.
- In-flight resources outlive the layer/device.
- Wrong pixel format/color space.

### Verification criteria

- Ten-minute clear loop on a physical device with stable frame pacing.
- Rotation/size changes render at the new pixel dimensions.
- Twenty background/foreground cycles and repeated surface detach/attach complete without validation errors, nil-drawable spin, leak, or deadlock.
- A screenshot plus structured frame/lifecycle log is retained as the first graphics artifact.

## M6 — Recompiled game code begins executing

**Current status (2026-09-15): achieved and exceeded.** The complete checked-in
AOT corpus is signed into the app, TU8 is applied, the game entry executes, and
the title continuously emits real PM4 command streams. There is no runtime CPU
JIT and no executable-memory generation.

**Difficulty:** 5/5
**Gate G4:** a known checked-in generated function dispatches correctly as signed ARM64 AOT code; no JIT or executable guest mapping appears.

### Objective

Compile/link the generated corpus into the iOS engine, preserve registration, and execute a deliberately selected generated `PPCFunc` or earliest module entry checkpoint without attempting a full boot.

### Files involved

- `glue/rexglue-sdk-main/gta4-recomp/generated/sources.cmake`
- `generated/gta4_init.cpp`
- `generated/gta4_init.h`
- `generated/gta4_register.cpp`
- `generated/gta4_recomp.0.cpp` … `gta4_recomp.83.cpp`
- `glue/rexglue-sdk-main/gta4-recomp/gta4_manifest.toml`
- `glue/rexglue-sdk-main/gta4-recomp/gta4_config.toml`
- `LibertyRecompLib/CMakeLists.txt`
- `glue/rexglue-sdk-main/resources/templates/codegen/init_h.inja`
- `glue/rexglue-sdk-main/src/system/{function_dispatcher.cpp,xmemory.cpp,xex_module.cpp,xthread.cpp,runtime.cpp}`
- Engine bridge and diagnostic trace callback

### Dependencies

- M3; M4/M5 if following normal app boot.
- Sufficient build time/disk/RAM for approximately 5.7 million generated C++ lines.
- A lawfully supplied matching XEX only for module-identity/load/entry tests—not for the isolated function fixture.

### Implementation strategy

1. Correct the `0xE...` physical host offset in the generator template/runtime helper and regenerate only generated glue that derives from that template.
2. Compile generated sources in bounded object groups; keep strict FP and non-strict-alias settings aligned with desktop.
3. Ensure `gta4_RegisterFunctions` and referenced function objects cannot be dead-stripped (`-force_load`, explicit root symbol, or equivalent narrow method).
4. Add a diagnostic dispatcher boundary that records guest address and host symbol without editing generated chunks.
5. Select a side-effect-bounded function with known input/output guest state for the first call.
6. Then load only enough matching XEX metadata to prove normal address registration/lookup and reach an early entry trace.
7. Inspect VM/code-signing state while the function executes.

### Likely failure modes

- Linker/object size or relocation pressure.
- Registration table/functions dead-stripped.
- ARM64 FPCR/vector/alignment difference.
- Incorrect endianness or raw physical offset.
- Function fixture has hidden runtime/TLS assumptions.
- Wrong XEX revision/layout.
- Indirect target missing from config.

### Verification criteria

- `FunctionDispatcher::GetFunction(known_address)` returns the expected host symbol.
- The function produces the expected deterministic `PPCContext`/guest-memory delta on device.
- An early normal module dispatch checkpoint is reached with a matching XEX.
- Code address lies in signed Mach-O `__TEXT`; no guest mapping is executable and no JIT entitlement exists.
- The same fixture passes on Apple Silicon macOS to distinguish title/runtime from iOS-only errors.

## M7 — Filesystem and user-owned assets load

**Current status (2026-09-15): developer-path achieved, product import UI still
pending.** The user-owned ISO/TU8 was validated and staged, then copied into the
app sandbox. `game:` and `update:` mount successfully and GTA IV boots from them.
Transactional document-picker import and installation management remain Liberty
Bridge product work. Missing `cache:`/`cache1:` probes are currently intentional
runtime behavior and are not blocking boot.

**Difficulty:** 4/5
**Gate G5:** a matching lawful installation imports transactionally and required XEX/RPF/DLC/save paths open through VFS.

### Objective

Replace configure-time payload embedding with a user-driven import, immutable game root, separate writable roots, and validated VFS/module loading.

### Files involved

- `LibertyRecomp/install/{embedded_assets.cpp,platform_paths.cpp,platform_paths.h}`
- `LibertyRecomp/install/xbox360/`
- `LibertyRecomp/user/{paths.cpp,paths.h,config.cpp}`
- `LibertyRecomp/os/ios/ios_paths_objc.mm`
- `glue/rexglue-sdk-main/src/core/filesystem_ios.mm`
- `glue/rexglue-sdk-main/src/filesystem/`
- `glue/rexglue-sdk-main/src/system/{guest_path.cpp,xfile.cpp,user_module.cpp,xex_module.cpp}`
- Existing installer/source-inspector/hash logic in both consumer trees
- Engine validation/config C ABI
- Theft4 import/installations UI and storage service

### Dependencies

- M6.
- `UIDocumentPicker`/document APIs and adequate device storage.
- Only user-supplied legally acquired files.

### Implementation strategy

1. Define an installation manifest with schema/version, source identity, validated required files, optional DLC/TU, byte counts, and hashes where legally/technically appropriate.
2. Select a source with UIKit, hold security-scoped access only during inspection/copy, and never retain an unsafe raw external path as the engine root.
3. Copy into `tmp`/staging with progress and cancellation; validate before atomically moving to `Library/Application Support/Theft4/Games/<id>`.
4. Pass separate game/save/config/cache/log roots to the engine.
5. Mount game/update/DLC overlays through the existing VFS without changing current working directory.
6. Treat game files as read-only. Write saves/config/cache only to their assigned roots.
7. Remove product reliance on build-time `LIBERTY_RECOMP_EMBEDDED_GAME_PATH`; retain an explicit private developer-test option if needed.
8. Implement cleanup/resume for interrupted imports and low-storage errors.

### Likely failure modes

- Picker URL access expires during a large copy.
- Partial import is mistaken for a valid installation.
- Case/Unicode/path-separator behavior differs from Xbox/desktop assumptions.
- Source media uses an unsupported container/update/version.
- Bundle `.app/Contents/Resources` assumptions leak into iOS.
- Save/config code writes into immutable game root.
- Imported data is backed up unnecessarily or exceeds space.

### Verification criteria

- Valid installation imports, validates, and survives app relaunch.
- Cancelled, corrupt, partial, unsupported, and low-space imports fail without publishing an installation.
- VFS opens every required XEX/RPF/audio/DLC path from the immutable root.
- A save/config write lands only in the writable root and survives relaunch.
- No CMake configure/build step reads or mutates user game files.

## M8 — Input works

**Current status (2026-09-15): implementation complete, physical acceptance
pending.** Theft4 now builds Apple's existing GameController driver directly.
Xbox, PlayStation, and MFi state maps to Xbox 360 input with hot-plug and haptics;
the latest device launch initialized the backend but had no physical controller
connected. Pair a controller and verify guest navigation to close this milestone.

**Difficulty:** 3/5

### Objective

Deliver controller input to Xbox guest state, then touch controls; keep keyboard/mouse optional. Add haptics only after state input is stable.

### Files involved

- `glue/rexglue-sdk-main/src/input/CMakeLists.txt`
- `glue/rexglue-sdk-main/src/input/input_system.cpp`
- `glue/rexglue-sdk-main/src/input/sdl/`
- `glue/rexglue-sdk-main/src/input/ios/{ios_input_driver.h,ios_input_driver.mm}`
- `LibertyRecomp/hid/driver/sdl_hid.cpp`
- `LibertyRecomp/os/ios/{haptics_ios,adaptive_triggers_ios}.{h,mm}`
- GTA IV input/touch hooks/coordinator in the selected consumer
- Engine input C ABI
- Theft4 touch/controller configuration UI

### Dependencies

- M6/M7 and visible UI from M5.
- SDL3 and/or GameController/CoreHaptics.

### Implementation strategy

1. Choose one authoritative controller enumeration path for the first build—SDL3 is preferred for minimum existing HID change.
2. Normalize buttons, triggers, sticks, controller ID, timestamps, connection, and remapping into Xbox state.
3. Repair the native GameController driver only for capabilities not available through SDL and avoid duplicate devices.
4. Add a renderer-independent input queue to the C ABI.
5. Translate touch with stable IDs, phase/cancel, safe-area-aware coordinates, and user-configurable layout.
6. Cancel all active input on resign/background/disconnect.
7. Repair haptics function-name/lifetime drift, then test engine reset and unsupported devices.
8. Add optional iPad keyboard/mouse focus/capture tests later.

### Likely failure modes

- Same controller appears through SDL and GameController.
- Hotplug/ARC callback races.
- Trigger/axis ranges or deadzones differ.
- Touch coordinate scale/safe area is wrong.
- Stuck controls after background.
- Haptic engine resets or blocks gameplay thread.
- Guest polls from a different thread than event delivery.

### Verification criteria

- Automated mapping fixture passes for standard Xbox-style state.
- On-device controller navigates a deterministic test UI/game state; hotplug/reconnect works.
- Multi-touch reaches expected guest actions with no stuck state after cancel/background.
- Rumble starts/stops correctly and degrades to no-op on unsupported hardware.
- One-hour input stress run has no duplicate IDs, callback-after-free, or queue growth.

## M9 — Audio works

**Current status (2026-09-15): native output works; XMA decode remains.** The
guest mixer is paced at 256 frames / 48 kHz. Submitted blocks now flow through
the existing ARM64 Xbox six-channel big-endian-to-stereo conversion into a
16,384-frame lock-free ring consumed by a RemoteIO AudioUnit. On-device metrics
show no drops, but every guest block currently has zero peak because
`xboxkrnl_audio_xma_headless.cpp` explicitly calls `ProduceSilentBuffer`. The
next task is integrating the real XMA decoder service and its iOS FFmpeg closure,
not changing the now-working output backend.

**Difficulty:** 4/5

### Objective

Decode XMA/title audio and deliver stable foreground playback through the existing SDL audio backend with correct AVAudioSession behavior.

### Files involved

- `glue/rexglue-sdk-main/src/audio/CMakeLists.txt`
- `glue/rexglue-sdk-main/src/audio/{audio_system.cpp,xma_context.cpp,xma_decoder.cpp}`
- `glue/rexglue-sdk-main/src/audio/sdl/`
- `LibertyRecomp/apu/`
- `LibertyRecomp/os/ios/media_ios.cpp`
- Proposed `LibertyRecomp/os/ios/audio_session_ios.mm`
- `thirdparty/ffmpeg-core/` or replacement iOS FFmpeg integration
- Engine audio/lifecycle callbacks
- Theft4 permission/UI only if voice is later enabled

### Dependencies

- M6/M7.
- Static iOS-compatible libavcodec/libavutil or an isolated compatible decoder.
- AVFoundation/AudioToolbox and SDL3 audio.

### Implementation strategy

1. Cross-build only required FFmpeg components as static iOS/simulator slices; disable unsupported assembly/components and record licenses/configuration.
2. Keep SDL audio as the output backend; do not port macOS CoreAudio first.
3. Implement AVAudioSession category, activation, interruption, route-change, and media-service-reset handling in a small Objective-C++ adapter.
4. Pause/drain decoder and guest audio safely on background/interruption; re-open/re-negotiate on resume.
5. Instrument buffer depth, underruns, decode time, sample format/rate/channels, and route transitions.
6. Defer voice capture and microphone permission until playback acceptance passes.

### Likely failure modes

- macOS FFmpeg binary or unsupported assembly is accidentally linked.
- XMA sample/channel conversion differs.
- Decoder starvation under thermal/GPU load.
- Bluetooth/headphone route changes alter format/latency.
- Interruption callback deadlocks guest/audio thread.
- Audio continues in background without intended background-audio product behavior.

### Verification criteria

- Decoder fixtures produce expected frame counts and sample hashes/tolerances.
- In-game/menu audio plays for 30 minutes without sustained underruns.
- Headphone/Bluetooth route changes, phone/system interruption, background/foreground, and media-service reset recover cleanly.
- Playback-only build requests no microphone permission.
- No macOS-only binary appears in the iOS link closure.

## M10 — Reach the first meaningful game state

**Difficulty:** 5/5
**Gate G6:** repeatable, visible, interactive title progress fits the selected device floor's memory, thermal, and frame-time budgets.

### Objective

Reach a named deterministic state—preferably legal/logo/front-end menu, then the first controllable scene—with rendering, assets, input, audio, and saves active.

### Files involved

- `LibertyRecomp/main.cpp`
- `LibertyRecomp/gpu/`
- `LibertyRecomp/kernel/`
- `LibertyRecomp/runtime/`
- `LibertyRecomp/patches/`
- `LibertyRecomp/install/`
- `glue/rexglue-sdk-main/src/{system,kernel,filesystem,input,audio}/`
- Generated sources and GTA IV configuration
- `LibertyRecompLib/shader/`
- `LibertyRecompLib/shader_overrides/`
- `LibertyRecomp/gpu/shader/msl/`
- XenosRecomp AIR tooling
- Engine trace/status ABI

### Dependencies

- M5–M9.
- Matching lawful game installation and correctly regenerated iPhoneOS Metal shader corpus.
- Deterministic trace points and device performance instrumentation.

### Implementation strategy

1. Make the AIR compiler accept explicit `macosx`, `iphoneos`, and simulator target parameters; record compiler/SDK/target metadata.
2. Rebuild all helper metallibs and the 1,356-entry guest AIR cache for iPhoneOS from public tooling and lawful extracted shader inputs; never reuse macOS AIR as a fallback.
3. Validate every artifact before embedding/loading; fail with hash/name/target detail for missing shaders.
4. Define checkpoints: runtime initialized, XEX loaded, entry dispatched, graphics ready, archives mounted, legal frame, frontend state, menu input, audio event, save created, first world scene.
5. Advance one checkpoint at a time and assign each failure to CPU/runtime, memory/fault, VFS, shader/renderer, input, audio, or guest service.
6. Disable community network, voice, HDR, MetalFX, expensive AA/upscaling, and optional debug overlays.
7. Capture RSS/virtual/physical memory, shader/pipeline compile time, CPU/GPU frame time, drawable stalls, thermal state, and crash traces.
8. Test cold boots repeatedly on at least two representative device generations.

### Likely failure modes

- AIR target/compiler mismatch or unsupported Metal shader feature.
- Missing shader cache entry stalls/fails rendering.
- Plume Metal format/synchronization semantics differ from desktop.
- Guest indirect function or kernel service is missing.
- Endian/alignment/fault issue appears only in a later engine path.
- Physical memory/shader caches cause jetsam.
- Background/foreground occurs during archive/GPU initialization.
- A disabled network/title service blocks the frontend unexpectedly.

### Verification criteria

- Five consecutive cold boots reach the named meaningful state on each test device.
- Controller/touch input and audio work at that state.
- A save/config artifact is written and loads after restart.
- Metal validation and engine fatal logs are clean for the accepted route.
- Written baseline includes time-to-state, peak RSS, virtual size, physical backing, frame-time percentiles, shader misses, and thermal state.
- If the first controllable scene is the goal, it runs at least 30 minutes without crash or unrecoverable resource growth.

## M11 — Integrate the Liberty Bridge frontend as Theft4

**Difficulty:** 3/5

### Objective

Replace the bring-up UI with the production Theft4 frontend while preserving the renderer-neutral, versioned engine contract and complete lifecycle ownership.

### Files involved

- Stable public `theft4_engine.h`
- Objective-C++ engine façade implementation
- Module map/XCFramework/CMake packaging
- Theft4 screens/services for installations, import/validation, settings, graphics, controller/touch, saves, logs, crash reports, and launch state
- Engine error/event schema and state-machine tests
- Product plist, entitlements, asset catalog, privacy declarations

### Dependencies

- M10.
- A versioned C ABI, structured async events, and defined supported-device/config matrix.

### Implementation strategy

1. Freeze the lifecycle state machine and v1 ABI: create/configure, attach/detach surface, start, pause/resume, memory warning, stop/destroy, input, save flush, and diagnostics.
2. Give every struct `struct_size`/version and every operation a typed result/error domain.
3. Make long operations asynchronous with progress/cancellation and one owner queue.
4. Keep UIKit objects entirely in the frontend; bridge only opaque layer pointers during an explicit attached lifetime.
5. Implement installation selection, validation status, settings snapshots, safe save management, and diagnostics export.
6. Enforce one engine instance and explicit transition errors.
7. Make engine upgrades testable without Swift changes unless the public ABI intentionally changes.

### Likely failure modes

- Swift/engine ABI drift.
- UI waits synchronously on guest/render work.
- Callback arrives after stop/destroy.
- Surface/config changes race running engine.
- Save corruption on force quit or storage failure.
- Logs/crash exports expose private user paths or excessive data.
- Frontend starts unvalidated installation or incompatible title version.

### Verification criteria

- User can import, validate, select, start, pause, stop, and restart an installation.
- Graphics/controller settings apply at documented safe boundaries.
- Save list/export/import/rollback paths are atomic and tested.
- Logs and crash reports can be exported with privacy redaction.
- UI remains responsive during load/stop.
- ABI conformance tests cover old/new struct sizes, unknown fields, failure transitions, and callbacks after cancellation.

## M12 — Performance, stability, and distribution hardening

**Difficulty:** 5/5
**Gate G7:** supported devices meet explicit frame-time, memory, thermal, lifecycle, crash, and packaging targets.

### Objective

Turn the feasibility port into a sustainable product build: bounded memory, stable frame pacing, robust lifecycle/save behavior, static signed code, and a documented distribution/compliance posture.

### Files involved

- Plume/Metal renderer, shader/pipeline caches, GPU scheduling and resource retirement
- `glue/rexglue-sdk-main/src/system/xmemory.cpp`
- Thread/fiber/fault adapters
- Audio/input/lifecycle bridge
- Release/LTO/symbol/XCFramework CMake
- Theft4 plist, entitlements, privacy manifest, signing/archive settings
- CI, unit/integration/device test plans
- Crash symbolication and bounded diagnostics

### Dependencies

- M11.
- Instruments, Metal System Trace/capture, Organizer/crash symbols, physical device matrix, low-storage/memory/thermal test conditions.

### Implementation strategy

1. Define supported devices and budgets before tuning: peak RSS, physical guest commitment, cache sizes, target resolution/FPS, 95th/99th frame time, launch time, thermal response.
2. Profile representative scenes; optimize measured bottlenecks only.
3. Introduce lazy/bounded resource and shader caches, pipeline prewarm/persistence, and memory-warning eviction.
4. Tier resolution, AA, post-processing, texture/cache policy, and FPS by device capabilities.
5. Stress suspend/resume, audio routes, controller churn, memory warnings, thermal changes, low storage, interrupted imports, force termination, and save recovery.
6. Strip debug/desktop services and verify static code closure.
7. Audit bundle/import behavior against Apple rules: XEX is data; all executable functionality ships signed; no arbitrary plugin or code download.
8. Document third-party licenses, user-owned-file requirements, privacy usage, entitlement rationale, and public distribution risks separately from technical feasibility.
9. Add simulator compile tests and repeatable signed-device validation; recognize that full GPU/memory gates remain physical-device tests.
10. Decide whether to remain on Plume or fund a one-time modern Graine/MoltenVK migration based on measured maintenance/performance—not theory.

### Likely failure modes

- Jetsam from physical backing or unbounded GPU/shader caches.
- Shader/pipeline stutter and frame-tail spikes.
- Thermal throttling makes selected quality/FPS unsustainable.
- Background or interruption reveals deadlocks/resource lifetime bugs.
- Large imported installation creates storage/backup problems.
- Symbols/signing/static-library packaging breaks archive.
- Entitlement/privacy declarations exceed actual functionality or are missing.
- Product/distribution review raises content-rights issues separate from source legality.

### Verification criteria

- Written device matrix and pass/fail budgets exist.
- Multi-hour soak in representative gameplay meets memory/frame/thermal targets.
- Hundreds of lifecycle cycles and fault-injection save/import tests pass.
- Crash reports symbolicate and contain actionable engine checkpoints.
- Release archive has no Homebrew paths, AppKit, unsigned dylibs/plugins, JIT entitlement, executable downloaded content, or anonymous executable pages.
- All required privacy strings/capabilities have exercised feature tests; unused ones are removed.
- A clean clone can build the documented release artifacts with pinned public dependencies and user-owned data supplied only at runtime.

## Completed first implementation task (M1)

The initial M1 work follows this reviewable scope:

1. Restore iOS-first platform detection and a coherent iOS core source branch.
2. Produce static `rexcore`/minimal-runtime targets for both `iphoneos` and `iphonesimulator`.
3. Exclude Vulkan/MoltenVK, NFD, desktop FFmpeg, GNS/CURL, Tracy, dynamic GPU plugins, and target-side host tools from that minimal graph.
4. Make both targets compile and link with no AppKit, Homebrew paths, raw dylib plugins, or proprietary payload embedded.
5. Add build-graph assertions and document the exact configure/build commands.

Stop that first pull request at a clean static build. M2 then launches the scene-based Theft4 harness and calls the C ABI; M3 owns the physical-device memory/fault/fiber/thread proof; M6 owns generated `PPCFunc` execution. This keeps each architectural risk independently reviewable.

M2 has now completed its app/core bring-up gate; exact implementation paths and
commands are in `docs/IOS_APP_BUILD.md`. The next implementation task is **M3**:
add an owned worker and isolated simulator memory/fault/fiber/thread probes
through the C boundary before attempting full runtime or guest startup.
The user has since authorized the physical M5 iPad for this work; the iPhone
remains out of scope. Follow the boot-priority update above instead of treating
the original exhaustive probe schedule as a prerequisite to each boot attempt.

## Completion definition for the first pass

This reconnaissance pass is complete when:

- the architecture and build evidence are reviewed;
- the team chooses the minimum-change Plume path or explicitly overrides it in favor of the higher-cost modern Graine/MoltenVK migration;
- a physical test device and supported-device floor are named;
- M1 is accepted as the next implementation task, with M2/M3 queued behind its clean-build gate;
- no broad source changes begin before those decisions.
