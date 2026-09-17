# Theft4 optional 1080p output — implementation and device acceptance

Status: signed ARM64 Release build and host policy/counter checks passed;
installed in place after the user's explicit cue. App left closed for the user
to playtest. No visual or performance improvement is claimed from build/install.

## Build artifact

- Xcode 27.0 (27A266a), iPhoneOS 27.0 SDK, Release `-O3 -DNDEBUG`, ARM64 with
  existing `-mtune=apple-m5`; ThinLTO remains off.
- Target: `Theft4` in `out/build/ios-device-release/LibertyRecomp-ALL.xcodeproj`.
- App: `out/build/ios-device-release/theft4/Release/Theft4.app`.
- Bundle identity preserved: `com.theft4.bringup`.
- Final build exited zero. Development signature verification passed (`valid
  on disk`, designated requirement satisfied). Verification required host
  certificate-trust access; no trust/signing configuration was changed.
- Mach-O UUID: `0292EDBE-8ACC-32AA-8604-AC092331AEDC` (arm64).
- Executable SHA-256:
  `6307eefc74d9bd70b7b120af3fe77f4c15b60fef9646bedf999c4d5dce6b2418`.
- Binary strings verified the new setting, content-sequence counter marker and
  output-route diagnostic are present. `git diff --check` passed.
- Private local build log: `/private/tmp/theft4-1080p-fps-build-final.log`.
  Existing FPCR/integer-width, generic Vulkan nontrivial-memory/parentheses,
  UIKit `mainScreen`, and Xcode manual-target-order warnings remain; no compiler
  error or new warning in the added counter/output-policy code was found.
- No install, launch, debugger attachment, or device-side comparison performed.
  This describes the original build-only handoff; installation was subsequently
  authorized and completed as recorded below.

### Installation after user cue

Verified the same executable SHA-256 and bundle identity immediately before
installing. Closed the running Theft4 process (PID 3942), then installed in place
successfully as `com.theft4.bringup`. No uninstall or app-data/save deletion was
performed. Post-install process listing confirmed Theft4 absent. No launch,
debugger attachment or visual/performance comparison was performed; user
playtest is next. Enhanced output remains default off, with preferences retained.

## Modes

The launcher Options tab contains **1080p enhanced output (FSR 1)**, default off.
It persists as `Theft4EnhancedOutput1080p` and is latched before runtime creation.
Changing modes requires quitting and relaunching the app; no live renderer
reconfiguration or GPU lifetime changes are introduced.

| Mode | Game render size | Swapchain/output | Presenter |
| --- | --- | --- | --- |
| Off / baseline | 1280×720 | 1280×720 | Existing bilinear path |
| On / enhanced | 1280×720 | 1920×1080 | Existing FSR 1 EASU + RCAS |

Enhanced mode selects the native hooks' existing FSR Quality ratio (1.5×) and
the presenter's existing spatial upscaler. Both the native hook settings and
`present_effect=fsr` must be selected: the iOS frontend does not run desktop
`GTA4App::OnPreSetup`. UIKit sizes the CAMetalLayer before the swapchain is
created and retains that launch size on later layout callbacks.

SMAA/high, 16:9, the 30 cap, one active native frame slot, shadows, reflections,
draw distance, AOT execution, audio and saves are unchanged. The independent
texture-filtering switch remains available; enable 4× for both comparison runs.
The game HUD is part of the scaled game image, not newly rendered native-1080p
text. This is not temporal upscaling, frame generation or true native 1080p.
Extra output pixels and the two FSR passes may cost performance.

## FPS correction

`Presenter::RefreshGuestOutput` now assigns a successful-content sequence before
the mailbox release operation. Inactive output carries zero; failed refreshes
return without publishing. The consumer counts each newer sequence once, only
after a successful/suboptimal queue present. Dropped mailbox publications and
repeated presents cannot inflate the count. The existing image allocation
version remains untouched, preserving framebuffer/resource cache identity.

The overlay still measures unique content handed to the presentation queue,
not hardware scanout. It neither forces a 30 display nor smooths away stalls.
Sparse `[Theft4Present]` log milestones now include `content` and `unique` for
cross-checking; no per-frame logging/readback is added.

## Host verification

```sh
env DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun clang++ \
  -std=c++17 -Wall -Wextra -Werror \
  -Iios/bridge -Iglue/rexglue-sdk-main/include \
  ios/tests/output_and_frame_counter_test.cpp \
  -o /private/tmp/theft4-output-counter-test
/private/tmp/theft4-output-counter-test
```

Checks: mode dimensions, exact 1.5× ratio, stable 720p input, valid first frame,
image reuse, repeat rejection, failed-present retry, stale and dropped
publications, inactive output, and 30 fresh frames counted as 30.
These are host logic tests, not an iPad GPU/visual test.

## Device test, only after installation is authorized

1. Start with enhanced output **off**, 4× filtering **on**, FPS **on**. Run
   normally without debugger, Metal validation, capture or profiling. Load the
   same save, let streaming settle, then traverse the bridge/long-view route.
2. Confirm log witnesses `render=1280x720 output=1280x720 upscaler=native` and
   actual route `guest=1280x720 swapchain=1280x720 fsr-easu=false fsr-rcas=false`.
   Compare `unique` increments per elapsed time with the displayed FPS; the old
   2/3 undercount must disappear without counting repeated frames.
3. Quit after saves finish. Relaunch, enable enhanced output, repeat the same
   save/route with similar camera, time/weather, temperature and power state.
4. Require configured `render=1280x720 output=1920x1080 upscaler=fsr1` and actual
   route `guest=1280x720 swapchain=1920x1080 fsr-easu=true fsr-rcas=true`.
   Check shader/pipeline errors and any silent fallback before judging quality.
5. Examine signs/HUD/map text, building/window edges, bridge cables/railings,
   road texture, highlights and dark scenes. Rotate slowly and drive quickly:
   watch for halos, ringing, shimmer, altered brightness, cropping or stretching.
6. Compare heavy-view FPS and hitch frequency, not only stationary clarity.
   Warm both modes, repeat in reverse order if a difference is small. A frame
   counter alone cannot establish perfect pacing; capture a bounded frame-time
   trace separately if needed, then judge performance again without profiling.
7. Stop the app when finished. Keep the original mode as the fallback if clarity
   is not worth the cost, rendering fails, or frame pacing deteriorates.

## Files

- `ios/Theft4/main.m`: persisted optional control and pre-start mode selection.
- `ios/bridge/theft4_output_policy.h`: common C/C++ input/output extent policy.
- `ios/bridge/theft4_metal_presenter.{h,mm}`: latched drawable size.
- `ios/bridge/theft4_startup.cpp`: native resolution and FSR presenter settings.
- `glue/rexglue-sdk-main/include/rex/ui/guest_output_frame_sequence.h`: pure
  publication/counter helpers shared with the host test.
- `glue/rexglue-sdk-main/include/rex/ui/{presenter.h,vulkan/presenter.h}` and
  `src/ui/{presenter.cpp,vulkan/vulkan_presenter.cpp}` under that SDK: sequence
  transport, counting and one-time selected-output-route logging.
- `ios/tests/output_and_frame_counter_test.cpp`: host checks.

No XeniOS change, game payload, new temporal-upscaler dependency, device launch,
or save migration is part of this implementation. Installation occurred only
after the separate user cue recorded above.
