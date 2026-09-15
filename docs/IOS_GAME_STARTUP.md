# Theft4: real AOT startup bring-up

Development sessions: 2026-09-14 through 2026-09-15. This extends the earlier
loader-only result in [IOS_APP_BUILD.md](IOS_APP_BUILD.md). XeniOS is a separate
reference project and is not embedded here.

For a fresh public checkout, clone XeniOS beside Theft4 and build its iOS
MoltenVK targets before generating the Theft4 project. The temporary dependency
on those archives is explicit and will be removed when the same pinned public
sources are built directly by Theft4:

```sh
git clone --recurse-submodules --branch xenios https://github.com/xenios-jp/XeniOS.git ../XeniOS
cmake -S ../XeniOS -B ../XeniOS/build-ios-xcode -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DXENIA_ENABLE_IOS_MOLTENVK=ON
cmake --build ../XeniOS/build-ios-xcode --config Release \
  --target MoltenVK MoltenVK_Common MoltenVK_ShaderConverter \
           spirv-cross SPIRV-Tools-static --parallel 6
```

Alternatively, set `THEFT4_MOLTENVK_IOS_LIB_DIR` to a directory containing
`libMoltenVK.a`, `libMoltenVK_ShaderConverter.a`, `libMoltenVK_Common.a`,
`libspirv-cross.a`, and `libSPIRV-Tools.a` before running
`Generate-Theft4-Xcode.command`.

## Current graphics bring-up result

The earlier headless stop at `VdSetGraphicsInterruptCallback` has been removed.
On the physical M5 iPad, GTA IV now continues to emit and consume real PM4
traffic for thousands of swaps. The device build runs LibertyRecomp's own Xenos
shader analysis and SPIR-V translation sources. Translated vertex and pixel
shaders are validated by creating real Vulkan shader modules through MoltenVK.

The production LibertyRecomp `VulkanInstance` and `VulkanDevice` implementations
also pass on-device with GPU emulation enabled. The Apple M5 GPU exposes the
required storage atomics and the needed MoltenVK extension set; geometry shaders
remain unavailable and are no longer incorrectly required on Darwin. The chosen
path is therefore the existing LibertyRecomp Vulkan renderer over static
MoltenVK—not an XeniOS backend and not a new Metal renderer.

The running embedded runtime is now connected to the production
`VulkanCommandProcessor`, caches, and `VulkanPresenter`. On-device it creates a
2816x1940 three-image swapchain from the UIKit-owned `CAMetalLayer`, publishes
the 1280x720 GTA IV guest output, and repeatedly returns `VK_SUCCESS` from image
acquisition, command submission, and `vkQueuePresentKHR`. The user visually
confirmed the game boots; later frames reached complex scenes with hundreds of
color/depth draws and many newly compiled pipelines.

Native GameController input and a native RemoteIO AudioUnit output ring are also
connected. Controller acceptance awaits a paired physical pad. The audio device
and sample conversion work, but the guest blocks are zero because the headless
XMA implementation deliberately produces silent decode buffers. The next audio
task is the real XMA decoder/FFmpeg closure.

## What is connected

The UIKit app now links the complete checked-in GTA IV AOT archive, plus the
SDK's actual filesystem, guest memory, object/thread, loader, Xbox kernel and
XAM implementations. Existing upstream stub exports remain incomplete; linking
an export does not prove that its behavior is implemented or correct.

`ios/bridge/theft4_startup.cpp` follows the existing runtime launch sequence:

1. Validate the prepared installation and create isolated startup save paths.
2. `Runtime::Setup(PPCImageConfig, config)` with the real kernel initializer and
   `tool_mode=true` (no GPU).
3. `LoadXexImage("game:/default.xex")`, using the normal UserModule loader, its
   sibling `default.xexp` patch application and import resolution.
4. Require the resulting game version `0.0.8.5` before execution.
5. Observe the registered entry point, prepare/resume the main Xbox thread,
   and wait on a background queue while the Runtime stays alive.

The observer emits `THEFT4 AOT ENTRY REACHED` immediately before calling the
registered native game function. Guest execution after that marker is still
subject to missing services, guest-code correctness, and renderer integration.

Four source units requiring concrete input, UI and audio implementations are
excluded from this experimental kernel. Their 217 exports have generated
fail-stop guards that log the exact name and caller before aborting. The first
GPU-dependent video operations likewise stop explicitly. They do not pretend
that a graphics/audio/input device exists. This guard behavior is limited to
`REXGLUE_HEADLESS_KERNEL`; the normal desktop graph is not switched to headless.

## Build and Xcode project

Host: macOS 27.0, Xcode 27.0 (27A266a), Apple Clang 21, CMake 4.4.3.
Target: physical M5 iPad Pro, ARM64, iPadOS 27, Debug, development signed.
The local development deployment minimum is **iOS 26**, because the linked
kernel's floating-point `std::from_chars` is unavailable at the preset's original
iOS 16 minimum. This is not a claim that the port supports every iOS 26 device.

From the repository root, with CMake on PATH:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
cmake --preset ios-device-debug \
  -DREXGLUE_RUNTIME_ONLY=ON \
  -DREXGLUE_HEADLESS_KERNEL=ON \
  -DTHEFT4_BUILD_GAME_CODE=ON \
  -DTHEFT4_ENABLE_GAME_STARTUP=ON \
  -DTHEFT4_SIGN_DEVICE=ON \
  -DLIBERTY_IOS_DEVELOPMENT_TEAM=YOUR_TEAM_ID \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
