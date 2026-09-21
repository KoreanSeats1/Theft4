# Lab build 34: bounded command reuse

Build 33's device capture is reviewed in `build33-device-review.md`. It
substantially reduced queue transfer and lock waiting, but the producer
validation remainder rose by about 5.1 ms/frame. That span includes the new
per-command allocation and default initialization; the allocation portion was
not separately measured. Worker assembly remained about 9.5 ms/frame.

This Lab build keeps the unique-owner queue and adds a one-producer/one-worker
command recycler. The worker releases each processed command's references and
owned payloads before placing reset storage in a local return batch. It
publishes up to 128 owners per exchange under a separate mutex. The producer
refills a local cache from that exchange, then reuses command storage without
taking the exchange mutex on every submission. Shared storage is capped at
1024 commands, with up to 128 in each local cache; excess is destroyed. Queue
order, in-flight command ownership, frame retention, resource pins,
synchronous completion and shutdown retain their existing paths. A burst can
still allocate fresh commands.

The worker now skips `SetVertexDeclaration` updates only when both the handle
and registered resource identity match its current state. Reusing the same
guest handle for a new registration still updates the immutable pipeline
snapshot. No render pipeline, physics, pacing or resolution policy changed.

The next detailed capture adds producer command-acquisition time and reuse
count, worker recycle time, constant-delta time, snapshot time, frame insertion
time, draw/state/other command counts, avoided vertex-declaration updates and
shared pool size/high-water. These are aggregate frame measurements; the
new timers themselves add cost during logging. The recycler also shifts
command reset work to the worker, so lower allocation cost alone does not
establish a net frame-time gain. Compare total interval and producer/worker
spans against build 33 before keeping this experiment.

Focused queue/recycler tests passed 6 cases and 667,349 assertions under
AddressSanitizer and UndefinedBehaviorSanitizer. They cover FIFO, resource
reference lifetime, reset state, reuse and capped burst storage. The final
Release iPad app compiled successfully from the staged source. Installed app:
`com.theft4.m5lab`, version 0.1.3, build 34, on the M5 iPad. The device
reported build 34, and Lab was not running after installation. Build 33 is
preserved at `out/m5-lab/backups/build33-85245661/Theft4.app`.

Validation logs and device receipts:
`out/m5-lab/validation/build34-command-reuse/`.

Next device run: drive the same heavy 900p + FSR city route with logging off
on the first and repeat passes. Then capture one repeat pass. Compare frame
interval distribution, especially >40 and >45 ms, plus command-acquire,
recycle, assembly and backpressure costs. Watch for correctness regressions
and longer-session physics behavior. This build has not yet been gameplay
validated and is not ready to merge into the main app.
