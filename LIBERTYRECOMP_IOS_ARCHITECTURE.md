# Theft4 / LibertyRecomp iOS Architecture Reconnaissance

**Validated implementation update (2026-09-15):** the Theft4 UIKit host now
initializes the real ReXGlue kernel, applies the user's matching TU8, and runs
the checked-in GTA IV AOT code on the physical M5 iPad without a CPU JIT. It
consumes real PM4 traffic and has remained stable through thousands of swaps.
LibertyRecomp's own Xenos shader analyzer and SPIR-V translator now run on-device;
many real GTA IV vertex and pixel shaders have translated successfully and the
resulting modules have been accepted by MoltenVK. The production `VulkanInstance`,
`VulkanDevice`, and `VulkanCommandProcessor` now run together on the Apple M5
GPU. It creates a real three-image, 2816x1940 swapchain from Theft4's
UIKit-owned `CAMetalLayer`, composites GTA IV's 1280x720 guest output, and has
returned `VK_SUCCESS` from acquire, submit, and `vkQueuePresentKHR` through
thousands of frames. The user visually confirmed that GTA IV boots. Native
GameController input is now linked with hot-plug/haptics, pending a physical-pad
acceptance test. Native 48 kHz stereo AudioUnit output also runs and receives
converted guest mixer blocks, but the blocks are silent because the intentionally
headless XMA service still calls `ProduceSilentBuffer`.

This supersedes the report's initial Plume recommendation: the selected
rendering path is LibertyRecomp's existing Vulkan backend over statically linked
MoltenVK. Presentation is no longer a gap.
See [actual startup evidence and build instructions](docs/IOS_GAME_STARTUP.md),
[core validation](docs/IOS_CORE_BUILD.md), and [earlier app validation](docs/IOS_APP_BUILD.md).
The findings below describe the original audited revision and its full app targets.

**Audit revision:** `36b729dcc166910c88a2960164707ba9d9e5138c`
**Audit date:** 2026-09-14
**Target product name:** Theft4 (the iOS application role described in the brief as Liberty Bridge)
**Scope:** repository forensics, source-backed architecture mapping, clean-build validation, iOS feasibility analysis, and planning. No broad port was implemented.

This report considers only public source and user-supplied files from a lawfully acquired copy of the game. It does not require, seek, or recommend leaked proprietary source. The checked-in recompiled C++ and project patches were treated as the public repository's inputs; no game payload was obtained or used during this audit.

**Target clarification:** this is not a proposal for one universal macOS/iOS application or for two cooperating applications. The shipped product described here is one iOS app, Theft4, with LibertyRecomp embedded in-process. The macOS target is retained as the upstream reference build, regression harness, and host for code/shader generation tools.

## 1. Executive summary

An iOS port is **realistically achievable, but conditional and difficult**. LibertyRecomp is an ahead-of-time (AOT) static recompilation project, not a runtime PowerPC JIT. Its generated game functions are ordinary C++ compiled into the signed host executable, so iOS's prohibition on arbitrary runtime-generated executable code is not the primary obstacle. The checked-in generator/runtime even states that no JIT is used and the dispatcher calls registered `PPCFunc*` functions directly.[1][2]

The present `ios-release` target is **broken**. It is not a nearly finished port hidden behind signing configuration. It has useful scaffolding—an iOS toolchain, Xcode presets, bundle metadata, Objective-C++ platform files, GameController/CoreHaptics work, and iOS-oriented memory/file/logging implementations—but the current Graine/ReXGlue platform detector classifies iOS as macOS. The iOS source files are therefore not selected, several have drifted from current interfaces, and CMake includes desktop-only dependencies on the iOS graph.[3][4]

There are two renderer/consumer lineages in the repository:

1. The target selected by the current iOS preset is the older monolithic `LibertyRecomp` consumer, configured to prefer **Plume's native Metal backend** while still incorrectly compiling/linking Vulkan and MoltenVK fallback baggage.
2. The supported macOS path returns early into the newer Graine consumer using a **Vulkan/SPIR-V GPU plugin and MoltenVK**.

The device experiments change the renderer recommendation. For the shortest
credible bring-up, retain the small Theft4/UIKit shell and embed the existing
LibertyRecomp Vulkan renderer through static MoltenVK. This reuses the production
Xenos command processor, caches, and SPIR-V translation path rather than building
a parallel graphics implementation. XeniOS is useful as a behavioral oracle and
debugging comparison, but is neither embedded nor used as Theft4's CPU/runtime
backend. The direct-Metal prototype remains useful only as a lifecycle and
presentation scaffold.

The recommended application architecture is **A + C** from the brief: Theft4 owns `UIApplication`/`UIScene`, the `UIWindow`/view/`CAMetalLayer`, import and storage UI, settings, lifecycle, logs, and crash presentation; it embeds LibertyRecomp in-process as static libraries or a static XCFramework behind a small C ABI. A separate runtime process is not a viable normal iOS design.

The biggest immediate blocker is the regressed and desktop-contaminated iOS build graph. The biggest fundamental risk is the runtime's guest-memory/fault/fiber model: a roughly 4.5 GiB contiguous virtual address layout with multiple file-backed aliases, `MAP_FIXED`, protection changes, signal-driven MMIO/write-watch handling, guest fibers, and host-thread suspension. Those assumptions must be proved on a physical iPhone before substantial UI or renderer work.[2][7]

### Bottom-line assessment

| Question | Finding |
|---|---|
| Is the port feasible? | **Yes, conditionally.** ARM64/AOT is favorable; real-device memory, fault, and fiber experiments decide feasibility. |
| Current iOS status | **First native/AOT 3D verified on device.** Signed AOT runtime, TU8, graphics translation, Vulkan-over-MoltenVK presentation, GameController integration, and native audio output all build and launch. A captured frame shows Niko in the opening cutscene; transition delay, playability, audio correctness, lifecycle/import UX, and performance remain. |
| CPU execution model | **AOT.** No runtime PowerPC-to-ARM64 code generation and no required JIT entitlement. |
| Immediate build blocker | Resolved in the dedicated Theft4 iOS graph. |
| Fundamental runtime risk | Reduced substantially by sustained physical-iPad execution; long-run lifecycle, memory pressure, and device-floor validation remain. |
| Shortest renderer path | Existing LibertyRecomp Vulkan backend over static MoltenVK, hosted by Theft4/UIKit. |
| Clean host integration | Theft4-owned UIKit lifecycle plus a statically linked engine and versioned C ABI. |
| Exact next implementation task | Diagnose the long depth-heavy near-black world-transition interval, then verify continuous cutscene motion and controller progress in a normal non-diagnostic AOT launch. |

## 2. Current LibertyRecomp architecture

### 2.1 The repository currently has two application graphs

```text
                         root CMakeLists.txt
                                |
               +----------------+----------------+
               |                                 |
     Apple desktop/macOS                  every other target,
     LIBERTY_RECOMP_GRAINE_MAC            including iOS
               |                                 |
        add_subdirectory(glue)        thirdparty + glue/ReXGlue SDK
        then early return                       |
               |                         LibertyRecompLib
     Graine gta4-recomp app                     |
               |                          LibertyRecomp app
     rexruntime + Vulkan UI                     |
     shared GPU plugins                 Plume renderer selected by
     MoltenVK on macOS                  LIBERTY_RECOMP_METAL on iOS
```

This distinction is essential. The root condition explicitly excludes `CMAKE_SYSTEM_NAME=iOS` from `LIBERTY_RECOMP_GRAINE_MAC`; iOS then builds the legacy `LibertyRecompLib` and `LibertyRecomp` projects after adding the same ReXGlue SDK.[3] The modern GTA IV consumer only creates a real executable for Apple desktop; on iOS it becomes an interface target that exports generated sources.[5]

The common assets are important:

- Both graphs consume the same checked-in ReXGlue-generated PPC C++.
- Both use the same ReXGlue runtime/kernel concepts.
- They do **not** use the same host application, renderer implementation, UI, packaging, or plugin model.
- Recent macOS renderer work is concentrated in `glue/rexglue-sdk-main/src/graphics/gta4_native`, while the iOS-selected host still uses `LibertyRecomp/gpu/video.cpp` and Plume.

### 2.2 Major directory map

| Path | Role verified from code/build files |
|---|---|
| `CMakeLists.txt` | Top-level policy, dependency patching, platform selection, embedded-payload checks, split between modern macOS and legacy/non-mac consumers. |
| `CMakePresets.json` | Windows, Linux, macOS, iOS, Android, PS4, and Switch configure/build presets; iOS is Xcode + `iphoneos` arm64 only. |
| `cmake/` | Dependency-patch orchestration, host LLVM/toolchain checks, macOS runtime bundling, platform packaging helpers. |
| `toolchains/` | Cross-toolchains. `ios.cmake` declares iOS 16, arm64, `iphoneos`, Metal on, Vulkan/D3D off, embedded assets on. |
| `LibertyRecomp/` | Older/portable application host: installer, kernel glue, VFS, patches, UI, Plume GPU implementation, input, audio, saves, networking, and platform adapters. This is the current iOS preset's app. |
| `LibertyRecompLib/` | Common game-facing resources plus inclusion of the authoritative generated PPC source list; 43 MiB combined shader cache, shader overrides, font atlases, and optional/private build resources. |
| `glue/` | Integrates the vendored Graine/ReXGlue SDK and its GTA IV title consumer. It forces Vulkan for the modern graph and adds the workspace MoltenVK build on Apple. |
| `glue/rexglue-sdk-main/include/rex/` | Runtime public contracts: PPC context, memory, platform flags, UI, input, audio, kernel and application interfaces. |
| `glue/rexglue-sdk-main/src/core/` | Host OS primitives: memory mapping, exceptions, atomics, clocks, fibers, threads, dynamic libraries, logging, sockets, files, dialogs. |
| `glue/rexglue-sdk-main/src/codegen/` | Manifest/config loading, XEX analysis, function discovery/validation, PowerPC lowering, generated-file emission. |
| `glue/rexglue-sdk-main/src/rexglue/` | Host CLI, including `rexglue codegen`. This is a build-host tool, not part of the phone runtime. |
| `glue/rexglue-sdk-main/src/system/` | Runtime composition, XEX/module loading, guest memory/heaps, dispatcher, XThreads/XObjects, MMIO, VFS-facing objects, services, and GPU plugin loader. |
| `glue/rexglue-sdk-main/src/kernel/` | Xbox kernel/XAM/XBDM export tables and implementations/stubs. |
| `glue/rexglue-sdk-main/src/{filesystem,input,audio,ui}/` | Service abstractions and concrete host drivers. SDL3 is the main portable input/audio/window dependency. |
| `glue/rexglue-sdk-main/src/graphics/` | Modern Xenos GPU emulation, SPIR-V translation, Vulkan backend, and title-specific GTA IV native Vulkan renderer. |
| `glue/rexglue-sdk-main/gta4-recomp/` | Manifest/config, 84 generated C++ chunks, registration code, current macOS app, GTA IV hooks, install/network/input code, and tests/tools. |
| `thirdparty/` | Direct app-side dependencies: Plume, custom MoltenVK wrapper, FFmpeg wrapper, Native File Dialog, GameNetworkingSockets, SDL2 remnants, msdf/Freetype consumer, and utility libraries. |
| `third_party/` | Smaller Xbox-format/support dependencies used by root build (`xbox_support`, disassembler, mspack, AES, TinySHA1). |
| `tools/` | Host-side extraction, analysis, shader conversion, SDK/tool sources, fixtures, debugger, and utilities. `XenosRecomp` translates shaders; it is not the CPU recompiler. |
| `os/android/`, `os/ios/` | App-shell resource trees. The iOS tree contains duplicate/older assets but no UIApplication/scene implementation or CMake-owned app shell. |
| `libserver/`, `libserver-data/` | Optional community/multiplayer server and data. This is not needed for the first iOS bring-up. |
| `docs/` | Build/install/platform and investigative documentation. Several iOS claims are stale and conflict with code. |
| `.github/workflows/` | Desktop/repository automation; no iOS configure/build/device CI was found. |
| `vcpkg/`, `thirdparty/vcpkg/` | Overlay triplets and pinned package manager for desktop graphs. The iOS preset deliberately bypasses vcpkg. |

