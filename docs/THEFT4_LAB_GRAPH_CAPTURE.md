# Lab 14: frame-time graph capture control

## Device observation

Screenshots from Lab 13 on the M5 iPad show completed publication intervals,
not the graph's unfinished-frame estimate:

- paused menu: 23.1 ms current, 54.1 ms rolling peak;
- standing still: 30.0 ms current, 53.2 ms rolling peak;
- rotating the camera: 37.6 ms current, 50.9 ms rolling peak.

None of the current values has the graph's `+` suffix, so these are completed
content-publication intervals. The variation therefore exists even without
forward movement and is not explained solely by new-area streaming. These UI
samples are not GPU duration or physical scanout timestamps. The bounded native
capture remains necessary to separate GPU time, CPU publication/recording and
the command-delivery gap.

## Capture control correction

Lab 13 only installed the FPS double-tap gesture when the process was launched
with `THEFT4_LAB_NATIVE_PROFILE=1`. A normal icon launch omitted both the
gesture and the immutable native-profiler diagnostics category, so the user's
double tap could not start logging.

Lab 14 makes the native profiler available in every Lab process while leaving
the bounded detailed collection dormant. Double-tapping the visible frame-time
graph arms one 600-frame capture. The graph gains an orange border and `REC`
label after the request succeeds. The FPS label no longer owns the gesture.
Ordinary Theft4 builds remain unchanged.

The frame-time graph still records distinct content-publication intervals. A
paused menu may publish at a different or irregular cadence, so paused samples
must be identified separately from standing, camera-rotation and driving
sections when interpreting a capture.
