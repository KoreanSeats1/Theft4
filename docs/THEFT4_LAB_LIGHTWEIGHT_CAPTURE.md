# Lab 15: lower-overhead CPU and pacing capture

Lab 14's 600-frame capture confirmed a sustained workload burst rather than
GPU saturation, but its detailed per-pass Metal timestamps and large export
materially perturbed the measured run. It exported 144 MiB, and directly
accounted profiler bookkeeping alone averaged 1.93 ms per frame.

Lab 15 keeps the frame-aligned CPU operation detail, producer/worker transport,
limiter deadlines and coarse whole-frame GPU envelope. It disables detailed
per-pass GPU timestamps, which removes hundreds of observational timestamp
boundaries per captured frame while retaining enough GPU evidence to identify
whether the 33.3 ms budget was exceeded.

The frame-time graph now tracks capture completion. `REC` and the orange border
remain while frames are collected and files are exported. A successful export
changes the graph to a green `SAVED` state; a failed export shows red `ERR`.
This prevents the active-capture indicator from remaining indefinitely after
instrumentation has stopped.

For the next device comparison, arm the graph while stationary in the dense
test area. Remain still briefly, rotate the camera, then reproduce the bench
collision with several pedestrians. Treat the capture as diagnostic evidence,
not an acceptance FPS result. A later fresh launch without arming the capture
is the performance acceptance run.
