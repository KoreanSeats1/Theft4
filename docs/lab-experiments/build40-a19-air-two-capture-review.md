# A19 iPhone Air: two Lab build 40 captures and build 41 experiment

2026-09-21. The user requested an A19/Pro optimization pass covering iPhone 17,
17 Pro, and iPhone Air. This review uses two bounded captures from their iPhone
Air (`iPhone18,4`, Apple A19 Pro GPU). Both were copied from the Lab app before
terminating only that app on the Air. The ordinary Theft4 app and other devices
were not touched. Raw files are in
`out/m5-lab/validation/build40-iphone-air-a19pro-20260921/` and
`out/m5-lab/validation/build40-iphone-air-a19pro-latest-20260921/`.

The first capture ID is `3675083388685`; the second, newer capture ID is
`3681547713120`. Each export has 600 samples and 599 complete, matched frames
after the boundary sample, with no missing or dropped pacing records and no
worker-partition errors. Both have detailed CPU scopes and only coarse GPU
timing. They are different playthroughs; route, camera, scene density, elapsed
heat, and output resolution were not held constant. Do not treat their
difference as the effect of one setting.

| Instrumented observation | First capture | Newer capture |
| --- | ---: | ---: |
| Internal scene resolution | 960×540 | 960×540 |
| FSR output policy | 1920×1080 | 2240×1260 |
| Shadows, draw distance, reflections, AA | Original, 1×, Original, Off | Same |
| Thermal state | Mostly nominal; fair near end | Serious on all 599 complete frames |
| Renderer interval p50 / p95 | 33.2 / 41.8 ms | 44.5 / 56.2 ms |
| Coarse GPU envelope p50 / p95 | 18.0 / 23.7 ms | 23.4 / 33.5 ms |
| Render callback p50 | 7.0 ms | 28.3 ms |
| Command recording p50 | 4.5 ms | 17.4 ms |
| Texture preparation p50 | 1.0 ms | 4.1 ms |
| Worker assembly p50 | 1.8 ms | 6.6 ms |
| Commands / draws, mean | 7,844 / 2,022 | 9,443 / 2,312 |
| Producer capture / validation, mean | 6.1 / 4.2 ms | 21.9 / 15.2 ms |
| Limiter sleeps / late resets | 576 / 0 | 2 / 175 |
| Physical footprint, mean | 1.06 GB | 1.41 GB |

The second scene issued about 20% more commands and 14% more draws, but
command recording rose roughly 3.7× and worker assembly 3.5×. The serious
thermal state is a strong explanation for much of that per-operation slowdown;
the trace does not expose CPU clocks, so it cannot prove the precise share from
thermal throttling. The higher output pixel count also raises GPU/upscaler
work. GPU time is usually below the 33.3 ms target, while the CPU callback,
producer validation, and worker queue become saturated. In the newer trace,
worker condition-wait median is zero, producer backpressure averages 2.45 ms,
queue dwell maximum averages 38.3 ms, and the limiter virtually never sleeps.
The capture supports a command-production/submission bottleneck amplified by
heat, with some GPU headroom remaining on median frames. It does not directly
measure guest physics or simulation CPU; the legacy `guest_gap` labels measure
the render worker between frame publications. Nor does it measure physical
scanout. Instrumented scopes overlap and must not be summed as a serial frame
budget.

Pipeline compilation is not the sustained cause: the newer run averaged only
0.01 pipeline misses and creates per frame. Texture uploads averaged 71 KB per
frame, so sustained texture streaming is also not the dominant recorded cost.
The highest exclusive render CPU categories in the newer capture were command
recording (`record-command` 3.06 ms), texture preparation (2.83 ms), profiler
bookkeeping (2.59 ms), shared draw constants (1.68 ms), pipeline-key creation
(1.61 ms), upload-capacity calculation (1.45 ms), and surface lookup (1.39 ms).
All are means over 599 frames, and profiler bookkeeping is capture-only.

## Build 41 A19 experiment

The A19 launch profile now enables sparse texture-stage walks in three hot
paths: producer texture capture, upload-capacity sizing, and packed-depth alias
budgeting. Shader metadata already supplies an authoritative used-stage mask,
and texture resources are captured only for draw commands. The new traversal
visits those stages in the same ascending order, preserving required captures,
resource identities, and alias budgeting while avoiding scans over all 26
texture slots of thousands of state commands and unused draw slots. The
existing exhaustive traversal remains the default for non-A19 profiles and a
launch-time CVAR fallback. The A19 profile is already selected for `iPhone18,*`
devices, including the requested iPhone 17 / Pro / Air family; it is not
enabled for M5 iPad. No shadow, distance, reflection, or scene-resolution preset
is changed.

At the user's request, the A19 Lab output policy also fixes the final drawable
to 1920×1080: 540p, 720p, and 900p scenes use FSR to that target, while 1080p
is native and bypasses FSR. This replaces the previous fit-to-screen A19 Lab
output, which was 2240×1260 in the newer trace. The launcher's FSR switch is
latched and disabled for A19 so the displayed setting matches the effective
policy. Non-A19 Lab output continues to fit the display; ordinary Theft4 is
untouched. This output change is a deliberate quality/power tradeoff, distinct
from the sparse-stage CPU optimization. The internal scene selection stays
under the user's control, including 540p.

This is a source-level hypothesis, not a measured FPS gain. Its likely impact
is on the texture-preparation and upload-capacity parts of the CPU path, plus
some producer capture cost. It cannot by itself undo severe thermal slowdown
or eliminate the rest of command recording and validation. Compare a cooled
build-40 and build-41 run at identical settings and route, then repeat after a
long warm-up. Record thermal state, output size, frame intervals, command/draw
counts, the three target scopes, backpressure and physical footprint. Check
texture correctness (especially reflections, depth aliases, and streaming)
before treating any speedup as valid. If 2240×1260 remains selected, a separate
1920×1080 output test is useful to quantify the output/power tradeoff; it is
not an engine optimization. The 14:25 launch also logged one secondary-frame
completion recovery failure; later sessions ran and captured. Treat that as a
separate stability investigation, not as a measured cause of these frame times.
