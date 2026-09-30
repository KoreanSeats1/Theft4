# Build 88 — bounded CPU work and preparation

## Request and baseline

The user authorized implementing the identified optimization/threading targets that have a limited, defensible ownership change, and asked for a broader opportunity audit. Build87 is the baseline, source `43baa5bdf9d90ca93ecc29f0c8e652d57ef0d430`, signed app/dSYM preserved under `out/backups/build87-recipe-persistence/`. BASELINE.json records the rollback identity. Work uses the existing isolated checkout on `codex/bounded-cpu-work-88`; the dirty main checkout is preserved.

The measured shared CPU-throughput transition remains unresolved. These changes reduce or overlap work needed at the slower operating level; they do not request fixed clocks, force numbered cores, or establish a cure for the transition. Broad improvements can have interactions, so two new independent restart-required switches preserve the previous paths.

## Implemented targets

| Target | Implementation and limit | Intended benefit |
| --- | --- | --- |
| Repeated pipeline request derivation across material/snapshot changes | Renderer-owned 32-set, four-way cache of successful requests; complete fixed state/context equality, shader identity, declaration generation and stream strides; no locks or per-request allocation | Skip repeated validation, key construction, vertex-interface search and global pipeline lookup for an exactly matching request |
| Snapshot allocation after setters restore the same state | Exact comparison of effective state after the existing fast version check misses; retain resource identities, texture slots, all stream fields, surfaces and trace fields; track the source version separately | Avoid allocation, copies and reference-count changes for a net no-op |
| Duplicate shader lookup on a changed setter | Reuse the resolved shader pointer already obtained by ApplyStateCommand | Remove one map search per changed shader setter |
| First-use index endian conversion on the recording path | Existing constant helper prepares up to 32 cold buffers / 2 MiB per publication into private vectors; render worker publishes those results after joining | Overlap first-use CPU index conversion with texture preparation without adding another geometry worker |
| Per-block copies of ordinary linear texture images | Copy/swap a complete visible row when source/host block sizes and endian granularity match | Reduce function-call/addressing overhead, preserve pitch padding and byte semantics |
| Larger CPU texture format conversions | Capture the exact needed guest blocks into owned packed storage, then split disjoint output rows between the capture thread and one Dispatch helper; CTX1, DXN, DXT5A and DXT3A only | Shorten larger CPU expansion operations while retaining capture/publication ordering |

### Pipeline reuse correctness

The selector only chooses a set; collisions require exact memberwise equality before reuse. All fixed-function fields remain in the key, including validation-only and dynamic inputs. Context retains pipeline lifetime, layout/backend, shader settings, target formats/usage/dimensions, samples, primitive and user-pointer mode. Shader objects remain alive until renderer teardown; vertex declaration generations distinguish replacements. Lifetime invalidation prevents old Vulkan handles being reused after renderer destruction. Null, rejected and pending pipelines are never inserted. Diagnostic full-pipeline trace and hot-cache validation retain the original path. An existing one-entry snapshot memo is tried first; this bounded cache helps requests that memo cannot satisfy. Required vertex-stream results may accompany a validated receipt.

This does not alter the on-disk pipeline recipe or its reconstruction semantics; the build87 v2 recipe format remains compatible.

### Texture ownership and scheduling

- Submission holds `command_capture_mutex_` through capture and join. One texture helper at most can be outstanding across producers. The title transaction is not released early and raw guest pointers are not passed to the conversion helper.
- Gather reads only the blocks the original serial code reads. It does not bulk-read tile holes or assume padded guest memory is mapped. Address calculation/untile gathering remains on the capture thread; this pass parallelizes CPU format expansion, not the entire untile/streaming subsystem.
- Eligibility: at least six reported physical/active CPUs, at least 4096 blocks and 16 rows, at most 4 MiB owned input and 16 MiB output per subresource. Smaller/unsupported/native compressed cases use the serial path. The CPU count is a heuristic, not an asserted optimum for every chip.
- The same Dispatch task primitive uses user-initiated QoS, one serial queue, and a blocking group join. No spin wait, per-texel tasks, core affinity or unbounded job queue. With the existing constant/index helper, at most two added preparation tasks may overlap.
- Destination rows/slices are disjoint. Destination storage and the conversion descriptor stay alive until join, including scope-exit handling. Floating-point environment is copied/restored for CTX1 palette math; failure uses the serial remainder. Optional source-allocation failure uses the original serial path.
- Native compressed GPU formats remain compressed. Offloading their cheap block copies after adding a gather copy is not assumed beneficial.
- Bulk rows preserve endian grouping. Cases with blocks smaller than their endian unit retain the original per-block behavior. Only visible source/destination row bytes are copied; padding retains initialization.

### Index ownership

The existing helper reads immutable buffer payloads and creates private conversion results. It does not mutate resource cache vectors or GPU state. Renderer accounting/reclamation can therefore inspect existing cache metadata during texture preparation. After join, the render worker moves results into the same host index caches used by the original upload path and records lifecycle events. Requests are deduplicated within the bounded job list. The current frame retains buffer lifetimes throughout the helper. Allocation failure leaves the buffer for original demand conversion. No new shader-constant algorithm or extra index worker is introduced.

