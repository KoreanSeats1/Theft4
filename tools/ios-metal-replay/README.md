# Isolated native-Metal trace experiment

This is a developer experiment, **not the Theft4 game app**. It replays one
privately captured ReXGlue/XeniOS XTR frame through XeniOS's native Metal GPU
backend. The CPU backend is explicitly `null`; no game executable or CPU JIT is
used. It does not replace Theft4's working Vulkan/MoltenVK renderer.

## Prerequisites

- A matching public XeniOS source checkout and its built iOS Release static
  libraries, with `libdxilconv.dylib` and `libmetalirconverter.dylib` in the built
  XeniOS app. These are local developer dependencies, not vendored here.
- Xcode, a development signing team, and a connected supported iPad.
- A local `scene.xtr` captured from the user's lawfully owned game. Never commit
  traces, pixels, shaders extracted from games, or game files to the repository.

## Build and run

```sh
cmake -S tools/ios-metal-replay -B out/build/ios-metal-replay -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DREPLAY_TEAM=YOUR_TEAM \
  -DXENIOS_SOURCE=/absolute/path/to/XeniOS \
  -DXENIOS_BUILD=/absolute/path/to/XeniOS/build-ios-xcode
xcodebuild -project out/build/ios-metal-replay/Theft4MetalReplay.xcodeproj \
  -target Theft4MetalReplay -configuration Release -sdk iphoneos \
  -allowProvisioningUpdates build
```

Install `out/build/ios-metal-replay/Release-iphoneos/Theft4MetalReplay.app` as
the separate bundle `com.theft4.metalreplay`. Copy the private trace to that
app's `Documents/scene.xtr`, then launch. It writes `Documents/replay.log` and,
if rendering/capture succeeds, `Documents/metal-output.ppm`.

## What this can and cannot prove

First establish compatible trace playback and visually correct output. Compare
the exact same trace against the current Vulkan renderer before attributing
differences to a backend. An output image alone is not a speed improvement.

The logged duration includes cold shader compilation, trace parsing/restoration,
GPU rendering, readback, and file output. **It is not GPU frame time or FPS.**
The standard TracePlayer clears caches when seeking backwards; repeated seeks
must not be mislabeled a warm steady-state benchmark. A controlled warm replay
and GPU timestamps are separate follow-up work. Whole-game 30 FPS also requires
normal CPU/audio/streaming work and stable frame pacing on the device.

The app uses a UIWindowScene lifecycle (required by the tested iOS 27 runtime).
Standalone application flags and the Apple cache-control shim are supplied by
this target because the reused libraries normally get them from XeniOS's app.
