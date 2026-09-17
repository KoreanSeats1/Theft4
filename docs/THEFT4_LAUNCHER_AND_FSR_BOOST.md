# After Hours launcher and Experimental FSR Boost

## Build-only checkpoint — 2026-09-16

Implemented and built. **Do not install or launch on the physical iPad until
the user gives a new cue.** An isolated simulator preview is not a game test.

Update: user subsequently said **"Install now"**. The verified Release candidate
was installed in place with no uninstall/data deletion or automatic launch.
Simulator System/scene-retirement views were also inspected; the decorative
city and rain disappear as intended. The preview and its dedicated simulator
were stopped afterward. Physical-device gameplay/Boost validation remains next.

The existing Xcode project remains
`out/build/ios-device-release/LibertyRecomp-ALL.xcodeproj`, target `Theft4`, Release.
The signed build is `out/build/ios-device-release/theft4/Release/Theft4.app`.
The development bundle ID stays `com.theft4.bringup`. Installation, when
authorized, must be in place: never uninstall or delete saves/game data.

## Responsibilities and lifecycle

- `ios/Theft4/Theft4LauncherView.{h,m}` owns visual layout, numbered tabs,
  control presentation, atmosphere and the decorative city view.
- `ios/Theft4/Theft4CityView.{h,m}` owns procedural SceneKit geometry, generated
  window textures, camera, lighting, reflections, depth of field, bloom and traffic.
  UIKit/SceneKit/Core Animation are system frameworks, not downloaded dependencies.
- `ios/Theft4/main.m` remains the owner of preferences, runtime, file preparation,
  lifecycle callbacks and actual launch. The launcher has no game-runtime includes.
- Before execution begins, `retireScene` removes the SCNView, releases its scene
  and camera and removes rain particles. It is idempotent; entering game
  presentation calls it again safely. The launcher itself stays available to show
  real startup events until the existing entry-point event hides it.
- Resigning active pauses menu animation; Reduce Motion disables automatic
  camera drift, traffic and rain. Manual orbit remains available. There is no
  menu audio, motion-sensor use, network request or new entitlement.
- SceneKit is deprecated by Apple in newer SDKs. It compiles with the installed
  SDK and is confined to a small replaceable launcher view; game rendering does
  not depend on it. A future migration can replace this view without changing
  runtime/startup or output policy.

## Output policy

| Mode | Scene rendering | Logical video mode | Presentation output |
|---|---|---|---|
| FSR off | 1280×720 | 1280×720 | 1280×720 |
| 1080p FSR, default | 1280×720 | 1920×1080 | 1920×1080 |
| Experimental FSR Boost | 1280×720 | 1920×1080 | Native-fit 16:9, 1080p–4K |

`theft4_output_policy_for_mode` computes Boost as
`units = clamp(min(nativeWidth / 16, nativeHeight / 9), 120, 240)`, then
`output = (16 * units, 9 * units)`. Sizes are integer pixels, aspect is exact.
The caller supplies the laid-out 16:9 game view in points multiplied by the
owning screen's native scale. Full-screen landscape examples: 2752-wide gives
2752×1548, 2420-wide gives 2416×1359 (four columns shy of full native width).
Small windows or missing geometry keep 1080p rather than downgrading Boost.
The 4K cap bounds allocation and postprocessing cost on larger external displays.

`theft4_metal_set_output_mode` latches the resulting policy before runtime
initialization. Later layout changes retain that drawable extent. Rotate/resize
the window before launching to select a different target; live game resizing is
not added in this pass.

The native game's FSR Quality hook derives scene dimensions by dividing logical
video dimensions by 1.5. Therefore **do not set `video_mode_width/height` to the
Boost drawable extent**: that would silently raise scene resolution. Startup
uses `video_width/height`; CAMetalLayer uses `output_width/height`. The shared
presenter reads the actual swapchain extent for EASU/RCAS. Existing EASU chaining
handles scale factors above 2× per axis without a new shader implementation.

SMAA/high and independently selected 4× texture filtering remain unchanged.
Sharper output does not create native-resolution geometry or HUD assets.
This is FSR **1**, not FSR 2/3, MetalFX, temporal reconstruction or frame generation.
Boost may be more expensive and may not look better in every scene; regular
1080p remains the reference and fallback.

Preference `Theft4EnhancedOutput1080p` now registers YES; explicit saved values
are not overwritten. New `Theft4ExperimentalFSRBoost` registers NO. Enabling
Boost enables FSR; disabling FSR clears Boost. Preferences persist in the same
app container. No save architecture, asset locations, audio or GPU flight
settings change.

## Verification performed

- Host C++17 tests with `-Wall -Wextra -Werror`: original720/1080 policies,
  native-fit Boost, portrait/native bounds, missing/small-window fallback,
  upper bound and unchanged720 scene/logical-video invariant; publication FPS
  counter tests still pass. Source: `ios/tests/output_and_frame_counter_test.cpp`.
- Isolated `com.theft4.launcher-preview` built and rendered on an iOS27 iPad
  simulator; Play and Display screenshots visually inspected. This harness
  never accesses game files or the development app container.
- Device ARM64 Release app built with Xcode27; strict signature verification
  succeeded. Existing dependency/FPCR/UIScreen deprecation warnings remain.
  No physical-device installation, launch, debugger or performance measurement.

Simulator harness:

```sh
cmake -S ios/launcher-preview -B out/build/launcher-preview -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0
xcodebuild -project out/build/launcher-preview/Theft4LauncherPreview.xcodeproj \
  -target Theft4LauncherPreview -configuration Release -sdk iphonesimulator build
```

Use the selected Xcode developer directory and make CMake available in PATH.
Preview arguments `--display`, `--system`, `--retire` expose settings/system/
scene-teardown paths without linking or executing game code. Orientation on the
headless simulator remained portrait despite a landscape geometry request;
physical landscape, compact layouts and large accessibility sizes still need
visual acceptance.

## Next authorized-device acceptance

1. In-place install only after cue. Check menu at landscape/portrait, Display
   and System scrolling, VoiceOver labels and Reduce Motion. Background/return
   before starting the game and confirm menu animations resume appropriately.
2. Start with regular1080 FSR,4×,one-frame-in-flight. Check real loading feedback,
   menu-to-game transition and normal save/controller/audio operation. Confirm no
   SceneKit menu scene remains running once game startup begins.
3. Relaunch with Boost. Verify startup render=1280×720 and expected output extent;
   sparse presenter output-route log must report actual swapchain and FSR passes.
   Do not infer working FSR solely from the switch or policy test.
4. Compare identical save/location/camera path: thin rails, wires, moving camera,
   HUD/text, long city views, rapid driving. Capture frame-time evidence without
   GPU validation in the performance run. Watch for ringing/shimmering and
   increased GPU/memory/thermal cost. Retain1080 if Boost has poor tradeoffs.
5. Quit when testing ends. Do not leave the game running unattended.
