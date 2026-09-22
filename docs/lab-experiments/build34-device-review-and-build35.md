# Lab build 34 capture and build 35 experiment

The M5 iPad's saved Lab build 34 capture was copied before terminating only
`com.theft4.m5lab`. The other Theft4 process was left alone. Capture
5551250596992 is complete: 600 samples, 599 full frames after excluding the
partial boundary frame, frames 3693–4291. Raw diagnostics, runtime log,
comparison script/JSON and device receipts are under
`out/m5-lab/validation/build34-device-review/`.

## Evidence

Build 34 ran at 1600×900 with a 2400×1350 output. Build 33's saved capture
used the same render size but 2416×1359 output and a different, uncontrolled
route. Capture overhead and scene density differ, so their frame intervals
are context, not a controlled A/B speedup or regression measurement. The
user's gameplay report says the initial area stayed smooth and heavy areas
still lowered FPS.

| Instrumented measurement, mean | Build 33 | Build 34 |
| --- | ---: | ---: |
| Renderer interval | 37.844 ms | 41.138 ms |
| Commands per frame | 16,329 | 18,599 |
| Coarse GPU envelope | 11.565 ms | 13.867 ms |
| Producer validation | 14.687 ms | 10.228 ms |
| Producer validation per command | 0.904 µs | 0.552 µs |
| Worker assembly | 9.509 ms | 10.398 ms |
| Worker assembly per command | 0.584 µs | 0.561 µs |
| Producer backpressure | 5.862 ms | 13.075 ms |

Build 34 reused 16,613 of 18,599 commands/frame on average, about 89.3%.
The shared pool reached its 1,024-slot cap, while about 1,986 commands per
frame still needed fresh storage. Command acquisition cost 1.239 ms/frame.
The worker-side reset and recycle cost **5.150 ms/frame**, leaving a clear
opportunity to move default initialization away from the worker. The
unattributed validation remainder fell from about 7.144 to 2.697 ms/frame;
these spans are nested and must not be added as independent frame costs.

In the heavier 60-frame window, the renderer interval averaged 46.98 ms with
21,353 commands, 12.249 ms of worker assembly and 13.403 ms coarse GPU time.
The capture's next-frame alignment gives a descriptive correlation of 0.904
between interval and worker assembly and 0.075 between interval and GPU time.
This favors CPU command delivery as the current bottleneck but does not prove
causality. Work on the worker measured 2.546 ms in constant application,
0.684 ms in pipeline snapshots and 0.762 ms inserting frame commands. It
processed roughly 13,344 state commands and 4,601 draw commands per frame.
The vertex-declaration equality guard recorded zero avoidances in this run;
it is not a meaningful performance target for the next build.

Thermal state, buffer-shadow mismatches, fast-path disables and texture
allocation failures were zero during this capture. The profiler cannot
measure physical scanout. No conclusion about longer-session physics
correctness follows from these counters.

## Build 35 changes and hypothesis

The recycler now destroys a completed command on the worker to release its
resource references, then returns only its raw allocated storage. The
producer reconstructs a fully default-initialized command in the slot before
capture. This preserves construction semantics and unique-owner queue
lifetime while removing default initialization from the worker's critical
path. The exchange cap rises from 1,024 to 2,048 slots, about 3 MB more
command storage at the 2,992-byte runtime size, to test whether fewer burst
allocations help without unbounded memory growth. Worker destruction still
costs time; the next capture must measure acquisition and recycle spans.

An empty constant delta on an already initialized stage now returns the
existing version before repeating validation and delta preparation. Invalid
complete snapshots and initial bootstrap still take the existing validation
path. This targets a portion of the measured 2.546 ms constant application,
but the frequency of empty deltas is not yet logged, so its gain is unknown.

Focused queue, recycler and dirty-state tests passed 27 cases and 688,255
assertions normally and with AddressSanitizer/UndefinedBehaviorSanitizer.
The Release iPad build completed and was signed with the development identity
in Lab's provisioning profile. The M5 iPad reports
`com.theft4.m5lab`, version 0.1.3, build 35; Lab was closed after installation.
Build and install receipts are under `out/m5-lab/validation/build35-raw-reuse/`.
Build 34's signed app is preserved at
`out/m5-lab/backups/build34-edd85ddb/Theft4.app`. The first test of build 35
should compare unlogged first and repeat driving passes in the same heavy
900p + FSR route, then one saved repeat-pass capture. Keep build 34 as a
rollback if frame intervals, pacing, resource correctness or memory worsen.
No locked 30 FPS or gameplay stability claim is made before that test.