### 2.3 Runtime composition

At runtime, the host creates a ReXGlue `Runtime`, guest `Memory`, `KernelState`, export resolver, virtual filesystem, input/audio services, and graphics implementation. The XEX loader maps title data and sections, resolves imports to host kernel implementations, registers recompiled functions, and begins a guest `XThread`. A guest thread is a host thread with a `PPCContext`; generated functions operate on that context and a pointer to the guest memory base. Indirect guest calls look up a statically registered host function pointer in the dispatcher/function table.[1][2]

The Xbox 360 assumptions are explicit rather than host-architecture accidents:

- 32-bit guest virtual addresses within a 4 GiB guest view.
- Big-endian PPC register and memory semantics.
- Several aliased physical address windows (`0xA...`, `0xC...`, `0xE...`, and a raw host view).
- Xbox kernel/XAM exports implemented or stubbed by the runtime.
- Xenos command packets, tiling, formats, shader microcode, and synchronization translated by the renderer.
- Guest threads/events/semaphores/timers/APCs represented with host primitives.

### 2.4 Build system characteristics

- Minimum CMake is 3.29; C++23 is required.
- Dependency setup is not a plain submodule checkout. `tools/setup_repo.py` initializes nested pins, applies six reviewed dependency patches, records patch state in Git metadata, and bootstraps vcpkg.[8]
- The dependency patches intentionally leave six submodule worktrees modified. This is expected setup state, not an unclean user edit.
- The macOS preset uses Ninja, Apple Silicon, a macOS 26 deployment target, host/system OpenSSL/CURL/Vulkan pieces, and the modern Graine graph.
- The iOS preset uses Xcode and `toolchains/ios.cmake`, but still routes into the legacy target and demands game files at configure time.
- Generated CPU sources are checked in. Normal builds do not run codegen or need an XEX to compile the CPU code. Embedded targets nevertheless require a payload for packaging under the current CMake policy.[8]

## 3. Recompilation pipeline

### 3.1 End-to-end data path

```text
lawfully acquired Xbox 360 installation
        |
        +-- default.xex / default.xexp and RPF/DLC/media data
        |
gta4_manifest.toml + gta4_config.toml
        |
host `rexglue codegen`
        |
tool-mode ReXGlue Runtime loads/decompresses the XEX
        |
DecodedBinary + XEX metadata/imports/PDATA
        |
analysis phases
  discover boundaries -> register forced/import functions
  -> validate direct/indirect control flow -> emit
        |
ordinary generated C++
  gta4_recomp.0.cpp ... gta4_recomp.83.cpp
  gta4_init.{h,cpp} + gta4_register.cpp + sources.cmake
        |
Apple Clang compiles that C++ for arm64 into signed Mach-O code
        |
host Runtime loads the matching XEX again as data/metadata
        |
PPCContext + guest memory + direct registered PPCFunc calls
        |
kernel/VFS/audio/input/renderer host services
```

### 3.2 Analysis and generation call chain

The authoritative manifest is `glue/rexglue-sdk-main/gta4-recomp/gta4_manifest.toml`. It names project `gta4`, points to `assets/default_v8.xex`, emits to `generated`, and includes `gta4_config.toml`. The CLI route is:

The manifest's `assets/default_v8.xex` input and even its parent `assets/` directory are **not present in the public checkout**. The checked-in generated output can be built without regenerating it, but a clean clone cannot reproduce CPU code generation by itself. Before regeneration is treated as reproducible, the project must document the exact supported XEX revision/hash and validate a matching file supplied from the user's lawful copy; this audit did not possess or infer that private input.

1. `src/rexglue/commands/codegen_command.cpp` parses/discovers the manifest and constructs `ProjectRecompiler`.
2. `src/codegen/manifest.cpp` validates project/module entries and resolves paths.
3. `src/codegen/project_recompiler.cpp` creates a tool-mode runtime, loads the entry XEX and any DLL modules, creates `BinaryView`/`CodegenContext` objects, calls `Analyze`, then `CodegenWriter`.
4. `src/system/user_module.cpp` and `src/system/xex_module.cpp` parse XEX headers, compression/encryption metadata, sections, imports, resources, execution data, and PDATA.
5. `src/codegen/decoded_binary.cpp` reads and byte-swaps PPC instruction words.
6. `src/codegen/analyze.cpp` runs discovery, registration, and validation phases. `phase_discover.cpp` finds candidate functions/control flow; `phase_register.cpp` adds XEX/PDATA/config/import-driven entries; `phase_validate.cpp` rejects unresolved invalid control flow unless explicitly forced.
7. The PPC emitter and templates generate host-neutral C++ plus the registration and build files.[9]

`gta4_config.toml` matters as much as automatic analysis. It forces missed address-taken callbacks, cross-function branch targets, CRT replacements, and known boundaries. Generic pointer scanning is deliberately not the sole authority. A missing address-taken target can therefore survive compilation and fail later when `ResolveIndirectFunction` cannot find a registered address.

### 3.3 Generated output and host portability

At the audited revision the generated directory is about **170 MiB**, contains **84** `gta4_recomp.N.cpp` chunks, approximately **5.71 million generated C++ lines**, and **38,606** `registrar->SetFunction(address, host_function)` entries. Those counts were measured from the checkout; the README's older codegen statistics do not match current output.

The generated ABI is deliberately portable:

```cpp
using PPCFunc = void(PPCContext&, uint8_t* guest_memory_base);
```

Functions use explicit PPC context fields, host C++ control flow, byte-swap helpers, and SIMDe/vector abstractions. `gta4_register.cpp` maps guest addresses to normal C++ function symbols. `xthread.cpp` says JIT execution was replaced by direct calls. No target-specific x86 machine code is embedded in the generated C++.[1]

### 3.4 Endianness

- XEX/PPC instruction words are decoded with explicit byte swapping.
- Generated 16/32/64-bit guest loads and stores use `__builtin_bswap*`; byte loads/stores are unchanged.
- Kernel/native-call marshaling uses big-endian wrappers or mapped guest pointers.
- The host may be little-endian ARM64 without changing the guest data model.

This is a good design for iOS, but it does not eliminate alignment/aliasing undefined-behavior tests. The project globally uses `-fno-strict-aliasing` and strict floating-point settings in the legacy generated library. ARM64 warnings around FPCR inline assembly also need cleanup before warnings-as-errors builds.

### 3.5 What remains runtime data

AOT does **not** make the original installation unnecessary. The checked-in native code replaces PPC instruction execution, but the runtime still uses a matching, lawfully supplied XEX/XEXP for identity, headers, data/rdata/BSS, imports, resources, and layout; RPF archives, audio, movies, DLC, and saves remain data files. Theft4 should validate and copy these as data into its sandbox. It must never download or synthesize executable additions from them.

### 3.6 Platform-independent versus iOS-specific stages

| Stage | Portable as-is | iOS work |
|---|---|---|
| XEX analysis and PPC decode | Host tool; platform-neutral C++ | Keep as a macOS build-host tool. Never cross-compile/run the CLI on iPhone. |
| Function discovery/config | Platform-neutral | No iOS-specific change expected. |
| Generated C++ | ARM64-capable AOT design | Compile with Apple Clang; verify intrinsics, FP state, alignment, dead stripping, and the physical-address offset issue. |
| XEX/module load | Mostly portable | Sandboxed paths, immutable game root, error reporting, memory-map validation. |
| Guest memory | Abstracted but host-sensitive | Real-device alias/protection/page-size experiment is mandatory. |
| Threads/fibers/faults | Interfaces portable; implementations host-specific | iOS implementations or Darwin variants must be repaired and proved. |
| Kernel/VFS | Mostly portable | Paths, case/Unicode behavior, save durability, lifecycle shutdown. |
| Renderer/shaders | Host-specific backend | UIKit layer, native Metal path, iPhoneOS artifacts, device capability/performance. |
| Input/audio | Abstracted | SDL3/GameController/touch and AVAudioSession lifecycle integration. |
| Packaging/app | Not portable | UIKit/UIScene shell, signing, privacy strings, static linkage, sandbox import. |

## 4. Existing Apple/iOS support

### 4.1 Classification

| Area | Status | Evidence |
|---|---|---|
| Toolchain/presets | Scaffolded | `toolchains/ios.cmake`, `ios-debug`, `ios-release`; device arm64 only. |
| App bundle resources | Scaffolded | plist, entitlements, launch storyboard, asset catalogs, CMake bundle properties. |
| Platform detection | Broken/regressed | All Apple targets become macOS in current ReXGlue; no active `REX_PLATFORM_IOS`. |
| Core iOS implementations | Partially implemented, unwired, stale | Memory/files/logging/fiber/net/dynlib/dialog files exist but are not in active source lists and some no longer compile against headers. |
| Lifecycle/window | Missing/broken | No valid `UIApplicationMain`/SDL3 iOS bootstrap or scene owner; macOS APIs selected. |
| Renderer | Partial | Plume Metal core has iOS branches, but AppKit adapter/device selection and shader artifacts are macOS-oriented. |
| Input/haptics | Partial, unwired | GameController/CoreHaptics files exist; CMake/input factory do not select them; API drift exists. |
| Audio | Partial | Portable SDL audio exists; platform regression selects unavailable CoreAudio; no AVAudioSession management. |
| Filesystem | Partial/inconsistent | iOS path helpers exist; bundle, Documents, installer, and writable-root assumptions conflict. |
| Networking | Mostly desktop graph | BSD socket wrapper exists but is unwired; CURL/GNS/OpenSSL graph is not iOS-ready. |
| CI/proof | Absent | No iOS workflow, simulator preset, device test, or build artifact. |

