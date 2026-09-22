# Theft4 0.2.0 — Lab renderer promotion

Theft4 Lab's renderer and gameplay performance work is now the regular Theft4
build. Direct device/sideload builds use the existing `com.theft4.bringup`
identifier. The existing TestFlight app uses `com.lukebrosious.theft4`.
Each 0.2.0 package updates its own prior app and container in place; they do
not migrate data between these two identities. The historical Lab app has its
own identifier and is not removed by either update.

## Performance and frame-time work

- The guest producer and native render worker transfer commands in batches.
  Queue ownership remains ordered, while producer lock contention and worker
  batch-transfer time were reduced in measured M5 iPad captures.
- Common state commands use compact packets, and successfully queued identical
  bindings can be suppressed. Draws still capture and protect their resource
  generations. Bounded packet storage reuse reduces construction and release
  churn across dense scenes.
- Immutable vertex-layout requirements and unchanged vertex/index bindings
  are reused. Validated parent constant blocks receive only changed ranges
  when an exact fenced frame allocation is available.
- Texture content identity is separated from sampler settings. A19 devices
  walk the texture stages actually used by the shader in selected CPU paths.
  The existing full traversal remains available as a diagnostic fallback.
- Pipeline prewarming is bounded, with A19 prewarm disabled to avoid speculative
  backlog. A draw whose new pipeline is still compiling can be deferred rather
  than blocking the entire render worker; check visual correctness when moving
  into new areas.
- Two completion-owned native frame slots preserve overlap without releasing
  buffers or textures still in flight. The launcher now exposes 540p through
  1080p internal resolutions, FSR, graphics quality choices and a performance
  preset. A19 final output is capped at 1080p to reduce drawable pixel work.
- The frame-time graph shows publication intervals and recent spikes. The new
  System capture switch arms a bounded 600-frame detailed CPU/GPU profile;
  after relaunch, the download button saves a single diagnostic text bundle to
  Files and opens the share sheet.

The [README](../README.md#what-changed-in-020) explains each change and its
purpose. The [engineering changelog](../CHANGELOG.md) and
[Lab experiment records](lab-experiments/) retain test conditions, reversions
and unresolved performance questions.

## What has been measured

In separate instrumented 900p M5 iPad playthroughs, the build 32 to 33
command-transfer change reduced mean batch-transfer time from 4.53 to 0.16 ms
and producer queue-lock wait from 7.40 to 1.54 ms. Mean renderer interval
changed from 40.57 to 37.84 ms, p95 from 47.58 to 45.58 ms, and frames above
40 ms from 357/599 to 182/599. The routes and GPU work differed, so the
whole-frame difference is not a controlled proof of a fixed percentage FPS
uplift. See the [capture review](lab-experiments/build33-device-review.md).

Heavy scenes and thermal state still matter. An A19 iPhone Air capture in
serious thermal state had a 44.5 ms median renderer interval; another run in
different conditions was 33.2 ms. Scene and output size differed, and the
captures do not measure CPU clocks or device power. Lower resolution and the
1080p output cap can reduce pixel workload, but lower heat and battery use
still require controlled device measurement. See the
[A19 review](lab-experiments/build40-a19-air-two-capture-review.md).

## For beta testers

Update Theft4 normally. Existing game files and saves are in the same app
container; do not uninstall the existing app. If a slow area occurs, note the
device model, route, graphics settings, approximate play time and whether the
device felt hot. For a detailed capture, enable **System → Detailed performance
capture** before starting the game. In the slow scene, double-tap the frame-time
graph, wait for 600 frames to complete, quit and relaunch, then tap
**Download Latest Log Capture**.
The result appears under **Files → On My iPhone/iPad → Theft4 → Diagnostics**.
Share that file with the report. Profiling itself adds overhead, so also
describe a normal run with capture off.

## Known work still underway

Physics and guest scheduling in heavy scenes, sustained frame-time tails,
first-visit pipeline behavior, background/foreground recovery, controller and
touch coverage, and wider device compatibility remain active work. A 30 FPS
average or short smooth interval is not a locked 30 FPS result. This release
does not claim measured lower device temperature or universal stability.

## Distribution

The GitHub `ios-arm64.ipa` asset is unsigned for users who sideload and re-sign
with their own account. Use the [sideload guide](IOS_SIDELOAD_INSTALL.md).
TestFlight receives a separate Apple-signed build under its existing
`com.lukebrosious.theft4` identifier and the same 0.2.0 version. The GitHub
source archive is not an IPA and contains
no game files, title update, saves or private signing material.
