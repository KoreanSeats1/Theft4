# Theft4 0.2.0b — M5 Lab becomes the official app

This release uses the M5 Lab 0.1.3 (41) code as its foundation, not the older
0.2.0/0.2.0a renderer path. It integrates first-launch setup and diagnostic
export into that Lab-based app. The iOS display name is **Theft4**, the bundle
identifier is `com.lukebrosious.theft4`, the Apple version is `0.2.0` build
`44`, and the launcher labels it `0.2.0b`.

## Substantial changes from the older official app

- Native GTA IV render path through Vulkan, MoltenVK, and Metal, including the
  M5 Lab command-transfer batching, tighter queue locks and bounded reuse.
- Reduced redundant state/binding work, shader-constant delta updates and
  texture-generation validation in the hot path.
- Bounded pipeline preparation and completion-owned frame/resource lifetime.
- Lab graphics launcher controls: independent internal resolution, FSR,
  performance preset, image-quality options, FPS counter and frame-time graph.
- First-launch installation guidance for the prepared game directory and
  matching title update, plus **System → Download Latest Log Capture**.
  A tester can play, record a bounded 600-frame capture, quit, reopen and
  export the latest diagnostic bundle to Files or the share sheet.

The Lab command-delivery comparison from M5 iPad build 32 to 33 at 900p showed
mean renderer interval 40.57 → 37.84 ms, p95 47.58 → 45.58 ms, and queue-lock
wait 7.40 → 1.54 ms. Routes and GPU work differed, so these are directional
observations, not a controlled whole-game uplift. They do not establish
lower temperature, locked 30 FPS or stability across every scene. See the
[build 33 device review](lab-experiments/build33-device-review.md) and
[A19 capture review](lab-experiments/build40-a19-air-two-capture-review.md).

## Install and first use

Install the official Theft4 app in place; do not remove an existing official
install if you want to retain its container. On first launch, follow the setup
screen to create **Files → On My iPhone/iPad → Theft4 → game**. Copy the
contents of a validated, legally obtained game staging folder into `game`;
do not copy a raw ISO or nest it as `game/game`. Select the matching TU8 title
update and use **System → Verify Game Files**. Exact revision and staging
instructions are in [iOS sideload installation](IOS_SIDELOAD_INSTALL.md).

The historical **Theft4 Lab** app has bundle identifier `com.theft4.m5lab` and
a different data container. Its game files and saves are not automatically
migrated into official Theft4. Back them up or copy them before deleting Lab.

For a low-FPS report, enable **System → Detailed Performance Capture** before
play, double-tap the frame-time graph in the slow scene, let the 600-frame
capture complete, then quit and reopen Theft4. Tap **Download Latest Log
Capture** and share the saved diagnostic bundle with device model, OS version,
route, graphics settings, play duration and thermal observations. Capturing
adds overhead, so also report how a normal run behaves with it off.

## Distribution identity

The release label is `v0.2.0b`. Apple accepts a numeric version, so the
corresponding TestFlight build is `0.2.0 (44)`. Availability in TestFlight
depends on App Store Connect processing and, for external testers, Beta App
Review approval. An uploaded build is not necessarily an available build.