Overall classification: **currently broken**, with **substantial scaffolding and partial implementation**. It is not fair to call it merely empty scaffolding, but there is also no source-backed basis for calling it functional. “Abandoned” cannot be established: history shows real iOS work followed by a dependency/runtime migration regression rather than a formal removal.

### 4.2 History

- `624b3ce5961f0b713ba7f35cf4c6f52b2c1e796c` introduced iOS/Android/PS4/Switch platform work.
- `9b73fcd9e1d38381764bf41b3c83f5c9a90ca155` expanded embedded payload, multiplayer/voice, and mobile/console work.
- `4a42de0afa0d264802d023d081c349fe10c65ac6` added Apple Game Center integration.
- `a1d3a84d9367db654a690e2e10d1ca450f95f355` replaced the prior ReXGlue tree with the Graine SDK. The parent revision checked `TARGET_OS_IOS` before broad `TARGET_OS_MAC` and had an explicit iOS core-source branch; the current tree lost both.[10]
- Subsequent commits moved supported macOS into the modern Graine consumer and invested heavily in its Vulkan renderer. They did not restore an iOS build.

### 4.3 Exact first-party iOS components

| Files | What they do now | Audit result |
|---|---|---|
| `toolchains/ios.cmake` | Sets `CMAKE_SYSTEM_NAME=iOS`, arm64, deployment 16, `iphoneos`; turns Metal and embedded assets on. | Useful device toolchain; no simulator variant. Comments mention obsolete SDL2 assumptions. |
| `CMakePresets.json` | Defines Xcode `ios-debug`/`ios-release`; points to missing `tools/local_game_payload`. | Configure entry exists, but default cannot work from a public checkout. |
| Root `CMakeLists.txt` | Validates embedded game files; deliberately routes iOS away from Graine-mac early return. | Validation is over-eager for an import-based iOS app; error text points to missing documentation. |
| `LibertyRecomp/CMakeLists.txt` | Creates iOS `.app`, assigns plist/assets/entitlements/signing, copies payload under `game`, compiles Metal helpers. | Real scaffold, but target graph includes desktop components and stale host code. |
| `LibertyRecomp/res/ios/iOSBundleInfo.plist.in` | Bundle metadata, device/Metal/orientation/controller/LAN declarations. | No scene manifest; product identifiers/names are not Theft4; microphone disclosure absent. |
| `LibertyRecomp/res/ios/LibertyRecomp.entitlements` | Game Center entitlement. | JIT entitlement correctly absent; other capabilities should be added only when functionality requires them. |
| `LibertyRecomp/res/ios/Assets.xcassets`, `LaunchScreen.storyboard` | App icon, color, launch resources. | Usable scaffold, must be rebranded. |
| Root `os/ios/Assets.xcassets`, `LaunchScreen.storyboard` | Older duplicate iOS resources. | Not the resources selected by the current app CMake; likely stale duplication. |
| `LibertyRecomp/os/ios/ios_paths_objc.mm` | Objective-C path lookup for sandbox/bundle. | Partial basis for path service. |
| `LibertyRecomp/os/ios/logger_ios.cpp` | Unified logging wrapper under `REX_PLATFORM_IOS`. | Becomes an empty translation unit because that macro is not defined. |
| `LibertyRecomp/os/ios/{haptics_ios,adaptive_triggers_ios}.{h,mm}` | CoreHaptics/GameController feedback. | Partially implemented; callers and exposed names do not match once iOS macros are fixed. |
| `LibertyRecomp/os/ios/media_ios.cpp` | Checks whether other audio is playing. | Not an AVAudioSession lifecycle implementation. |
| `LibertyRecomp/os/ios/process_ios.cpp` | Rejects child-process launch. | Correct direction for iOS; reinforces in-process design. |
| `LibertyRecomp/os/ios/{user_ios,version_ios}.cpp` | Platform user/version adapters. | Small and plausible, but not proof of app/runtime functionality. |
| `LibertyRecomp/os/gamecenter/achievement_bridge_gc.mm` | GameKit achievement bridge with UIKit/AppKit selection. | Current macro regression takes AppKit branch on iOS. |
| `LibertyRecomp/install/{embedded_assets,platform_paths}.cpp`, `LibertyRecomp/user/paths.cpp` | Bundle/install/save/cache path selection. | iOS branches exist, but bundle layout and read-only/writable assumptions conflict. |
| `glue/rexglue-sdk-main/src/core/memory_ios.cpp` | Intended non-executable guest memory, `shm_open`, aliases, `mprotect`, page alignment. | Never selected; important design reference, not validated production code. |
| `glue/rexglue-sdk-main/src/core/filesystem_ios.mm` | Bundle, Documents, temp, cache paths. | Never selected; needs app-owned path injection rather than hidden globals. |
| `glue/rexglue-sdk-main/src/core/{logging_ios.mm,net_ios.cpp}` | `os_log` and BSD-socket platform startup/shutdown. | Never selected. |
| `glue/rexglue-sdk-main/src/core/dynlib_ios.cpp` | iOS dynamic library adapter. | Never selected; method signature and constants are stale. Prefer no plugin discovery on iOS. |
| `glue/rexglue-sdk-main/src/core/fiber_ios.cpp` | AArch64 `setjmp`/stack-pivot attempt. | Never selected and incompatible with current `Fiber` fields; PAC/SIMD/unwind behavior unproved. |
| `glue/rexglue-sdk-main/src/core/keyboard_dialog_ios.mm` | Presents UIKit text input and waits for completion. | Unwired and can deadlock when called on main thread. Host UI should own dialogs asynchronously. |
| `glue/rexglue-sdk-main/src/input/ios/ios_input_driver.{h,mm}` | GameController discovery/state/motion/CoreHaptics. | Not added by input CMake or constructed by `InputSystem`; interface drift remains. |
| `glue/rexglue-sdk-main/include/rex/platform.h`, SDK/root `CMakeLists.txt`, `src/core/CMakeLists.txt` | Platform macros, platform name, OS source selection. | Primary regression: iOS is macOS. |
| `LibertyRecomp/ui/game_window.cpp` | SDL window/native handle extraction. | Calls `CGGetActiveDisplayList`, desktop sizing/fullscreen, and Cocoa window property for all Apple. |
| `LibertyRecomp/main.cpp` | Legacy host startup/runtime creation. | Plain desktop-style blocking `main`; SDL main glue is Android-only; stale Runtime API calls. |

### 4.4 Third-party iOS-related components

Vendored dependencies contain many incidental iOS references, tests, sample projects, and platform headers. Their presence is **not** first-party iOS support. The relevant build-consumed components are:

- Vendored SDL3 under `glue/rexglue-sdk-main/thirdparty/sdl3`: real UIKit window, touch, audio, GameController, Metal-layer, and iOS main support. The project integration does not use its startup contract correctly. SDL documents that iOS startup must be owned appropriately and that the bundle is read-only.[11]
- `thirdparty/plume/{plume_metal.cpp,plume_metal.h,plume_render_interface_types.h}`: iOS-aware Metal core and Apple window/view carrier.
- `thirdparty/plume/{CMakeLists.txt,plume_apple.mm,plume_apple.h}`: current all-Apple helper uses AppKit, `NSWindow`, `NSScreen`, and IOKit; it needs an iOS split.
- `thirdparty/MoltenVK/CMakeLists.txt`: project wrapper requires AppKit/IOKit and builds a raw shared dylib. The upstream MoltenVK source supports iOS, but this wrapper does not select its iOS packaging. Upstream documents static/dynamic XCFrameworks for iOS and raw `.dylib` only for macOS.[12]
- `thirdparty/nativefiledialog-extended`: all Apple maps to its Cocoa/AppKit backend; it should not be in the iOS graph.
- `thirdparty/ffmpeg-core`: all Apple selects macOS arm64 prebuilts/frameworks; not an iOS slice.
- `thirdparty/msdf-atlas-gen`/Freetype setup: source Freetype fallback is enabled for PS4/Switch/Android but not iOS, producing the first real configure error.
- `thirdparty/GameNetworkingSockets`: currently included for iOS and requires OpenSSL/protobuf/abseil integration that has not been designed for this target.
- `tools/XenosRecomp/XenosRecomp/air_compiler.cpp`: host-side Metal/AIR shader compiler. The current dependency patch removed an earlier literal macOS SDK selection, but the tool still has no explicit target/SDK parameter and invokes bare `xcrun metal/metallib`.

### 4.5 Documentation versus code

`docs/PLATFORM_SETUP.md` claims SDL2, a working `scripts/ios-setup.sh`, no installer wizard, bundle game reads, Documents saves, and `os_log`. The repository uses SDL3; the script and `tools/local_game_payload/README.md` do not exist; installer/update code is not excluded from iOS; platform macros prevent the advertised paths/logging; and no valid UIKit lifecycle exists. For iOS, the code and configure trace contradict the document, so the document is not evidence of working support.[3][8]

## 5. What currently works

“Works” here means source-backed or host-build-validated, not assumed device behavior.

- Repository setup completes: all recursive public dependencies initialize, the reviewed patches apply, bundled vcpkg bootstraps, and `python3 tools/setup_repo.py --check` reports success.
- The complete generated PPC corpus is checked in and included in the generated build graph. The audit measured its size and registration table, but the macOS build stopped before compiling any `gta4_recomp.*.cpp` translation unit; corpus-level ARM64 compilation therefore remains unvalidated.
- The code generator is an AOT pipeline with a concrete GTA IV manifest/config and checked-in outputs.
- Apple Silicon/ARM64 is recognized by the generated/runtime code and modern macOS build.
- Endianness is explicit in generated memory access and loader helpers.
- A macOS configuration can be generated after satisfying its documented host dependencies; the audit's diagnostic build reached step 845 of 1109 before a host shader-tool crash.
- The iPhoneOS compiler itself works when full Xcode is selected; CMake correctly identifies AppleClang 21 and arm64 before dependency configuration fails.
- SDL3, upstream MoltenVK, and Plume each contain genuine iOS-capable code that can be reused.
- iOS bundle resources, signing cache variables, and Metal helper-shader CMake commands exist.
- A number of iOS platform implementation files contain useful prior work, especially paths, logging, memory intent, GameController, and haptics.
- The no-JIT design means generated CPU code can live in normal signed Mach-O `__TEXT`; no anonymous executable guest memory is intrinsically required.

