# Build 90 — command stream ownership and bounded CPU cleanup

## Purpose

Reduce work on the native renderer and guest Present producer after the shared effective CPU-throughput transition. Build 89's same-count fixed register probe still slowed roughly 1.9× outdoors; native renderer CPU reached about 29–31 ms/frame. This implementation does not establish or disable the platform transition, and no performance improvement is claimed before a user comparison.

## Implemented

- Producer-owned packets of up to eight compact, resource-free state notifications. Partial packets publish immediately before the next full command; full packets publish immediately. This avoids exposing an unfinished setter run to the worker. Producer shader shadow updates remain immediate. FIFO, sequence/epoch identity, logical queue accounting, Present admission and synchronous texture-lock boundaries remain ordered. Producer-only buffer lifetime/unlock notifications flush pending state before their effects. Detailed transport profiling and diagnostic envelopes retain their existing individual records.
- Full draw/clear/resolve/handoff/marker/release command objects retain their original unique owner and stable address into the frame. Frame storage contains pointers in the enabled path, avoiding another large command move and destruction of its moved-from twin. Existing generation protection remains in force at queue, staging, current-frame and GPU retirement boundaries.
- Command recycler can retain empty draw payload and constant delta vector capacity, with an 8 KiB combined payload/range limit per slot. All command flags, resources, snapshots, shader versions, trace owners and synchronous owners reset before producer reuse. Existing shared limit of 2,048 slots remains; producer and worker local batches are 128 slots each. Oversized capacities are released. The disabled path retains the original raw-slot reconstruction behavior. Constant capture explicitly clears sizes and snapshot flags while preserving these capacities only in the new path.
- One serial GCD utility queue processes completed CPU command records. It admits at most one batch, up to 8,192 commands / 64 MiB of directly owned command metadata, and requires at least four available CPUs. It uses a private 128-slot exchange batch and the recycler's locked shared list; it never mutates renderer/producer local free lists. Busy, unavailable or oversized jobs fall back inline without waiting. Pressure recovery and teardown wait for outstanding cleanup. This is an extra CPU lane whose core placement is chosen by iOS; it does not pin or promise efficiency-core use.
- Cleanup only resets/releases CPU owners. Thread-safe retirement notifications remain published to the existing queue; Vulkan recording and GPU release stay with the renderer. Existing constant helper work joins before CPU owners can leave the frame.
- Long Capture extends its native schema from 222 to 236 columns. New cumulative counters report buffered states, published packets, retained acquisitions, completed cleanup commands, helper CPU and wall duration, retained/high-water metadata and busy/budget fallbacks, plus effective feature flags. Counters measure moved work as well as native-thread savings; they do not make moved CPU work disappear.

## Limits and deferred work

The 64 MiB limit counts direct command metadata, payload and range capacities, including inline-byte heap fallback. It is not a total shared-resource memory limit: one outstanding completed batch can retain additional immutable resource references until cleanup runs. There is no unbounded job backlog. Payload retention is bounded separately. Watch footprint/compression, helper completion and fallback counters on smaller devices before wider rollout.

This pass does not add early constant materialization or additional constant helpers. Published constant versions contain mutable parent/materialization memoization, and the current upload arena has one owner. Earlier overlap requires private immutable preparation inputs and an ordered publication design. Static pipeline identity redesign is also deferred. Build 89's memory pressure recovery, pipeline receipt cache, renderer reuse, parallel constant/index and texture preparation are retained.

## Controls and undo

System has two new restart-required switches, both default ON:

| Switch | Effect | Undo |
| --- | --- | --- |
| Command Stream Optimization | Producer state packets, retained full command ownership, bounded empty payload capacity reuse | OFF, then fully close/reopen |
| Background CPU Cleanup | Bounded utility cleanup of completed CPU records; requires Command Stream Optimization | OFF, then fully close/reopen; ownership/packet changes remain |

Keep the controls separate during the comparison. Consolidate proven defaults into an Advanced section in a later pass after evidence. For the old processing path, turn both OFF and fully restart. The signed build 89 app and matching dSYM remain the exact whole-build rollback.

## Comparison instructions

1. Keep Native resolution, the same save/graphics settings, Direct Guest Clock ON, Runtime Wait Improvements OFF, and the existing efficiency/preparation/assembly/memory controls ON. Keep both new controls ON for the first run. Do not clear the learned pipeline recipe cache.
2. Fully restart. First run without logging: hold the same apartment position/camera through the first hitch and 30 seconds after it; then go outside to the same view for at least 60 seconds. Note recovery, settled FPS/frame time, native/producer CPU, pop-in and physics separately.
3. Repeat after a full restart with Long Performance Capture enabled. Follow the same still view and route. Stop/save capture; wait five seconds. No external Instruments capture is needed for this comparison.
4. If cleanup causes a regression, turn only Background CPU Cleanup OFF and fully restart for isolation. Turn Command Stream Optimization OFF as well to compare the previous processing path. Change one new group at a time.

Compare sustained total CPU, native CPU outside Publish, sparse assembly insertion/clear costs, packet counts, utility CPU/job, fallback rate, memory accounting and fixed-probe regime. A shorter spike alone is insufficient to call the sustained issue fixed.

## Baseline and delivery

Baseline source: `d8ae10239085384f2bd93810a31dd5a6e8ed2532` (build 89).

Baseline signed app: `out/backups/build89-pressure-and-assembly/Theft4.app`; executable SHA256 `27ac780a453ee3aff3075c84b2fa8aca6ee8fccb4acbc6ae865621bd5d8e0820`, UUID `63F05DFA-2CBE-35A5-8888-BE6C2D5ACBFE`.

Build 90 source commit, Release result, app/dSYM identity and signature are recorded in `artifact.json` / `SOURCE.json`. M5 save/profile/preferences backup and final installation/closed-state check are recorded in `m5/INSTALLATION.json`. Other devices are unchanged. No automated tests or agent gameplay run; performance validation is pending the user's run. Install and leave Theft4 closed.
