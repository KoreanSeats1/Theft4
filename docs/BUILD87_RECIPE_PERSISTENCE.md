# Build 87 — recipe persistence and fair compilation

## Basis and scope

Base: build 86, source `3c758f66de5732b627ac492d9fa43fe6a2255b84`, clean isolated worktree `/Users/lukebrosious/.codex/worktrees/m5-pacing-control/Theft4 Project`. Branch `codex/recipe-persistence-87`. Signed build 86 and matching dSYM remain in `out/backups/build86-parallel-preparation/`; BASELINE.json records their identity. Main-checkout source changes are untouched.

The September 30 OFF capture had 107 pipeline publications and 20.389 seconds of accumulated asynchronous driver compilation around outdoor entry, with 63 jobs outstanding. The renderer deferred draws whose exact pipeline was unfinished. This explains a concrete delayed-geometry mechanism. It does **not** explain the persistent slowdown after the queue drained or the first indoor throughput transition.

The saved 323-record recipe file had a valid payload, but its identity was computed using the old compiler timestamp. Build 86's newly compiled timestamp caused it to reject that file. Recipe saving was coupled to driver-cache serialization, and iOS periodic serialization had correctly been disabled after earlier stalls. The result was no usable recent learned-recipe replay despite a driver cache being loaded.

## Changes

1. Replace the compiler-date identity with explicit schema/semantic revision 2 and ABI guards (record/key/snapshot sizes, alignments, important offsets, pointer size and dynamic-state list). Compatible rebuilds reuse recipes. Changes to member meaning/order, descriptor layout, entry point or reconstruction semantics require a revision bump. Existing device/driver-specific filenames and exact shader-code checks remain required.
2. Use a separate `.recipes-v2` sidecar, preserving the old `.recipes` for rollback. Legacy records are not blindly imported. The first build-87 run learns new records; replay can benefit subsequent launches once the relevant shaders register.
3. Save bounded CPU-only recipes on the existing writer, at utility QoS on iOS, at most once per five seconds while dirty. A generation captured under the record mutex is acknowledged only after atomic replacement succeeds. Failures keep the previous file and dirty state. Allocate outside the record lock. Maximum 4096 records, approximately 6.6 MB payload; no growing write queue.
4. Keep live iOS driver-cache serialization disabled. Recipe checkpoints never call Vulkan or inspect guest memory. Shutdown joins compiler then writer and saves outstanding recipes independently before the driver cache. Desktop retains its prior 30-second driver-cache interval.
5. Demand is a persistent job property. Visible/demanded pipelines use FIFO ahead of speculative jobs. Polling a pending draw cannot reorder an already demanded job. Admission can evict only unstarted speculative jobs; it cannot evict demanded jobs. A full demanded queue retains existing bounded nonblocking retry behavior.
6. Remove redundant condition-variable notifications from pending-result polling. Enqueue wakes the worker, and completion/stop wakes result waiters.
7. Limit replay inspection to a round-robin slice of 64 recipes per invocation. Missing shader/backend records rotate for retry; unsupported/mismatched records are removed and mark persistence dirty. Compiler capacity stops further admission. A restored large cache no longer requires a full scan on every publication.

Compiler concurrency stays at one; build-86 parallel constant preparation stays independently selectable. No changes to guest clock, physics, resolution, game-mode metadata, frame scheduling or Sustained Execution policy. No settings migration is introduced. The M5's last saved Parallel Render Preparation setting was OFF from the comparison; set it ON for normal use and the build-87 comparison.

## Review and limitations

Records are copied under their mutex, file I/O occurs afterward, and only the writer (or joined shutdown) saves. Compiler idle and renderer publication can request saves concurrently; an atomic interval claim coalesces requests. Shader modules, layouts and driver cache outlive the compiler, and compiler-state/path lifetime outlives the writer. The compiler thread's initialization hook sets only its own writer name/QoS; it does not read partially initialized state.

Queue review covers pending and completed duplicates, speculative promotion once, demanded FIFO, full speculative/mixed/all-demanded queues, active work, completed results and stop/join. Active driver work is never evicted. Visible jobs can still wait behind one active speculative driver call; the driver call cannot safely be cancelled.

This preserves nonblocking first use, so entirely new pipelines may still cause temporary missing draws. A recipe is a compilation description, not a precompiled GPU executable; useful replay still needs the exact registered shaders and driver work. Cache persistence/fairness is not a claimed cure for the sustained CPU-throughput drop. Utility QoS does not force an efficiency core or guarantee zero checkpoint cost. Gameplay/performance validation is pending the user's run.

## Next CPU/threading opportunities, in order

1. Retain Parallel Render Preparation ON. In the comparable outdoor interval ON recorded about 29.3 ms native CPU plus 2.6 ms helper CPU per frame, versus 33.9 ms native CPU with OFF. Command counts were similar, but fixed-work probes differed by 4–9%; the entire frame-time difference cannot be assigned to the helper.
2. Separate the remaining native assembly cost before moving it. About 16.3 ms/frame of native CPU was outside the measured PublishFrame span in the ON outdoor interval. Worker queue transfer, state application, constant delta application, SnapshotPipeline and TryPrewarmDrawPipeline are source-confirmed work in that interval. The current capture does not apportion those milliseconds among them. First reduce redundant state/snapshot/key work, then move independent derivation from immutable snapshots into a bounded helper, preserving command order and ownership of caches and GPU resources.
3. Decouple CPU texture conversion from guest capture. CaptureTextureResource currently walks tiled guest data, performs format/endian conversion and creates payloads before resource publication. A future job must own a stable source copy and resource generation, have bounded bytes/tasks, and join before its upload is needed. Raw guest pointers cannot simply be passed to background workers. This targets streaming stalls; it is not yet measured as the steady-state bottleneck.
4. Consider parallel command encoding only after the first two costs are established. Resource transitions, render-target aliasing, descriptor arenas and publication currently share ordered ownership. Splitting encoding without redesigning these dependencies risks correctness and driver contention. More workers or fixed core affinity alone are not an implementation plan.

## User comparison

Keep Native resolution and existing graphics/scheduling settings. Enable Parallel Render Preparation ON and fully restart. Run the usual apartment/outdoor route once to learn pipelines; allow at least five seconds of active play after new scenery finishes appearing, then fully close/reopen and repeat the same route. Save a long capture on the repeat run if practical. Compare delayed models, compilation backlog and post-spike frame times separately. Do not clear the cache between these two runs.

Rollback: reinstall the preserved build-86 app in place without uninstalling; saved data remains in the same container and the legacy recipe file remains present.

## Delivery status

Compilation, signing and installation records are stored alongside this document. See artifact.json and m5/INSTALLATION.json for the final verified state. The agent does not launch gameplay or run a test suite.