None of these items establishes that the current iOS app configures, links, launches, initializes guest memory, presents a frame, or executes the game.

## 6. What currently does not work

- A public clone cannot run the documented iOS preset unchanged because the default payload directory and its referenced README are absent.
- With synthetic marker files used only to bypass CMake `EXISTS` checks, configure fails at Freetype before generating Xcode projects.
- After a diagnostic dummy Freetype target, the next failure is the macOS-only MoltenVK wrapper's required AppKit lookup. The trace also reports Native File Dialog selecting `PLATFORM_MACOS` and Plume enabling both Vulkan and Metal.
- Substituting iOS frameworks only to continue configuration exposes another independent OpenSSL/GameNetworkingSockets failure. These probes are not fixes and cannot produce a valid target.
- `REX_PLATFORM_IOS` is never selected, so iOS-specific ReXGlue sources and application branches are dormant.
- Dormant `dynlib_ios.cpp` and `fiber_ios.cpp` do not match current headers.
- The legacy host calls removed/currently different ReXGlue APIs (`Runtime::Setup` shape and `HasFunctionTable`), so later compile failures are deterministic even after dependency guards.
- The app has no correct `UIApplication`/`UIScene` or SDL3 iOS startup integration.
- Apple window code uses macOS display and Cocoa-window APIs on iOS.
- The intended iOS input driver is neither compiled nor instantiated; the legacy haptics caller and implementation APIs disagree.
- Audio backend selection takes the macOS CoreAudio class while its sources are excluded for `CMAKE_SYSTEM_NAME=iOS`.
- Filesystem logic mixes read-only bundle data, writable game/install trees, Documents saves, and macOS `.app/Contents/Resources` layout.
- Current Metal helper libraries and all measured guest AIR cache entries identify as macOS artifacts. They cannot be assumed loadable on iPhoneOS.
- No simulator preset, iOS CI job, device-memory test, launch test, first-frame test, or proof artifact exists.

## 7. iOS blockers

### A. Trivial build/configuration problems

- Full Xcode must be selected; Command Line Tools alone do not provide the iPhone SDK/compiler environment.
- CMake 3.29+, Ninja for host builds, Python 3.10+, signing settings, and host generator tools are prerequisites.
- Default payload path and referenced setup files are missing.
- No simulator preset; docs use obsolete filenames/SDL version.
- iOS is omitted from several otherwise simple platform exclusion/fallback conditions.

These are necessary repairs but do not make the port functional.

### B. Missing or stale platform implementations

- Correct iOS-first platform detection and source selection.
- Current-interface iOS memory, mapped-memory, fiber, threading, exception, dynamic-library policy, paths, logging, and socket code.
- UIKit/scene lifecycle and render-surface ownership.
- Async host dialogs, import flow, touch UI, audio-session events, memory warnings, and termination/save flushing.

### C. Unsupported third-party dependencies

- Native File Dialog Cocoa/AppKit backend.
- macOS-only FFmpeg prebuilts.
- Custom MoltenVK AppKit/IOKit/raw-dylib wrapper.
- GNS/OpenSSL/protobuf/abseil graph.
- Freetype source selection.
- Shared Tracy/runtime/plugin targets and cross-compiled host tools if the modern graph is chosen.

The first port should exclude nonessential desktop features and reintroduce them one subsystem at a time.

### D. Architectural assumptions incompatible or unproved on iOS

- A contiguous mapping reaching `0x120000000` (roughly 4.5 GiB) with eight guest-visible views plus a ninth raw-physical view.
- A 512 MiB physical backing range precommitted for GPU access.
- `shm_open`/`ftruncate`, `mmap(MAP_SHARED|MAP_FIXED)`, `mprotect`, decommit/advice, and 16 KiB host pages.
- Fault-driven MMIO and write-watch behavior under Darwin signal/Mach exception rules.
- Host stack sizes and one-host-thread-per-guest-XThread behavior under iOS memory pressure.
- Fiber context preservation and thread suspend/APC semantics.
- Synchronous desktop `main` and global application state.

### E. Renderer problems

- Plume's selected Apple adapter is AppKit/IOKit-only.
- `GameWindow` retrieves a Cocoa window rather than a UIKit window/Metal layer.
- Metal device enumeration contains macOS assumptions.
- Helper `.metallib` files and 1,356 cached AIR entries are macOS-targeted.
- AIR generation has no explicit `iphoneos`/simulator target contract.
- Lifecycle does not stop drawable acquisition before background/surface detach.
- MetalFX is optional, unneeded for bring-up, and should initially be disabled.

### F. Recompilation/runtime problems

- Stale legacy host-to-ReXGlue API calls.
- Dormant iOS core sources incompatible with current public headers.
- Raw `0xE...` physical translation adds a required `0x1000` host offset only for Win32/macOS in the runtime header, template, and generated header. Correctly defining a distinct iOS platform would currently remove that compensation on 16 KiB hosts, conflicting with the mapping layout. This is a concrete correctness defect, not merely an experiment.[1][2]
- Unresolved indirect functions remain a title-specific runtime risk.
- Guest exception/unwind coverage is incomplete upstream.

### G. Apple platform and distribution restrictions

- The app and all executable engine/plugin code must be built, signed, and shipped with the app. Apple guideline 2.5.2 disallows downloading/installing/executing code that changes app functionality.[13]
- The imported XEX must remain **data**. It must not become runtime-generated native code.
- A normal child runtime process is not a portable iOS architecture.
- The app bundle is read-only; imported installations and saves require container directories. Apple and SDL both document sandbox/container behavior.[11][14]
- Gameplay should quiesce in background. UIKit tells background apps to do as little work as possible, and App Review limits background services to intended categories.[13][15]
- Local network, Game Center, controller, and microphone capabilities/privacy descriptions must match enabled features. Do not request entitlements preemptively.

## 8. ARM64 feasibility

### 8.1 Favorable evidence

- Generated functions are emitted as ordinary host C++; they contain no embedded x86 machine code. Their full ARM64 compilation still needs the M6 validation described below.
- `PPCContext` plus `uint8_t* base` is a stable, explicit call ABI.
- ARM64 host detection and SIMD portability infrastructure already exist.
- Apple Silicon macOS compiled substantial runtime and dependency code during this audit, and the generated code is ordinary host C++ with ARM64-aware support abstractions. No generated GTA IV translation unit was reached, so the full corpus's ARM64 compiler/ABI portability is a hypothesis to verify in M6, not a build result.
- Big-endian guest behavior is explicit.
- CPU code is AOT and can be normal signed code.

### 8.2 ARM64-specific risks

- FPCR access and exact Xbox floating-point behavior need conformance tests; the audit observed an inline-assembly operand-width warning.
- Unaligned guest access, strict FP, NaN/rounding behavior, atomics, reservations, and vector lane order need test fixtures on device.
- `arm64e` pointer authentication makes hand-written context/fiber/function-pointer tricks sensitive. The dormant iOS code's comments are not proof that its raw stack pivot or pointer stripping is correct.
- A 16 KiB host page changes protection granularity and the `0xE...` physical alias offset calculation.
- The iOS simulator is useful for UIKit/build verification but is not sufficient for page size, memory pressure, signal, PAC, GPU, or device scheduling conclusions.

### 8.3 Feasibility verdict

The generated code itself is **not the blocker** and should not be regenerated merely to target ARM64. The feasibility gate is the host runtime around it. If a physical device can sustain the required sparse mappings/aliases and reliably recover controlled faults while switching guest contexts, the CPU side is credible. If it cannot, the project needs a material memory-model redesign (segmented translation, explicit MMIO, reduced/precommitted physical backing, or fewer aliases), which would raise scope sharply.

## 9. Renderer analysis

### 9.1 What iOS actually selects today

The current iOS toolchain sets `LIBERTY_RECOMP_METAL=ON` and `LIBERTY_RECOMP_VULKAN=OFF`. The legacy `main.cpp` supplies no ReXGlue graphics backend; ReXGlue enters native-rendering mode; `Video::CreateHostDevice` puts `plume::CreateMetalInterface` first. Therefore the intended/default iOS renderer is **native Metal through Plume**.[5][6]

That is not what the whole build graph cleanly enforces. The legacy target does not consistently consume `LIBERTY_RECOMP_VULKAN`; Plume still compiles its Vulkan backend, all Apple targets currently add the repository's MoltenVK wrapper, and `Video::CreateHostDevice` exposes Vulkan as a fallback. This unwanted Vulkan/MoltenVK baggage is why AppKit enters the current iOS configure path. M1 should remove it from the minimal iOS graph rather than mistake its presence for the chosen runtime strategy.

Direct3D code and shader paths are Windows host backends. They are useful as behavioral references, but no Direct3D component belongs in the iOS link graph and the repository contains no Direct3D-to-Metal route that would improve this port.

### 9.2 Plume/Metal path

Reusable pieces:

- Plume Metal device, queues, resources, pipelines, swapchain, and iOS feature conditionals.
- `RenderWindow` can carry Apple window/view objects.
- Legacy GPU code and shader cache already understand Metal/AIR fields.
- CMake helper-shader commands already know to select `iphoneos` when `CMAKE_SYSTEM_NAME=iOS`.

Required changes:

1. Split `plume_apple.mm/.h` into macOS and iOS helpers; use UIKit/`CAMetalLayer`, `UIScreen`, and the system-default Metal device on iOS.
2. Make `GameWindow` either consume a host-provided layer or use the SDL UIKit property/`SDL_Metal_GetLayer`, never Cocoa display APIs.
3. Add explicit surface attach/detach/resize/scale/max-FPS/lifecycle events.
4. Regenerate all helper metallibs and the guest AIR cache for `iphoneos`; make XenosRecomp's AIR compiler take an explicit SDK/target and record artifact metadata.
5. Disable MetalFX and optional post-processing until base presentation is stable.
6. Validate required pixel/depth formats, memory heaps, fences/events, argument buffers, and shader features on representative devices.

This is the recommended M4–M5 path because it changes the fewest renderer layers in the app that the iOS preset already builds.

### 9.3 Modern Graine Vulkan/MoltenVK path

The newer macOS consumer forces Vulkan, creates shared `rexruntime`, `rexgpu-xenos`, and `rexgpu-gta4-native` targets, discovers GPU plugins dynamically, and stages a patched `libMoltenVK.dylib` into a macOS bundle. Recent title renderer development is concentrated here. Upstream MoltenVK supports iOS and translates SPIR-V to MSL, so the route is technically plausible.[12]

