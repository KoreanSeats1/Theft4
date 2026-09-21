# Lab native resolution and independent FSR control

The Lab Display tab now selects the actual scene resolution with a 720p / 900p /
1080p segmented control. A separate FSR upscaling switch applies FSR 1 to any of
those scene sizes. With FSR off, the drawable matches the selected scene size;
iOS fits the resulting image into the existing 16:9 game view. With FSR on, the
drawable uses the existing native-fit 16:9 policy, bounded from 1080p to 4K.
The controls apply before game startup and are disabled when startup begins.
Changing them requires a fresh game launch.

| Selection | Scene | FSR off output | FSR on output on the 11-inch M5 iPad |
|---|---|---|---|
| 720p | 1280×720 | 1280×720 | 2416×1359 |
| 900p | 1600×900 | 1600×900 | 2416×1359 |
| 1080p | 1920×1080 | 1920×1080 | 2416×1359 |

The display summary shows the selected input and output dimensions. Other
screens/windows get their own fitted output; small or unavailable geometry
retains the existing 1920×1080 floor. When input and output are both 1080p, the
presenter can apply FSR sharpening without an enlargement pass.

## Preferences and renderer routing

Lab persists the render height as `Theft4LabRenderHeight` and FSR independently
as `Theft4LabFSREnabled`. Missing or invalid heights select 720p. The FSR setting
migrates once from the old enhanced-output/Boost On state. Old Boost values do
not override the new control. Enabling FSR now explicitly chooses screen-fitted
output instead of the previous fixed 1080p enhanced output.

Ordinary Theft4 retains its old controls and policies. The new controls appear
only when the bundle's `Theft4LabBuild` flag is true. No frame limiter, renderer
queue, filtering, motion blur, reflection, audio or save policy is changed here.

The native renderer already supports variable resolution. Its FSR Quality
hooks derive scene dimensions by dividing the logical video size by 1.5. The
new policy therefore uses logical modes 1920×1080, 2400×1350 or 2880×1620 with
FSR enabled, independently of the physical swapchain size. With FSR disabled,
logical, render and drawable sizes all match the selected native resolution.
Startup logs report the selected scene and physical output dimensions; the
presenter's output-route log must separately confirm actual input/output.

## Validation

The focused C++20 test exercises all six selections through the actual shared
aspect policy and the native hook's FSR division, with M5 screen sizes, portrait,
small/missing geometry and an oversized external display. It verifies exact
scene sizes, 16:9 output, no FSR downscaling, output caps and invalid preference
fallback. The existing C++17 legacy output/counter test remains unchanged.

```sh
xcrun clang++ -std=c++20 -Wall -Wextra -Werror \
  -Iios/bridge -Iglue/rexglue-sdk-main/gta4-recomp/src \
  ios/tests/lab_resolution_policy_test.cpp -o /private/tmp/theft4-lab-resolution-test
/private/tmp/theft4-lab-resolution-test
```

Host policy tests passed with AddressSanitizer and UndefinedBehaviorSanitizer;
the unchanged C++17 legacy output/counter test also passed. The UIKit controller
passed an iOS-device syntax check with the game-startup and Lab capture paths
enabled. An isolated Release simulator preview was built and visually checked
at 900p + FSR and native 1080p; its simulator was shut down afterward. These
checks do not exercise the game renderer on a physical device.

The isolated launcher preview accepts `--900p`, `--1080p` and `--no-fsr` with
`--display` to inspect the six combinations without loading game data.

Device gameplay acceptance is still required. For each selection, use the same
save and route after warming caches; verify the output-route dimensions, HUD,
menus, reflections and streaming, then compare frame intervals and temperature.
900p/1080p scene rendering is experimental, not a sustained-30-FPS claim.