## Controls and counters

System → **Frame Assembly Reuse**, default ON: pipeline request cache, net-no-op snapshot reuse and bounded index preparation. System → **Parallel Texture Conversion**, default ON: eligible parallel format expansion and linear row batching. OFF plus full relaunch restores each corresponding build87 path. The equivalent shader-pointer reuse is unconditional.

Existing **Parallel Render Preparation** must also be ON for the constant/index helper. Its saved preference is retained; the last inspected M5 preference was OFF after the build86 comparison. New controls default ON without migrating other saved settings.

Light capture grows from 185 to 199 columns: assembly mode, net-no-op reuse, request/cache-hit counts, texture requested mode, job/source-byte/copy/helper/join counters, bulk rows/bytes, and per-publication index count/bytes. Assembly and texture counters are cumulative, index counters are per-publication, texture spans are wall ticks and may overlap. Existing helper CPU accounting includes index work. A smaller native-thread number alone is not proof of less total process work. No per-draw timing syscalls or external profiler were added.

## Opportunity audit and boundaries

This is a source/evidence-based inventory of the relevant renderer/runtime paths, not a claim to have enumerated every possible optimization in the game.

| Area | Disposition | Reason / next boundary |
| --- | --- | --- |
| Repeated command/state setup | Implemented above; existing compact packets and producer duplicate filtering retained | Further packet coalescing would add scanning to already-filtered setters; benefit not established |
| Snapshot lifetime and allocation | Net-no-op reuse implemented; pool retained | Snapshot destruction can cross producer/renderer boundaries through recycling, so replacing the synchronized pool with an unsynchronized pool is unsafe |
| Pipeline validation/key/lookup | Bounded successful-request reuse implemented | Full exact comparisons and lifetime checks remain necessary |
| Constant conversion/uploads | Existing86 overlap, immutable/version/content/projection caches retained; index work added | Further splitting constant caches would require separate arenas/caches and increase join/merge work |
| Geometry/index preparation | Cold index work implemented | Vertex variants have CPU eviction and GPU-resident fast paths; blindly preconverting would reintroduce redundant work when GPU data is already resident. A future immutable job plan must be built by the render owner after resident-cache checks |
| Texture conversion | Owned-block expansion and bulk rows implemented | Full async resource publication needs generation-aware ready states across hash/dedup/dirty/virtual-texture paths; current change keeps those transactions intact |
| Texture upload and allocation | Existing pools/reuse and failure recovery retained | Vulkan images, views, descriptors, shared upload arena and transitions are ordered renderer state; parallel allocation/recording is a larger ownership change |
| Descriptor and dynamic-state encoding | Build85 reductions retained | Shared frame descriptor arenas and state lifetimes prevent arbitrary draw splitting |
| Render-target transitions and pass merging | Existing independent barrier batching retained | Aliased surfaces, resolves, stencil/depth handoff, HUD and postprocessing require explicit dependency proofs before more fusion/discard |
| Pipeline compile/prewarm/persistence | Build87 stable recipes, fair queue and bounded replay retained | More compiler workers can contend in the driver/shared cache. Main sustained slowdown remains after compilation drains |
| Asset I/O | Existing native async read path retained | Latest outdoor-entry host reads average0.166ms; no second-scale host-read queue was observed. CPU conversion is a more concrete target than extra disk workers |
| Audio/runtime/guest simulation | Preserve current time/wait/scheduling behavior | No evidence of doubled audio batch count; cadence remains187.5 batches/s in earlier traces. Work stealing guest execution or changing the30FPS simulation contract is not a safe renderer optimization |
| CPU placement/power | Existing OS scheduling and Sustained request retained | Effective-throughput transition is measured; exact platform controller cause remains unestablished. More even bars or more runnable threads do not establish a win |
| Direct Metal / Metal4, MetalFX, frame generation | Larger future features | Require actual backend/interop, shader ABI, temporal motion/depth/history and presentation work; user previously kept these separate from the current renderer optimization scope |

## Validation and use

Release compilation, source review, signing/dSYM and installation checks are recorded alongside this file. No test suite or gameplay is run by the agent. Actual speedup, overhead and visual correctness remain pending the user's run; safeguards and a successful build do not prove those outcomes.

For normal use, keep both new controls ON and enable Parallel Render Preparation. Fully close/reopen after changing switches. Use the same Native graphics settings and usual route; check texture appearance, geometry and post-spike timing. The first visit may still compile new pipelines. If behavior regresses, disable either new control independently and fully restart, or reinstall the archived87 app in place. Do not uninstall/delete saves or clear the v2 cache for rollback.

## Delivery

See SOURCE.json, artifact.json, REVIEW.json and m5/INSTALLATION.json for final source/build/device identity and status. Main-checkout application source was not modified. The M5 is the installation target; other devices are not part of this update.