It is not a drop-in iOS switch because the repository wrapper links AppKit/IOKit, the UI surface selects macOS files, runtime/GPU components are shared plugins, and the modern title CMake excludes iOS. An iOS version needs official/static MoltenVK packaging or an equivalent iOS wrapper, static GPU factory registration, UIKit `VK_EXT_metal_surface` ownership, and a complete required-feature/format audit.

### 9.4 Recommendation and decision gate

Use Plume native Metal to establish runtime and graphics feasibility through the first meaningful state. Keep the Theft4 C ABI renderer-neutral. After a clear frame—but before a large amount of iOS-only renderer optimization—record two facts with small experiments:

- whether Plume can load regenerated iPhoneOS AIR and provide every required GTA IV format/synchronization feature;
- whether static MoltenVK exposes the modern Graine renderer's required Vulkan feature/format set at acceptable cost.

If Plume works, it is the shortest delivery path. If maintainers declare the legacy consumer obsolete or Plume hits a fundamental capability gap, pivot once to modern Graine/MoltenVK. Do not ship or indefinitely maintain both renderers. A new Metal renderer from scratch is not recommended; Plume already is the native Metal abstraction.

## 10. JIT/AOT analysis

### 10.1 Conclusion

LibertyRecomp is **AOT**. Recompilation happens on the development host before the app is built. Apple Clang compiles the generated C++ into the app's signed Mach-O. Runtime dispatch chooses among already compiled `PPCFunc*` symbols. The XEX supplies data/metadata and guest addresses, not executable ARM64 pages.[1][2][9]

### 10.2 Direct evidence

- Generated `gta4_register.cpp` contains 38,606 address-to-C++ `SetFunction` calls.
- `xthread.cpp` says JIT execution was replaced with direct function calls.
- `xex_module.cpp` says backend notification is unnecessary because ReXGlue has no JIT.
- `memory_ios.cpp` explicitly strips `PROT_EXEC`, avoids `MAP_JIT`, returns false from writable-executable support, and says generated code is in signed `__TEXT`.
- The runtime's “thunks” are guest address/table bookkeeping for host function pointers; they are not emitted ARM64 instruction buffers.

Some comments mark unported Xenia features as “JIT only” or TODO; that means those features were removed/stubbed in the AOT fork, not that normal title execution secretly starts a JIT.

### 10.3 iOS consequences

- No JIT entitlement should be requested.
- Guest memory should never be writable and executable.
- `MAP_JIT`, `PROT_EXEC` on guest mappings, downloaded native modules, and unsigned plugin loading should be forbidden by tests/build policy.
- Static libraries or signed embedded frameworks are appropriate. The current desktop `dlopen` plugin discovery should become static factory registration on iOS.
- GPU shader compilation/conversion by Metal/MoltenVK is a graphics-driver mechanism and not a PowerPC CPU JIT.

Apple documents `MAP_JIT` and the JIT entitlement as writable/executable-memory mechanisms, and notes that `pthread_jit_write_protect_np` is unavailable on iOS.[16][17] That would be a severe issue for a real runtime CPU JIT; it is not required by this AOT architecture.

## 11. Required platform-layer changes

### 11.1 Platform identity and build selection

- In `glue/rexglue-sdk-main/include/rex/platform.h`, test `TARGET_OS_IOS`/`TARGET_OS_IPHONE` before the broad Apple/macOS condition and define coherent `REX_PLATFORM_IOS`, `REX_PLATFORM_POSIX`, and ARM64 flags.
- In the SDK and root CMake, give iOS an explicit platform name/source branch. Do not let `if(APPLE)` imply AppKit.
- Add separate `iphoneos-arm64` and `iphonesimulator-arm64` presets. Simulator success must not satisfy device-only gates.
- Separate build-host tools (`rexglue`, XenosRecomp, shader compilers/validators) from target libraries. Cross-compilation must invoke prebuilt macOS host tools, never iOS executables.
- Remove configure-time game-file requirements from the normal product target. Keep an explicit developer-only embedded-payload option if useful.

### 11.2 `UIApplication` and scene lifecycle

Theft4 should own the scene-based lifecycle. That is increasingly important on current SDKs; Apple states that apps built with the latest iOS 27 SDK must adopt scenes to launch.[18]

Required engine transitions:

```text
Created -> Configured -> SurfaceAttached -> Initializing -> Running
                                      |          |           |
                                      +------> Paused <-------+
                                                 |
                                            Stopping
                                                 |
                                              Stopped
```

- `sceneWillResignActive`: stop accepting gameplay input and request a safe guest pause.
- `sceneDidEnterBackground`: stop drawable acquisition/submission, flush saves, quiesce audio/network work, release discardable GPU/cache memory.
- `sceneWillEnterForeground`: rebuild transient surface/audio resources as required.
- `sceneDidBecomeActive`: resume only after the surface and audio route are valid.
- memory warning: trim shader/resource caches and report current resident/guest allocation metrics.
- scene disconnect/termination: bounded stop and save flush; callbacks must not target deallocated UIKit objects.

The current synchronous `main` cannot simply block the UIKit main thread. SDL3's iOS main/callback support can bootstrap an initial standalone bundle, but the production architecture should expose lifecycle calls to the host rather than make SDL the owner of Theft4.[11]

### 11.3 View and rendering surface

- Theft4 owns `UIWindow`, `UIViewController`, and a `UIView` whose layer is `CAMetalLayer`.
- The bridge attaches/detaches the opaque layer on the main thread and passes pixel dimensions, content scale, color/HDR preference, and maximum refresh rate to the engine.
- The renderer retains only the documented lifetime necessary to present. It must tolerate the layer/drawable becoming unavailable.
- Remove `CGGetActiveDisplayList`, `NSWindow`, `NSScreen`, Cocoa SDL properties, desktop fullscreen/position/minimum-size behavior, and IOKit display enumeration from the iOS branch.

### 11.4 Input

Recommended order:

1. SDL3 controller input for fastest consistency with existing HID code.
2. Repair/wire the native GameController driver only where it adds motion, virtual controller, battery/light, or haptic capabilities.
3. Add touch through a platform-neutral event queue and the existing GTA IV touch-coordinator concepts.
4. Treat keyboard/mouse as optional iPad features; SDL notes that indirect input and focus behavior vary by iOS version.[11]

The input boundary should carry normalized controller state, stable touch IDs, pixel/normalized coordinates, timestamp, phase, and cancellation. Backgrounding must synthesize release/cancel for every active control.

### 11.5 Audio

- Keep the ReXGlue SDL audio backend initially; do not select the macOS CoreAudio implementation.
- Add a thin Objective-C++ `AVAudioSession` owner controlled by Theft4: category/mode/options, activation, interruption notifications, route changes, media-service resets, and foreground/background policy.
- Use an iOS static FFmpeg/libavcodec build or remove the dependency from the initial smoke target. The existing macOS prebuilt wrapper is unusable.
- Enable microphone/voice only after playback is stable; add privacy text and permission flow at that milestone. Apple documents that `AVAudioSession` communicates the app's audio intent and posts route-change events that applications must handle.[19][20]

### 11.6 Filesystem and game import

Recommended layout:

```text
<App Container>/Library/Application Support/Theft4/
  Games/<installation-id>/       immutable imported game/XEX/RPF/DLC data
  Saves/<installation-id>/       durable save data
  Config/                        durable settings and installation metadata
  Logs/                          bounded diagnostic logs
<App Container>/Library/Caches/Theft4/
  Shaders/                       reproducible/discardable pipeline caches
  Extraction/                    resumable temporary products
<App Container>/tmp/             transactional import staging
<App Container>/Documents/       only files intentionally exported/user-visible
<App Bundle>/                    signed code and app-owned immutable resources only
```

Import should use `UIDocumentPicker` or an appropriate document-browser flow, acquire security-scoped access only while copying, validate required filenames/version/hashes, copy transactionally to a staging directory, then atomically publish the installation. Do not run the desktop installer against the read-only app bundle or mutate the user's source directory. Apple directs iOS apps to use the app container and reserve Documents for user documents.[14]

The engine should receive explicit immutable and writable roots. It should not infer them from current working directory, executable path, UIKit, or a global “Documents/LibertyRecomp” convention.

### 11.7 Memory mapping and executable memory

Build a small device-only probe before game startup that performs exactly the runtime's operations:

1. Query actual page size and allocation granularity.
2. Reserve a contiguous range matching `map_info` through `0x11fffffff`.
3. Create the 4.5 GiB sparse backing object.
4. Map every virtual/physical alias with the exact offsets and lengths.
5. Write through one alias and verify reads through all corresponding aliases.
6. Exercise reserve/commit/decommit/protect boundaries around 4 KiB guest pages on 16 KiB hosts.
7. Validate the `0xE...` `0x1000` host offset in every translation path.
8. Trigger and recover a controlled no-access/MMIO fault.
9. Measure virtual size, resident growth, physical commitment, and jetsam behavior.
10. Assert that no mapping is executable and no `MAP_JIT` is used.

The dormant `memory_ios.cpp` should not be merged wholesale. Its statements about page size, shared-memory scoping, PAC, and API availability are hypotheses until tested. `QueryProtect` is a stub, and mapping correctness also depends on `xmemory.cpp` choosing an iOS/Darwin contiguous-map route rather than the generic address-guess loop.

### 11.8 Threading, synchronization, fibers, and exceptions

- Audit every result from `pthread_*`, semaphore, scheduling, cancel, and signal calls; do not assume Linux semantics.
- Avoid `SCHED_FIFO` as a requirement. Map guest priority to advisory QoS conservatively.
- Replace unsafe forced cancellation/suspension with cooperative safepoints where possible.
- Update the iOS fiber implementation to the current `Fiber` ABI and prove preservation of callee-saved integer/SIMD state, TLS, stack alignment, C++ unwinding boundaries, and arm64e behavior.
- Run stress tests for events, semaphores, timers, APC delivery, suspend/resume, and shutdown while backgrounding.
- Decide whether Darwin signals are sufficient for controlled guest memory faults or whether a carefully isolated Mach exception approach is required. The engine must coexist with crash reporting and never throw unsafely across an arbitrary signal boundary.
- Keep guest exceptions separate from host crashes. The upstream generated exception/unwind path is incomplete and should be reported explicitly rather than masked.

### 11.9 Networking

- Disable community multiplayer, update checking, voice, CURL, and GameNetworkingSockets for the first runtime and rendering milestones.
- Retain local BSD sockets only if required for core initialization.
- Later, cross-build each network dependency as a static iOS library, define cancellation/background behavior, and add local-network/microphone disclosures only for enabled features.
- Do not run `libserver` in the app process as a substitute for a supported network service design.

### 11.10 Entitlements and distribution