cmake --build out/build/ios-device-debug --config Debug \
  --target Theft4 --parallel 6 -- -allowProvisioningUpdates
```

The team above is the user's existing local development team; use your own on
another machine. Explicitly reconfigure after modifying CMake/templates.

Open `out/build/ios-device-debug/LibertyRecomp-ALL.xcodeproj`, choose scheme
**Theft4**, and select your development iPad. The app is
`out/build/ios-device-debug/theft4/Debug/Theft4.app`. Do not edit generated
`.pbxproj` settings as the source of truth; CMake regeneration replaces them.

No JIT, `MAP_JIT`, downloaded native code, or new executable-memory entitlement
is used. The generated C++ is compiled and signed as part of the app. Development
signing includes ordinary `get-task-allow`; this is not a JIT entitlement.

## Game files and controls

The user's full transferred installation is in **Documents/game**, with the
retail `default.xex`, matching `default.xexp`, and `update` assets. The prior
remote/local inventory matched 1,689 files and 7,072,969,417 bytes exactly.
The old private Application Support/game transfer is partial and is not the
source for the new startup button.

- **Prepare transferred game** keeps the earlier loader-only validation path.
- **Attempt game startup** runs the real game entry path, once per app process.
- Xcode scheme argument `--theft4-start-game` selects the same startup path.
- Restart the app process for another execution attempt. **Restart core probe**
  restarts only the old host probe; it does not reset the guest runtime.

Startup user/cache/marketplace/save roots are under
`Library/Application Support/Theft4/startup`, separate from existing saves.
The game mount is read-only under the unchanged default configuration. The
original ISO and update package are untouched and are not included in the app.

## Failures found while connecting the real executable

- **Platform guard:** `xam_net.cpp` selected socket headers for macOS/Linux, not
  iOS. Include platform definitions before the guard and use the existing Darwin
  platform distinction for socket/select declarations. No socket algorithm was
  rewritten.
- **SDK availability:** the real kernel exposes the iOS 16 `from_chars` issue
  above. The development target was raised to 26 rather than inventing a parser.
- **Static registration:** kernel export registration is constructor-based.
  Force-load the runtime archive so ordinal-resolved exports survive linking.
- **Missing headless cvar:** its original definition lives in excluded XAM UI.
  The headless export unit supplies it in the experimental graph only.
- **Generated symbol collision:** the original Apple wrapper called
  `__imp__<game-function>`, the same namespace as Xbox imports. In the first
  signed app, LLDB disassembly confirmed that `RtlSetLastError` branched into
  the XAM stub instead of its generated body. The first run also logged heap
  stubs, so that run is not a valid heap/TLS correctness result.

The Apple generator macro now puts original guest bodies in `rex_generated_*`.
Public weak game hooks call those bodies directly. Weak legacy `__imp__*`
forwarders preserve noncolliding passthrough hook compatibility; real kernel
exports remain independent. `REX_ORIGINAL_FUNC(name)` explicitly selects the
guest body when a passthrough name collides. The existing D3DResource_Release
passthrough was updated accordingly. Non-Apple alias behavior is unchanged.
Both the generator template and generated header carry the fix; none of the
84 generated instruction chunks was edited. The desktop native-hook target is
not built by this headless app and still requires its own later validation.

## Evidence and remaining scope

Build/configuration/install/console logs are under `out/build/ios-device-debug`.
`startup-app-build-5.log` and `startup-ipad-console.log` describe the first
signed launch: it entered actual AOT code and reached a deliberate stop at
`VdSetGraphicsInterruptCallback`, but had the symbol collision described above.
That first result must not be cited as proof that only graphics remained.

The corrected build **succeeded**, passed signature verification outside the
host sandbox, was installed over the existing app, and ran on the physical
iPad without attaching a debugger. Evidence:

- `startup-app-build-6.log`: all 86 AOT translation units recompiled; app linked
  and development signing completed. Existing FP register-width and integer
  narrowing warnings remain; those semantics are not validated by this build.
- `startup-symbol-check.log`: LLDB disassembly of the final app shows
  `RtlSetLastError` calling `rex_generated_RtlSetLastError` in the actual generated
  chunk, not the XAM stub. The original guest heap body is also present.
- `startup-ipad-install-2.log`: successful install of `com.theft4.bringup`.
- `startup-ipad-console-2.log`: TU8 applied, AOT entry reached, followed by
  `HEADLESS STARTUP BLOCKED: VdSetGraphicsInterruptCallback requires a graphics backend`.
  The earlier heap/error-state stub warnings are absent. The process exits with
  signal 6 because the diagnostic guard deliberately calls `abort()`.

That was the historical AOT baseline. The current build has gone substantially
beyond it: real presentation and a user-visible boot are proven. Complete
heap/TLS semantics, every Xbox service, lifecycle recovery, long-run memory use,
physical controller input, decoded audio, and gameplay correctness still require
acceptance work.

## Next bounded task

Keep the proven AOT/HLE/Vulkan/MoltenVK baseline intact. Pair a physical
controller and verify guest navigation. Then integrate the real XMA decoder in
place of `xboxkrnl_audio_xma_headless.cpp`; the native output device is already
ready to consume its nonzero mixer samples. Use XeniOS only when a specific
guest-service or rendering behavior needs comparison—it must not replace
Theft4's AOT CPU path or working renderer.