- No JIT, unsigned executable memory, disabled library validation, or child-process entitlement is required or recommended.
- Game Center entitlement is optional until GameKit integration is tested with the Theft4 bundle ID.
- Controller capability and local-network/microphone descriptions should reflect actual features.
- Background modes should not be used to keep gameplay running; pause promptly.
- All executable engine/plugin code must be statically linked or packaged as signed embedded frameworks. Imported files remain non-executable data.

## 12. Liberty Bridge / Theft4 integration architecture

### 12.1 Choice among A–D

- **A, directly embed libraries/frameworks:** yes—use static libraries or a static XCFramework.
- **B, launch a separate runtime:** no—unnecessary, unsupported by the current iOS process adapter, and a poor lifecycle/distribution fit.
- **C, frontend owns lifecycle and LibertyRecomp is the engine:** yes—this is the primary ownership rule.
- **D, other:** use a narrow versioned C ABI and Objective-C++ implementation so Swift never imports ReXGlue/Plume types.

Thus the answer is **A + C, implemented through D's façade technique**.

```text
Theft4 Swift/UIKit
  - scenes/windows/views
  - import & validation UI
  - installation/settings/save/log UI
  - lifecycle and permissions
          |
          | versioned C ABI, async callbacks, opaque handle
          v
Theft4Engine bridge (Objective-C++)
  - state machine / thread marshaling
  - immutable config and owned callbacks
  - CAMetalLayer attach/detach
          |
          v
LibertyRecomp + ReXGlue runtime + generated AOT game code
  - guest memory/kernel/VFS
  - Plume Metal renderer
  - SDL audio/input adapters
          |
          v
iOS adapters
  - paths/logging/faults/threads/fibers
  - Metal / AVAudioSession / GameController / BSD sockets
```

### 12.2 Proposed stable ABI

The exact spelling can change, but the boundary should resemble:

```c
typedef struct theft4_engine theft4_engine_t;

typedef struct {
  uint32_t struct_size;
  uint32_t api_version;
  const char *game_root_utf8;
  const char *save_root_utf8;
  const char *cache_root_utf8;
  const char *log_root_utf8;
  void *callback_context;
  void (*on_event)(void *context, const theft4_event_t *event);
} theft4_engine_config_t;

theft4_result_t theft4_engine_create(
    const theft4_engine_config_t *config, theft4_engine_t **out_engine);
theft4_result_t theft4_engine_attach_metal_layer(
    theft4_engine_t *engine, void *ca_metal_layer,
    uint32_t pixel_width, uint32_t pixel_height, float scale);
theft4_result_t theft4_engine_start(theft4_engine_t *engine);
void theft4_engine_pause(theft4_engine_t *engine);
void theft4_engine_resume(theft4_engine_t *engine);
void theft4_engine_memory_warning(theft4_engine_t *engine);
void theft4_engine_detach_surface(theft4_engine_t *engine);
void theft4_engine_stop(theft4_engine_t *engine);
void theft4_engine_destroy(theft4_engine_t *engine);
```

Additional APIs should submit normalized input, report validation/import progress, request save flush, and export bounded diagnostics. Configuration structs need `struct_size` and version fields. Callbacks must be copied/owned, serialized, never invoked after destroy, and documented as main-queue or engine-queue callbacks.

Do not expose:

- `rex::Runtime*`, `PPCContext`, guest addresses, Plume objects, SDL windows, or internal paths to Swift;
- synchronous UIKit dialog callbacks from guest threads;
- mutable global configuration read directly by UI code.

### 12.3 Responsibility boundary

| Theft4 owns | Engine owns |
|---|---|
| UIKit lifecycle, windows, view/layer | Guest lifecycle and safe pause/resume |
| User file selection and lawful-use messaging | Format/version validation logic callable by host |
| Transactional copy into sandbox | VFS mounts over supplied roots |
| Installation catalog and selection | XEX/RPF/module loading |
| Settings UI and persistence | Applying a validated immutable run config |
| Controller/touch presentation | Mapping normalized input to guest state |
| Permission prompts | Audio/input/network service requests via callbacks |
| Save browsing/export/import UI | Save serialization and flush |
| Log/crash presentation/export | Structured engine logs, trace points, fatal status |
| Surface creation/destruction | Metal device/render resources and frame submission |

### 12.4 Why this minimizes coupling

The app can be renamed, redesigned, or rewritten without exposing internal engine headers. The engine can later switch from Plume to Graine/MoltenVK without changing Swift if the surface/input/lifecycle ABI stays stable. It also gives tests a headless host and prevents the runtime from assuming desktop current-working-directory, dialogs, processes, or app ownership.

## 13. Dependency risks

| Dependency/component | Current role | iOS risk | First-pass disposition |
|---|---|---|---|
| ReXGlue/Graine core | CPU/runtime/kernel/services | Platform regression; stale iOS primitives; shared targets | Repair only the core path needed by the legacy iOS consumer; static link. |
| Generated GTA IV C++ | AOT title code | Huge object/link time, dead stripping, ARM64 conformance | Keep checked in; compile/link; force registration retention; do not regenerate for ARM64. |
| SDL3 | Window/input/audio | Startup/lifecycle ownership and duplicate GameController paths | Reuse; integrate supported iOS main for smoke app, then host-owned lifecycle. |
| Plume | Current iOS renderer | AppKit adapter, device enumeration, target shaders | Preferred initial renderer; add narrow iOS helper. |
| XenosRecomp | Host shader translator | Target-unaware AIR build; large cache regeneration | Keep host-only; add explicit Apple SDK/target metadata. |
| MoltenVK | Modern macOS Vulkan translation | Project wrapper is macOS-only; static/plugin redesign | Strategic fallback/convergence experiment, not initial critical path. |
| Native File Dialog | Desktop installer | AppKit-only | Exclude on iOS; use Theft4 import UI. |
| FFmpeg/libav | XMA/media decoding | macOS prebuilts; assembly and wrong platform slices | Produce iOS static build or isolate until M9. |
| GameNetworkingSockets | Multiplayer | OpenSSL/protobuf/abseil, ICE, background/LAN/privacy | Exclude until after single-player; integrate separately. |
| CURL/OpenSSL | Updates/community/crypto | Host discovery and platform build | Exclude optional services; use platform networking or static iOS builds later. |
| Freetype/msdf | UI/font assets | iOS omitted from source fallback | Build as target dependency or pre-generate required atlases. |
| Tracy | Profiling | Shared library and background overhead | Disable for initial device target; use signposts/Instruments first. |
| ImGui | Engine/debug UI | Input/lifecycle and touch usability | Keep only debug overlays required for bring-up. Product UI stays UIKit. |
| GameKit | Achievements | Entitlement/bundle configuration | Defer; no-op behind host capability initially. |
| MetalFX | Optional upscaling | Link/availability/device behavior | Disable until baseline performance work. |
| `libserver` | Community backend | Not an embedded-engine responsibility | Build/run separately; exclude from iOS target. |

## 14. Recommended development order

The order is deliberately risk-first:

1. Preserve and publish M0 desktop/build evidence.
2. Make a minimal iOS static compile graph, excluding optional desktop services.
3. Launch a tiny Theft4/SDL UIKit shell and call one engine symbol.
4. Prove guest address space, aliases, protection/fault handling, fibers, threads, and clean shutdown on a physical device.
5. Bring up Plume Metal against a host-owned `CAMetalLayer` and present a clear frame.
6. Link/register and invoke generated AOT code.
7. Import/mount lawful game files from the sandbox.
8. Add controller/touch and audio independently.
9. Advance through deterministic game checkpoints.
10. Harden the stable frontend/engine contract, performance, lifecycle, and distribution.

Do **not** begin by fixing every iOS compile error, re-enabling multiplayer, rewriting the renderer, or coupling Swift directly to ReXGlue. Each milestone should enable only the dependencies it owns.

## 15. Milestones

The concrete milestone plan is in `LIBERTYRECOMP_IOS_PLAN.md`. Its critical gates are:

| Gate | Milestone | Question answered |
|---|---|---|
| G0 | M0 | Can the pinned public checkout reproduce the upstream host build or a stable baseline failure? |
| G1 | M1 | Can a clean device/simulator iOS graph compile without desktop APIs/binaries? |
| G2 | M3 | Does the real iPhone support the guest memory/fault/fiber/thread model without executable memory? |
| G3 | M4–M5 | Can Plume initialize required Metal capabilities and present safely through UIKit lifecycle? |
| G4 | M6 | Does a linked generated `PPCFunc` dispatch correctly as signed AOT ARM64 code? |
| G5 | M7 | Can an imported, matching lawful installation load through sandbox/VFS paths? |
| G6 | M10 | Can the title reach a repeatable meaningful state within memory/thermal budgets? |
| G7 | M12 | Can the product sustain lifecycle, performance, stability, privacy, signing, and distribution constraints? |

Failure at G2 is a potential architecture redesign. Failure at G3 may justify the Graine/MoltenVK convergence route. Ordinary compile errors are not reasons to bypass either gate.

## 16. Estimated difficulty by subsystem

Scale: 1 = routine, 3 = substantial but bounded, 5 = high-risk research/port.

| Subsystem | Difficulty | Why |
|---|---:|---|
| Build-host setup and reproducibility | 2/5 | Pinned setup exists; host dependencies and shader tool crash need cleanup. |
| iOS CMake/Xcode graph | 5/5 | Platform regression plus many transitive desktop assumptions and host/target tool separation. |
| UIKit shell and C ABI | 3/5 | Straightforward APIs, but lifecycle/thread ownership must be correct from the start. |
| Guest memory mapping | 5/5 | 4.5 GiB layout, aliases, fixed maps, 16 KiB protection granularity, jetsam uncertainty. |
| Fault/MMIO/write-watch handling | 5/5 | Signal/Mach semantics, crash-handler coexistence, async-safety. |
| Fibers/thread suspension | 5/5 | Stale implementation, full context preservation, PAC, cancellation/background behavior. |
| Generated AOT ARM64 execution | 4/5 | Design is portable; size, FP/vector/alignment and dispatch coverage need proof. |
| Plume/UIKit Metal adapter | 3/5 | Concentrated adapter/device/lifecycle changes. |
| iPhoneOS shader regeneration | 5/5 | Large cached corpus, host tooling/target metadata, validation and missing-shader behavior. |
| MoltenVK strategic alternative | 5/5 | Modern app port, static plugin conversion, feature/format/performance audit. |
| Filesystem/import/VFS | 4/5 | Large user data, transactional copies, sandbox paths, version validation. |
| Controller input | 3/5 | SDL3 is available; native iOS work is unwired and duplicate paths need policy. |
| Touch UI | 4/5 | Mapping is easy; a usable game control scheme, safe areas, gestures, cancellation are not. |
| Audio playback | 4/5 | Decoder dependency plus AVAudioSession interruption/route lifecycle. |
| Networking/voice | 5/5 | Large third-party graph, privacy, backgrounding, multiplayer correctness. |
| Saves/config | 3/5 | Existing logic, but atomicity, lifecycle flush, and path separation need tests. |
| Performance/thermal/memory | 5/5 | Title scale, shader compilation, 512 MiB physical backing and mobile sustained limits. |
| App distribution/compliance | 4/5 | AOT helps; content rights, static code, privacy/capabilities and large imported assets remain. |

Overall port difficulty: **5/5**. This is a real platform port, not an Xcode packaging exercise.

## 17. Exact files/directories that will require work

This is the current expected edit surface, ordered by priority. It is intentionally narrower than “all Apple references.” Vendored test/sample references that are not built do not require changes.

### P0 — platform-conformance and clean compile

- `CMakeLists.txt`
- `CMakePresets.json`
- `toolchains/ios.cmake`
- `glue/CMakeLists.txt`
- `glue/rexglue-sdk-main/CMakeLists.txt`
- `glue/rexglue-sdk-main/include/rex/platform.h`
- `glue/rexglue-sdk-main/src/core/CMakeLists.txt`
- `glue/rexglue-sdk-main/src/core/memory_ios.cpp`
- `glue/rexglue-sdk-main/src/core/fiber_ios.cpp`
- `glue/rexglue-sdk-main/src/core/dynlib_ios.cpp`
- `glue/rexglue-sdk-main/src/core/filesystem_ios.mm`
- `glue/rexglue-sdk-main/src/core/logging_ios.mm`
- `glue/rexglue-sdk-main/src/core/net_ios.cpp`
- `glue/rexglue-sdk-main/src/core/{threading_mac.cpp,threading_posix.cpp,exception_handler_mac.cpp,mapped_memory_mac.cpp}` or new iOS-specific counterparts
- `glue/rexglue-sdk-main/include/rex/thread/fiber.h`
- `glue/rexglue-sdk-main/include/rex/system/xmemory.h`
- `glue/rexglue-sdk-main/src/system/{xmemory.cpp,runtime.cpp,xthread.cpp,function_dispatcher.cpp}`
- `glue/rexglue-sdk-main/resources/templates/codegen/init_h.inja`
- `glue/rexglue-sdk-main/gta4-recomp/generated/gta4_init.h` only via regeneration/template correction, not hand-editing
- `thirdparty/CMakeLists.txt`
- `LibertyRecompLib/CMakeLists.txt`
- `LibertyRecomp/CMakeLists.txt`
- `LibertyRecomp/main.cpp`

### P1 — UIKit shell, lifecycle, surface, and renderer

- New `ios/Theft4/` application shell (or equivalent user-owned frontend project)
- New public `theft4_engine.h` plus Objective-C++ implementation/module map
- `LibertyRecomp/res/ios/{iOSBundleInfo.plist.in,LibertyRecomp.entitlements,Assets.xcassets,LaunchScreen.storyboard}` or replacement Theft4-owned resources
- `LibertyRecomp/ui/{game_window.cpp,game_window.h}`
- `LibertyRecomp/gpu/{video.cpp,video.h}`
- `thirdparty/plume/CMakeLists.txt`
- `thirdparty/plume/{plume_apple.mm,plume_apple.h,plume_metal.cpp,plume_metal.h,plume_render_interface_types.h}`
- New `thirdparty/plume/plume_ios.mm`/`.h` or an upstreamable equivalent
- `LibertyRecomp/gpu/shader/msl/`
- `tools/XenosRecomp/XenosRecomp/air_compiler.cpp`
- `LibertyRecompLib/shader/{shader_cache.cpp,shader_cache.h}` via host regeneration, not manual binary edits

### P2 — app services

- `LibertyRecomp/install/{embedded_assets.cpp,platform_paths.cpp,platform_paths.h}`
- `LibertyRecomp/user/{paths.cpp,paths.h,config.cpp}`
- `LibertyRecomp/os/ios/ios_paths_objc.mm`
- `LibertyRecomp/os/ios/{logger_ios.cpp,media_ios.cpp,user_ios.cpp,version_ios.cpp,process_ios.cpp}`
- `glue/rexglue-sdk-main/src/filesystem/`
- `glue/rexglue-sdk-main/src/system/{guest_path.cpp,xfile.cpp}`
- `glue/rexglue-sdk-main/src/input/CMakeLists.txt`
- `glue/rexglue-sdk-main/src/input/input_system.cpp`
- `glue/rexglue-sdk-main/src/input/ios/{ios_input_driver.h,ios_input_driver.mm}`
- `LibertyRecomp/hid/driver/sdl_hid.cpp`
- `LibertyRecomp/os/ios/{haptics_ios,adaptive_triggers_ios}.{h,mm}`
- `glue/rexglue-sdk-main/src/audio/CMakeLists.txt`
- `glue/rexglue-sdk-main/src/audio/sdl/`
- New iOS `AVAudioSession` adapter
- `thirdparty/ffmpeg-core/` or a replacement target integration
- `LibertyRecomp/os/gamecenter/achievement_bridge_gc.mm`
- `glue/rexglue-sdk-main/src/core/keyboard_dialog_ios.mm` (prefer replacement by async host callback)

### P3 — deferred optional systems

- `thirdparty/GameNetworkingSockets/` integration and its OpenSSL/protobuf/abseil configuration
- `LibertyRecomp/network/`
- `glue/rexglue-sdk-main/gta4-recomp/src/network/`
- GameKit/voice/microphone/local-network plist and entitlements
- `glue/rexglue-sdk-main/src/graphics/`, `src/ui/`, `src/system/gpu_plugin_loader.cpp`, and `thirdparty/MoltenVK/CMakeLists.txt` only if migrating to modern Graine/MoltenVK
- `libserver/` only for external service compatibility, never the initial embedded target

### Files that should not be churned

- Do not manually modify all 84 `generated/gta4_recomp.*.cpp` files.
- Do not rewrite platform-independent kernel/VFS/render code until a failing test proves it necessary.
- Do not modify vendored SDL3 UIKit internals unless an upstream SDL defect is isolated.
- Do not add game files, generated native binaries from unverified sources, or private SDK material to the repository.

## 18. Unknowns that require experiments

| Priority | Experiment | Success signal | Consequence of failure |
|---:|---|---|---|
| 1 | Physical-device 4.5 GiB reserve + eight guest-visible views and one raw-physical view | Exact addresses/aliases work repeatedly; low resident cost before touch | Redesign guest address translation/memory layout. |
| 2 | 512 MiB physical precommit and jetsam profile | Fits supported devices with acceptable headroom | Lazy/segmented physical commitment and GPU upload redesign. |
| 3 | 16 KiB protection/`0xE...` offset test | All guest page operations and aliases preserve bytes/protection | Fix allocator/protection granularity and regenerate access macros. |
| 4 | Controlled MMIO/write-watch fault recovery | Handler identifies access, advances/resumes correctly, coexists with crash capture | Explicit bounds/MMIO instrumentation or Mach exception redesign. |
| 5 | Fiber context test under arm64/arm64e | Integer, FP/SIMD, stack, TLS and repeated switches remain exact | New assembly/context implementation or scheduling model. |
| 6 | Thread/APC/suspend/background stress | No deadlock/cancel corruption over thousands of cycles | Cooperative safepoints and host-thread model changes. |
| 7 | One generated AOT function on device | Registered address calls signed native symbol and produces expected guest-state delta | Isolate ARM64 codegen/ABI/FP/alignment issue. |
| 8 | Plume `CAMetalLayer` clear frame | Device/queue/swapchain survive resize and 20 lifecycle cycles | Repair adapter; if fundamental capability gap, assess MoltenVK. |
| 9 | Helper and guest shader regeneration for `iphoneos` | Every artifact reports correct target and validates/loads | Repair AIR pipeline or choose SPIR-V/MoltenVK migration. |
| 10 | Plume format/synchronization matrix | All title-required formats and primitives have correct Metal mappings | Implement missing mappings or pivot renderer lineage. |
| 11 | Static MoltenVK feature probe | Modern renderer-required Vulkan features/formats supported | Confirms Plume remains the only practical route or requires renderer changes. |
| 12 | XMA/FFmpeg device fixture | Correct frames/channels/rate with static iOS libraries | Decoder dependency replacement or native path. |
| 13 | Large transactional import | Interruption/resume, storage errors, hashes and path semantics are safe | Redesign importer/staging/format support. |
| 14 | Cold boot checkpoints | Deterministic progress through XEX entry, archives, legal screen/menu | Attribute failure to runtime, VFS, renderer, or missing service using trace gates. |
| 15 | Sustained performance/thermal run | Frame pacing, RSS and thermals within written device budgets | Resolution/quality/cache reductions or supported-device floor change. |

The first six experiments should precede any promise of a playable port. They are small compared with the full game and answer the questions that source inspection cannot.

## Appendix A — build-validation record

### Host and tools

| Item | Recorded value |
|---|---|
| Host | macOS 27.0 (`26A428`), Darwin 27.0.0, Apple Silicon arm64 |
| Repository | `36b729dcc166910c88a2960164707ba9d9e5138c` (“Fix macOS build targeting and portable app packaging”) |
| Active default developer directory | `/Library/Developer/CommandLineTools` |
| Full Xcode used explicitly | `/Applications/Xcode.app`, Xcode 27.0 (`27A266a`) |
| SDKs | iPhoneOS 27.0, iPhoneSimulator 27.0, macOS 27.0 |
| Compiler | Apple clang 21.0.0 (`clang-2100.3.34.2`) |
| Python | 3.10.7 |
| CMake | 4.4.3, installed user-local for the audit; project minimum 3.29 |
| Ninja | 1.13.2, installed user-local for the audit |
| Missing documented host packages | Homebrew command, pkg-config, Homebrew LLVM, OpenSSL 3 development package, Vulkan loader/SDK |

### Checkout and setup

Commands:

```bash
git clone --recurse-submodules https://github.com/OZORDI/LibertyRecomp.git
cd LibertyRecomp
python3 tools/setup_repo.py
python3 tools/setup_repo.py --check
```

The first network operations failed inside the restricted audit sandbox and succeeded when retried with network access. That is an audit-environment issue, not a repository failure. Setup ultimately reported all dependencies and patches ready. Six dependency submodules remain modified because the project's reviewed patches are applied there by design.

### Documented macOS baseline

The documented configure command was attempted first with full Xcode selected:

```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  cmake --preset macos-release
```

The first repository-relevant failure was:

```text
Could NOT find OpenSSL
(missing: OPENSSL_CRYPTO_LIBRARY OPENSSL_INCLUDE_DIR Crypto)
Required is at least version "3.0"
```

The host lacked the Homebrew prerequisites listed by `docs/BUILDING.md`. To trace later failures without claiming a clean supported build, a diagnostic configure pointed CMake at an unrelated pre-existing ARM64 OpenSSL 3.6.2 development tree. Configure then succeeded with a warning that no macOS Vulkan runtime was found.

The final reproducible diagnostic configure command was:

```bash
env PATH=/path/to/developer/Library/Python/3.10/bin:/usr/bin:/bin:/usr/sbin:/sbin \
  DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  /path/to/developer/Library/Python/3.10/bin/cmake \
  --preset macos-release \
  -DCMAKE_MAKE_PROGRAM=/path/to/developer/Library/Python/3.10/bin/ninja \
  -DOPENSSL_ROOT_DIR=/path/to/developer/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/poppler/poppler \
  -DGTA4_NATIVE_SPIRV_VAL=/path/to/developer/LibertyRecomp/out/host-spirv-tools/tools/spirv-val
```

This is evidence-gathering only: the OpenSSL tree belongs to another local tool runtime, is not a declared LibertyRecomp dependency, and is not a recommended project setup. The notable configure diagnostics were the version fallback (`no v* tag reachable from HEAD`), missing optional PkgConfig and LibUSB, deprecated CMake compatibility/policy warnings in glslang/SPIRV-Tools, and exactly:

```text
No macOS Vulkan runtime was found. Install the LunarG Vulkan SDK or set
REXGLUE_VULKAN_RUNTIME_DIR/REX_VULKAN_SDK to a Vulkan runtime root.
```

The initial diagnostic build command was:

```bash
cmake --build --preset macos-release --target LibertyRecomp --parallel 2
```

It initially reached target step 845/1109 and stopped because `spirv-val` was required by a nonempty shader-override manifest. A host `spirv-val` was then built from the repository's pinned public SPIRV-Tools sources and passed as `GTA4_NATIVE_SPIRV_VAL`. The resumed build stopped in the hash-specific shader-override compiler on:

```text
LibertyRecompLib/shader_overrides/bink/ps_bink.hlsl
```

The build wrapper uses a random temporary output path. The following equivalent stable-output reproduction was run verbatim and exited **139** with empty stdout/stderr and no output file:

```bash
env DYLD_LIBRARY_PATH=/path/to/developer/LibertyRecomp/tools/XenosRecomp/thirdparty/dxc-bin/lib/arm64 \
  /path/to/developer/LibertyRecomp/tools/XenosRecomp/thirdparty/dxc-bin/bin/arm64/dxc-macos \
  -spirv -fspv-target-env=vulkan1.0 -HV 2021 -T ps_6_0 -E shaderMain \
  -all-resources-bound -fvk-use-dx-layout -O3 -Qstrip_debug -WX \
  -I /path/to/developer/LibertyRecomp/LibertyRecompLib/shader_overrides \
  -I /path/to/developer/LibertyRecomp/LibertyRecompLib/shader_overrides/bink \
  -DXENOS_RECOMP_PIXEL_SHADER -DGTA4_RECOMP \
  -Fo /path/to/developer/LibertyRecomp/out/dxc-ps-bink.spv \
  /path/to/developer/LibertyRecomp/LibertyRecompLib/shader_overrides/bink/ps_bink.hlsl
```

`dxc --version` itself works. The graphics CMake comments already warn that the bundled DXC 1.8 crashes in SPIRV-Tools AggressiveDCE for the GTA IV Bink shaders and prefers a newer installed DXC. No app bundle was produced, and a filesystem check found **zero** compiled `gta4_recomp.*.cpp.o` files.

Observed nonfatal build warnings included `fpscr.h:78:93: warning: value size does not match register size specified by the constraint and modifier` (with Clang recommending `%w0`), deprecated Darwin `ucontext` use, `xenos.h:1621:70: warning: unused parameter 'one_reg'`, and a linker warning that duplicate static libraries and `-lpthread` were ignored. The missing Vulkan runtime would also prevent a functional modern macOS launch even if compilation completed.

### iOS baseline

The documented preset was attempted without source changes:

```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  cmake --preset ios-release
```

Failure sequence:

1. With the repository defaults, CMake stops because `tools/local_game_payload` does not exist. The referenced `tools/local_game_payload/README.md` is also absent.
2. To test configuration without proprietary data, four ignored text marker files named `default.xex`, `common.rpf`, `xbox360.rpf`, and `audio.rpf` were created under `out/synthetic_game_payload`. They contain no game bytes and only satisfy CMake's `EXISTS` checks.
3. With that diagnostic path, AppleClang 21 and arm64 are detected, then configure stops at `thirdparty/msdf-atlas-gen/msdfgen/CMakeLists.txt:133`: `Could NOT find Freetype`.
4. An ignored configuration-only top-level include defined a dummy interface `Freetype::Freetype` to reveal the next gate. It cannot link an app. CMake then reports `nfd Platform: PLATFORM_MACOS`, enables Plume Vulkan+Metal, and fails at `thirdparty/MoltenVK/CMakeLists.txt:28` because AppKit does not exist in the iPhoneOS SDK.
5. A further configure-only framework substitution exposed `Could NOT find OpenSSL` in GameNetworkingSockets. This confirms multiple independent desktop dependency gates.
6. Because generation never completes, `LibertyRecomp-ALL.xcodeproj` does not exist and no iOS target can be built.

The payload-marker configure was:

```bash
env PATH=/path/to/developer/Library/Python/3.10/bin:/usr/bin:/bin:/usr/sbin:/sbin \
  DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  /path/to/developer/Library/Python/3.10/bin/cmake \
  --preset ios-release \
  -DLIBERTY_RECOMP_EMBEDDED_GAME_PATH=/path/to/developer/LibertyRecomp/out/synthetic_game_payload
```

The Freetype bypass used the same command plus `-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=/path/to/developer/LibertyRecomp/out/ios_config_probe.cmake`. That ignored probe defines only an empty `Freetype::Freetype` interface target; it cannot produce a linkable application and was used solely to expose the next deterministic CMake failure.

The initial compiler probe also failed when run inside a restricted sandbox because Xcode could not use its normal caches. Re-running the same command outside that sandbox reached the deterministic repository failures above. That sandbox artifact is not classified as an iOS source defect.

### Baseline conclusion

Neither macOS nor iOS produced a runnable application on this host. The macOS failure is presently a host prerequisite plus reproducible bundled shader-tool issue after substantial compilation. The iOS failure is architectural/build-graph contamination before project generation. No source “fixes” were made during this reconnaissance.

## Appendix B — key documentation discrepancies

| Documentation claim | Code/build evidence |
|---|---|
| iOS uses SDL2 | Build uses vendored SDL3; SDL2 app-side targets are disabled. |
| `scripts/ios-setup.sh` is the recommended path | The file/directory is absent. |
| `tools/local_game_payload/README.md` explains layout | The referenced file is absent. |
| Simulator works without signing | No simulator preset; toolchain hardcodes `iphoneos`. |
| iOS has no installer and reads bundle game files | Installer/update paths are not consistently excluded; platform macro failure bypasses read-only behavior. |
| Saves go to Documents and logging uses `os_log` | Code exists, but iOS macro/source selection prevents the advertised path from being reliably active. |
| Metal uses the same path as macOS | Current macOS returns early to Graine/Vulkan/MoltenVK; iOS falls through to the legacy consumer, prefers Plume Metal, and still incorrectly retains Vulkan/MoltenVK fallback build baggage. |
| README codegen function count | Measured generated registration count is 38,606, not the older documented count. |

## Sources

1. [Generated AOT ABI, memory access, and function registration](https://github.com/OZORDI/LibertyRecomp/tree/36b729dcc166910c88a2960164707ba9d9e5138c/glue/rexglue-sdk-main/gta4-recomp/generated)
2. [ReXGlue runtime memory, dispatch, XEX, and thread implementation](https://github.com/OZORDI/LibertyRecomp/tree/36b729dcc166910c88a2960164707ba9d9e5138c/glue/rexglue-sdk-main/src/system)
3. [Root build selection, presets, and iOS toolchain](https://github.com/OZORDI/LibertyRecomp/tree/36b729dcc166910c88a2960164707ba9d9e5138c)
4. [ReXGlue platform and dormant iOS core implementations](https://github.com/OZORDI/LibertyRecomp/tree/36b729dcc166910c88a2960164707ba9d9e5138c/glue/rexglue-sdk-main/src/core)
5. [Modern GTA IV consumer CMake](https://github.com/OZORDI/LibertyRecomp/blob/36b729dcc166910c88a2960164707ba9d9e5138c/glue/rexglue-sdk-main/gta4-recomp/CMakeLists.txt)
6. [Legacy application renderer and host](https://github.com/OZORDI/LibertyRecomp/tree/36b729dcc166910c88a2960164707ba9d9e5138c/LibertyRecomp)
7. [Intended iOS memory backend](https://github.com/OZORDI/LibertyRecomp/blob/36b729dcc166910c88a2960164707ba9d9e5138c/glue/rexglue-sdk-main/src/core/memory_ios.cpp)
8. [Official repository building guide](https://github.com/OZORDI/LibertyRecomp/blob/36b729dcc166910c88a2960164707ba9d9e5138c/docs/BUILDING.md)
9. [ReXGlue code-generation implementation](https://github.com/OZORDI/LibertyRecomp/tree/36b729dcc166910c88a2960164707ba9d9e5138c/glue/rexglue-sdk-main/src/codegen)
10. [Commit replacing the previous ReXGlue tree with Graine](https://github.com/OZORDI/LibertyRecomp/commit/a1d3a84d9367db654a690e2e10d1ca450f95f355)
11. [SDL3 iOS documentation](https://wiki.libsdl.org/SDL3/README-ios)
12. [MoltenVK runtime guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md)
13. [Apple App Review Guidelines](https://developer.apple.com/app-store/review/guidelines/)
14. [Apple: Files and directories](https://developer.apple.com/documentation/technologyoverviews/files-and-directories)
15. [Apple: Managing your app's life cycle](https://developer.apple.com/documentation/uikit/managing-your-app-s-life-cycle)
16. [Apple: Allow execution of JIT-compiled code entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.allow-jit)
17. [Apple: Porting JIT compilers to Apple silicon](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon)
18. [Apple: Transitioning to the UIKit scene-based life cycle](https://developer.apple.com/documentation/uikit/transitioning-to-the-uikit-scene-based-life-cycle)
19. [Apple: AVAudioSession](https://developer.apple.com/documentation/avfaudio/avaudiosession)
20. [Apple: Responding to audio route changes](https://developer.apple.com/documentation/avfaudio/responding-to-audio-route-changes)
