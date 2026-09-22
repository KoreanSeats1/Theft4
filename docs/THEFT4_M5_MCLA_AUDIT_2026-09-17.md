**Theft4 iOS / M5 audit — MCLA lessons, 17 September 2026**

The strongest optimization candidate is a narrow correction to **guest texture-content identity**. Theft4 already has a separate effective Vulkan sampler cache, but the CPU texture snapshot cache compares sampling fields as content. Keep the existing phase-accumulating frame limiter, Vulkan/MoltenVK renderer, material-filtering policy, and AMD FSR 1 passes. Improve measurement and failure visibility before deciding which runtime experiment to implement.

Per your follow-up, this deliverable is an audit and ordered test plan. **No runtime optimization was applied, no iOS app was built or installed, and no new device performance test was run.** The only new executable is a host-only audit test binary. Its synthetic cache counts are not device performance measurements.

**1. Preserved baseline and branch**

| Item | Recorded evidence |
|---|---|
| Original checkout | `/Users/lukebrosious/Documents/ChatGPT/Theft4 Project` |
| Original branch / commit | `main` / `a25c54cffa1d1d9823f8ea0149c7c00df8b04ebb` (`v0.1.2`) |
| Experimental branch | `codex/ipad-m5-mcla-experiments` |
| Isolated worktree | `/Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/experiments/ipad-m5-mcla` |
| M5 source baseline commit on experiment branch | `814905f9` — preserves the existing eight modified tracked files, 72 added lines |
| Frozen M5 reference | [m5-stable identity](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/device-builds/2026-09-17/m5-stable/identity.json>) |
| Preserved app SHA-256 | `b6537db28971738fc927894763c27e1cd5e7d0f77930fdfbce96907b8982b88c` |
| Preserved executable size | 82,683,648 bytes |
| Main build's executable | Independently hashed; identical to the frozen M5 executable |
| Build configuration | Release; bridge, native renderer and AOT compiler response files contain `-O3 -DNDEBUG`; renderer and AOT use `-mtune=apple-m5`; ThinLTO off |
| Renderer switches | Native backend compiled/enabled; game startup enabled; XeniOS direct resolve off in frozen M5 configuration |
| Device inventory | Physical M5 iPad connected; Theft4 `com.theft4.bringup`, version 0.1.2, build 4 installed |
| Device activity | Existing Theft4 PID 865 observed; left untouched. Installed executable hash was **not** independently retrieved |

All 12,729 tracked file/symlink contents were recorded. Every comparable tracked source file matched the frozen `m5-stable/source` copy. The one file-only comparison exception is the `LibertyRecompResources` symlink. Eight modified files and six dirty dependency submodules existed before this audit. Their state and patch fingerprints were preserved in [original-state.json](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/original-state.json>) and [original patch](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/original-working-tree.patch>).

The isolated branch includes the tracked M5 changes; it does not claim that all submodule working-tree patches are committed or that a fresh clone reproduces the app. Dependency patch snapshots are saved beside the audit evidence; frozen M5 dependencies remain intact. The experiment worktree has its own source tree, and any later device build must use a separate build directory. Neither the original nor frozen M5 build directory was regenerated.

The historical M5 log confirms 1280×720 scene, FSR 1080p output, sharpness reduction 0.2, 4× material anisotropy, two native frame slots, and `gta4-native` on Apple M5. These are observed settings for that session, not proof of the currently running process's full settings. See [historical session](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/m5-historical-latest-session.log:1>), [startup policy](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/ios/bridge/theft4_startup.cpp:147>), and [M5 configuration](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/device-builds/2026-09-17/m5-stable/configure-command.json>).

**2. Findings and decisions**

| Prompt topic | Classification | Recommendation | Expected benefit / regression risk |
|---|---|---|---|
| CPU texture identity versus sampling | **Likely beneficial** | Measure sampler-only cache misses, then test a conservative content predicate | Potentially substantial CPU/upload reduction if this happens often; medium correctness risk |
| Effective Vulkan sampler key | **Already correct** | Preserve | No duplicate sampler-key rewrite warranted; low risk if untouched |
| CPU 30 Hz deadline accumulation | **Already correct** | Preserve; measure display cadence separately | No demonstrated benefit from replacing it; medium pacing risk if changed |
| Actual presentation measurement | **Worth measuring** | Add supported Vulkan timing telemetry in a later isolated measurement build | Enables trustworthy acceptance; low-to-medium integration risk |
| Production diagnostics | **Already correct in several major paths; worth measuring elsewhere** | Keep existing gates; profile remaining copies, validation, locks, and semantic hashing separately | Unknown until attribution; medium/high risk from indiscriminate removal |
| Shader-corpus completeness | **Likely beneficial** | Keep decoded-stage inventory tool; collect a complete set of observed registrations later | Detects omissions early; low runtime risk for an offline report |
| GPU resource completion ownership | **Already correct in inspected normal paths; worth measuring under lifecycle faults** | Keep two-slot ownership and retirement; test suspension and failure recovery | Primarily stability; high risk from shortcutting fences |
| Filtering eligibility | **Already correct for explicit exclusions; worth measuring UI edge cases** | Keep 4× and test HUD/minimap; no broader override | Preserves image quality; medium risk from broadening eligibility |
| Bounded per-reason failures | **Likely beneficial** | Separate total failure counts from verbose trace enablement | Better diagnosis, not an FPS claim; low/medium implementation risk |
| Direct-Metal pipeline/argument-buffer fixes | **Not applicable** | Do not port directly | No established benefit to current Vulkan path; high architectural risk |
| Taking over MoltenVK drawable presentation | **Too risky for current renderer** | Stay within Vulkan ownership | Avoids competing presentation/resource ownership |
| FSR/output policy | **Already correct algorithm and resolution policy; worth measuring lifecycle/per-pass cost** | Preserve EASU/RCAS; audit existing intermediate reuse | No replacement needed; medium risk if resource ownership changes |
| Generic fallback during native initialization | **Worth correcting for acceptance visibility** | Reject such runs; later add a strict test mode that fails visibly | Prevents testing the wrong renderer; low implementation risk |

**3. Texture path: the main actionable finding**

The source cache is a handle-to-immutable-resource map. On a normal CPU texture hit, `CaptureTextureResource` requires equality of **all six fetch DWORDs** plus clean/font identity checks. After a miss, it decodes/copies the requested mip payload, hashes the payload, compares all six DWORDs again, and can assign a new generation even if the decoded texels are unchanged. Host image lookup then keys on that generation. This establishes a concrete route from a guest sampler-only change to reconversion, a new generation, a new image/view and another upload. It does not establish its frequency during driving.

Evidence: [first cache predicate](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:5178>), [conversion loop](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:5255>), [second predicate and generation](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:5411>), [image-generation lookup](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:15763>), [upload](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:16004>).

The layers must remain distinct:

| Layer | Current behavior and required protection |
|---|---|
| Guest source/content | Handle, fetch descriptor and immutable generation; dirty state governs reuse. Preserve source and mip addresses, format, endian, dimensions, pitch, tiling, packed mips, array/cube/3D identity and conversion-affecting fields |
| Mip content | Only `mip_min_level..mip_max_level` is decoded. **Retain these fields in the first correction**, even though they also affect sampling; masking them risks absent mip data |
| Image/view | Images are cached by generation; view dimension/subresource range/swizzle are created from the resource. Retain swizzle in the conservative content predicate until an explicit independent view cache is designed |
| Sampler | Key already represents effective Vulkan filters, address modes, LOD limits, enabled anisotropy and relevant border color, with inactive fields normalized |
| Guest writes/unlock | Dirty regular textures; virtual guest writes invalidate host ownership, including companions |
| Guest release/reuse | Erases source/dirty/font/virtual mappings and queues generation retirement; do not allow equal fetch bits to reuse a released lifetime |
| GPU-produced textures | Existing virtual-lifetime, packed-depth and shape-based reuse is a separate path. Do not apply a CPU-payload identity patch across it |

See [Xenos fetch fields](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/include/rex/graphics/xenos.h:1168>), [effective sampler key](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/native_sampler_cache_key.h:25>), [sampler lookup](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:16361>), [unlock/release](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:4102>), and [view creation](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:15924>).

The CPU texture hit returns before payload conversion/hash, so it is **not** recopying every unchanged texture on every draw. Also, changing the **host** filtering override is already downstream of content capture and is sampler-only. The problematic route is the guest fetch changing sampling bits, not simply selecting 4× in the launcher.

The audit-only proposal masks 43 established sampling bits while retaining the other 149 bits. It is deliberately more conservative than copying MCLA's entire mask: uncertain fields remain significant. Named-field tests verify format/endian/addresses/pitch/tiling/mips/dimensions/swizzle changes still differ. A 100-request alternating-filter model gives **100 current comparison misses versus 1 proposed comparison miss**. These are simulated predicate counts; no decode/upload counter or FPS gain has been measured. The test is [audit_m5_policies.cpp](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/experiments/ipad-m5-mcla/tools/audit_m5_policies.cpp:1>); the production predicates remain unchanged.

The smallest later runtime experiment is to share one conservative content-comparison helper between the two CPU predicates, preserve all dirty/lifetime/font checks, and keep the current per-draw fetch for sampler/shader state. Count sampler-only mismatches, real content changes, reconversions, decoded/uploaded bytes, generations, image/view creation and sampler hits. Reject the experiment if any same-handle guest write, release, mip/layout or produced-target test becomes stale.

**4. Pacing and what the FPS counter means**

Theft4 already has MCLA's transferable CPU-clock concept. `AdvanceDeadline` carries fractional nanoseconds; `Plan` advances from the old deadline, tolerates less than one full missed interval without changing phase, and resets to `now + period` after greater lateness. It schedules no multi-frame catch-up loop. `PaceNativePresent` calls it and sleeps until the planned target. The audit test covers 300 periods at 30 Hz with ±1 ms jitter: the deadline advances **exactly 10,000,000,000 ns**; a simulated five-second suspension resets and the following early frame waits.

Sources: [clock](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/gta4-recomp/src/gta4_frame_limiter.h:32>), [caller](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/gta4-recomp/src/gta4_native_hooks.cpp:351>), [existing tests](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/tests/unit/system/gta4_frame_limiter_test.cpp:16>). This validates the clock, not display pacing or lifecycle suspension.

Darwin defaults to strict FIFO and disables immediate presentation. The CPU limiter, native slot fence waits, Vulkan image acquisition and display FIFO can all impose waits; whether their combination produces long frames remains unmeasured. Do not add another independent 30 Hz sleep. See [present-mode defaults](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp:67>) and [mode selection](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp:1671>).

The earlier allocation-version counter defect has already been corrected with a content sequence. However, the current iOS counter increments following successful/suboptimal **`vkQueuePresentKHR`**, not a GPU-completion or actual scanout callback. The overlay therefore reports accepted distinct-content present requests. Sparse milestone logs cannot provide presentation median/p95/p99. See [counter boundary](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp:3080>).

A later measurement build should query supported `VK_GOOGLE_display_timing`, associate present IDs with content sequences/swapchain epochs, and asynchronously drain actual presentation timestamps. Khronos documents this query as delayed, bounded history, so detect missing records rather than silently constructing intervals across gaps. MoltenVK lists support, and the local source records actual times; availability must still be checked against the **linked M5 archive and device**, not assumed from today's upstream docs. See [Khronos timing API](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingGOOGLE.html), [MoltenVK guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md), and [local timing history](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/thirdparty/MoltenVK/MoltenVK/MoltenVK/MoltenVK/GPUObjects/MVKSwapchain.mm:259>).

Keep CPU submission, native GPU completion, present requests and actual presentation as four separate measurements. Do not call `presentDrawable:atTime:` on drawables owned by MoltenVK.

**5. Production diagnostics and safety checks**

Several of MCLA's removals are already present: shader container hashes are computed during registration; vertex declarations are hashed when registered; draw-time code reuses stored identities. Semantic diagnostic hashing is allocated only when legacy/transition diagnostics request it. Full device snapshots are gated by transport/diagnostic/validation settings, and normal transport defaults to `delta`. Profiling scopes avoid clock reads when no recorder is active. References: [shader registration](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/gta4-recomp/src/gta4_native_hooks.cpp:3632>), [declaration registration](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:6542>), [diagnostic hash gate](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:25736>), [snapshot gate](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:4398>), [profile scope](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/native_cpu_profile_scope.h:11>).

Remaining candidates require attribution:

- A `std::string` transport setting is copied for each draw/clear. Cache a validated enum at the appropriate setting boundary if profiling justifies it. `REXCVAR_GET` is a storage accessor, not intrinsically a string lookup; avoid overstating its overhead.
- Frontend source capture and worker paths share texture/buffer/state mutexes. Measure wait duration and contention before changing ownership or making caches lock-free.
- Lighting draws intentionally capture/hash full constants independent of diagnostics and compare them with reconstructed worker constants. Those checks protect semantic state; do not classify them as disposable traces.
- Clean vertex/index snapshots still perform bounded rolling guest-memory comparisons and disable the fast path on mismatch. This is a mutation-detection safeguard, not evidence that immutable textures are fully revalidated every draw. Removing it requires authoritative write/generation coverage first.

Evidence: [CVar accessor](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/include/rex/cvar.h:314>), [lighting capture](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:4458>), [constant verification](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:6515>), [buffer safeguard](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:4787>). No guest safety check was removed.

**6. Shader corpus: decoded inventory, limited live coverage**

The offline audit decoded actual Zstandard/SMOL-V contents and inspected SPIR-V entry-point execution models. It found:

| Check | Result |
|---|---:|
| Packaged guest shader identities | 1,356 |
| Decoded vertex / pixel programs | 650 / 706 |
| Additional late pixel variants | 706 |
| Unique logged stage/hash identities across supplied M5 logs | 19: 9 vertex, 10 pixel |
| Registration/miss observations before deduplication | 518 |
| Missing observed vertex identities | None detected (`[]`) |
| Missing observed pixel identities | None detected (`[]`) |
| Observed stage mismatches | 0 |
| Groups of byte-identical early modules | 203 groups, covering 1,000 guest identities |

Early/late modules are variants, not another 706 guest programs. Identical compiled modules do not justify merging guest identities: metadata, constants and original title semantics can differ. The 1,356-entry table itself is sorted, has no duplicate hash entries, valid inspected ranges and texture masks, and structurally valid decoded stage/entry-point records. This was structural inspection, **not a new `spirv-val`, device compilation, draw-acceptance or visual correctness test**.

The shader blob is actually present in the preserved executable: **1,007,263 compressed bytes at offset 60,800,288**, SHA-256 `4eac0aac5c9cc30a5ac41569e85fc54654ffcca65c332e5cc2474642f10a45ad`. See [embedded verification](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/embedded-shader-verification.json>), [full corpus inventory](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/shader-corpus.json>), and [read-only comparison tool](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/experiments/ipad-m5-mcla/tools/audit_m5_shader_corpus.py:1>).

**This does not establish full corpus completeness.** Normal registration logging prints only the first 16 and every 256th registration, explaining the small observable set; 1,337 packaged identities were not observed in these logs. There is no complete new M5 route corpus. See [registration log gate](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:7182>).

Runtime stage screening also relies partly on `_vs`/`_ps` in filenames, while **89 entries have other naming forms** (`.fxc`, `.vert`, `.frag`). Keep decoded execution-model checks in the offline report and consider explicit stage metadata for build validation. The current runtime's pixel reflection supplies additional checks; the filename heuristic alone is not a complete proof. See [stage heuristic](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:1465>) and [pixel output validation](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:6920>).

The repository already contains cache addition/preservation validators; extend those with a complete observed stage/hash manifest rather than building a second shader pipeline. Capture registrations once per identity in a separate diagnostic session, then compare offline. No MCLA shader hash belongs in Theft4's corpus merely because it fixed MCLA.

**7. Completion ownership, lifecycle and backend identity**

The prompt's “three-frame Theft4 system” assumption does not describe the active renderer. **The GTA IV Vulkan resource ring has two slots**, and current iOS startup selects two (with an explicit one-slot override). The separate Metal helper has different bookkeeping and its `note_draw` does not encode scene geometry. See [ring size](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/native_frame_context.h:15>), [startup](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/ios/bridge/theft4_startup.cpp:181>), and [Metal helper](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/ios/bridge/theft4_metal_presenter.mm:219>).

The inspected normal Vulkan path waits for the exact submission before acknowledging slot reuse, releasing retired images/descriptors, and resetting frame-owned constants/uploads. Queued resources retain immutable generations; image use is stamped with submission identity. Guest-output publication carries a submission completion object. Shutdown drains active and secondary slots; if necessary it waits for device idle and refuses ordinary destruction when completion cannot be established. These are protections to preserve, not replace with Metal callback code. See [completion and recycling](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:7978>), [published completion](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:32924>), and [shutdown](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:33229>).

Two lifecycle gaps warrant independent tests. The launch overlay retires on the guest entry-point event, not first GPU completion. Scene resignation calls `theft4_core_pause`, which changes shell state; that function does not pause the actual game/runtime worker. The inspected scene callbacks do not establish coordinated GPU background quiescence or device-loss recreation. Earlier city evidence records wait errors after backgrounding; do not relabel them as foreground slowdown. See [overlay](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/ios/Theft4/main.m:291>), [pause](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/ios/Theft4/main.m:472>), [shell state](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/ios/bridge/theft4_core.cpp:74>), and [historical lifecycle finding](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/THEFT4_CITY_PERFORMANCE_2026-09-16.md:29>).

There is also a **logged automatic generic fallback if native factory initialization fails**. It is not a missing-shader fallback, and it is not silent, but it can violate an acceptance run's intended backend. Successful historical logs select native. Require an explicit backend witness for every future artifact/run, and later add a strict acceptance option that aborts visibly on native initialization failure. See [fallback](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/ios/bridge/theft4_bootstrap_graphics.cpp:463>). Do not claim “no generic fallback exists.”

**8. Filtering, failure visibility and direct-Metal boundaries**

Filtering eligibility explicitly requires a color texture with more than one mip, linear-filter capability, a non-produced source, and excludes reflection, vector-font replacement and base-map-only sampling. Host overrides update the sampler state without modifying texture content. This preserves the important MCLA lesson. It is a structural resource test, not an explicit “HUD” semantic label, so a mipmapped CPU-backed UI texture could qualify; HUD/minimap visual checks remain necessary. See [eligibility](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:16268>), [filter policy](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/texture_filtering_policy.h:17>), and [anisotropy policy](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/anisotropic_filtering_policy.h:18>).

Rejection helpers have a real visibility weakness: `NextNativeTraceDiagnosticCount` returns `UINT64_MAX` without incrementing the counter when native trace is disabled. Texture capture/image errors then have neither a preserved total nor their bounded message through those helpers. With trace enabled, each helper shares a cap across all its reasons, so one noisy reason can obscure later reasons. Other shader/device errors log separately; this finding does not mean all failures are silent. See [counter gate](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:1186>), [capture rejections](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:4929>), and [image rejections](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp:15733>).

Later use a fixed reason enum, cheap total/per-reason counts, the first few examples per reason, and a periodic/end-of-run summary. Include shader pair, frame, primitive and resource identity when available. Avoid maps, heap allocation and formatting on successful draws. This is diagnostic reliability, not a promised frame-rate gain.

Direct-Metal nil-fragment pipelines, `stepRate=0`, Metal residency calls and dense Metal attribute locations are **not changes for this Vulkan renderer**. Vulkan depth-only behavior and resolve mip/slice correctness can be tested in their own contracts, but there is no evidence here that MCLA's exact defects exist in Theft4. Frame ownership and produced-target lifetime principles transfer; Metal-specific calls do not.

**9. FSR/output implementation**

The output policy keeps a 1280×720 scene, uses 1920×1080 as balanced enhanced output, and bounds Boost to integer 16:9 from 1080p through 3840×2160. FSR 1 is spatial upscaling, not frame generation. See [output policy](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/ios/bridge/theft4_output_policy.h:26>).

Theft4 selects actual EASU and RCAS SPIR-V arrays. Their executable instruction streams match the local AMD-based Xenia reference after removing debug instructions. Those reference sources directly call `FsrEasuF`/`FsrRcasF` and include AMD's `ffx_fsr1.h`. The dither variant has an additional harmless undefined-value result-ID renaming in the compared artifact; no alternate edge-aware algorithm was substituted. All three exact Theft4 arrays were found in the preserved M5 binary. See [shader selection](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp:3438>), [AMD call/include](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/tools/xenia-master-1/src/xenia/ui/shaders/guest_output_ffx_fsr_easu.ps.xesl:64>), [bytecode evidence](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/fsr-bytecode-verification.json>), and [embedded arrays](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/embedded-shader-verification.json>).

One ownership detail differs from MCLA: Vulkan presenter FSR intermediates are **shared within the paint context**, not one texture per native frame slot. Same-queue render-pass dependencies order shader reads and color writes; resize/replacement waits for recorded use, and removal checks completed submission. This is not itself proof of corruption, and allocating three copies blindly is unwarranted. The replacement wait result is not checked before reset, so failed-wait/device-loss handling deserves a focused follow-up. See [intermediate replacement](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp:2329>), [retirement](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp:2406>), and [read/write dependencies](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp:3482>).

FSR cost was not measured separately. HDR has a logged bilinear fallback because this FSR path expects perceptual input; normal iOS defaults to HDR off. Preserve that distinction when verifying the selected output route. See [HDR handling and route witness](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/glue/rexglue-sdk-main/src/ui/vulkan/vulkan_presenter.cpp:2214>).

**10. Exact measurements and their limits**

| Required measurement | Evidence available now | New baseline / runtime experiment |
|---|---|---|
| Actual presented FPS | Not available for Theft4 | Not measured |
| Median / p95 / p99 display intervals | Not available | Not measured |
| Frames above 41.67 / 50 / 66.67 / 100 ms | Not available from sparse milestones | Not measured |
| Historical native present-request cadence, Sept 17 | 2,400 requests over **81.559 s = 29.426550/s**; logged windows **25.960540–30.051087/s** | Historical context only |
| Historical city present-call cadence, Sept 16 | **25.26/s** over 95.015 s; 300-call windows **23.43–27.61/s** in prior report | Older one-slot/settings context; not comparable A/B |
| GPU time / FSR GPU cost | No valid same-route baseline | Not measured |
| CPU render-submission time | No valid same-route baseline | Not measured |
| Texture decode/upload counts and bytes | No acceptance counter set | Not measured |
| Sampler/image/view creates and hits; invalidations | No acceptance counter set | Not measured |
| Shader misses / rejected draws | No observed missing IDs in the sampled corpus; failure totals incomplete | Not proven zero during driving |
| GPU errors / crashes | No matching GPU/device-loss errors in the extracted latest historical session; no full-session crash audit | Not an acceptance result |
| Audio | Latest historical sample: **19,456 blocks; 0 underrun frames, rebuffers, drops, clips, nonfinite samples and recovery-silence frames** | Historical sample only |
| Five minutes actual driving after warm-up | Not performed | Deferred by your instruction |

The Sept 17 request-rate endpoints are 12:17:52.629 at present 600 and 12:19:14.188 at present 3000. The log does not establish a continuous five-minute driving route or actual scanout. Its executable hash is not embedded in the log, so the preserved binary's matching build context is not a cryptographic linkage to that run. See [calculation](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/historical-cadence.json>) and [raw endpoints](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/m5-historical-optimized.log:5983>).

A fresh read-only device copy contained only seven tool-mode preparation lines, not gameplay metrics; it is not a useful timing baseline. The historical logs above were retained separately, and the preexisting running app was not interrupted.

MCLA is supporting evidence only: run5b shows bindings 12.80–13.07 µs/draw and a 22.31 presented-FPS sample; run6b shows 0.63–0.64 µs/draw and 30.06 presented FPS with p95 41.67 ms. The later FSR startup log shows 30.00 FPS, median/p95 33.33 ms and GPU 7.78–7.91 ms in final samples. The former used profiling and the latter was a startup check, so neither proves five-minute paced driving acceptance even for MCLA. Sources: [run5b](</Users/lukebrosious/Documents/ChatGPT/EveningGroupLA/artifacts/mcla-metal-run5b.log>), [run6b](</Users/lukebrosious/Documents/ChatGPT/EveningGroupLA/artifacts/mcla-metal-run6b.log>), [FSR startup](</Users/lukebrosious/Documents/ChatGPT/EveningGroupLA/artifacts/mcla-metal-fsr-smoke7.log>), [acceptance status](</Users/lukebrosious/Documents/ChatGPT/EveningGroupLA/METAL_TRANSITION.md:76>).

**11. Checks performed and disposition**

- Host-only tests: **58 tests / 4,329 assertions passed**, compiled with `-O2 -fsanitize=address,undefined`. Production-header tests cover sampler keys, filtering, frame scheduling/ownership and content policy; additional audit cases cover the proposed identity and existing 30 Hz clock. [Results](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/host-test-results.log>), [exact compiler invocation](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/host-test-command.json>).
- Host audit executable SHA-256: `10f4a4d8656f394361a830d55b3c633e57b1d8462f3c3a68f9845bed86fa02a6`. This is a macOS test artifact, not an iOS candidate.
- Offline corpus decode/metadata comparison: completed; counts and limits above. Keep the report tool. It compares stage/hash identities and records compiled duplicates separately.
- FSR bytecode/source/executable verification: completed. Keep the current FSR implementation.
- Runtime cache correction: **proposed, not implemented**; retain as first performance hypothesis.
- Pacing replacement: **do not implement now**; it duplicates an existing mechanism.
- Broad diagnostic stripping or removal of buffer validation: **reject as an unbounded change**; isolate measured costs later.
- Per-reason logging, strict backend witness and lifecycle work: **planned**, not tested in an app.
- New iOS experiment hashes/results/regressions: **none**. No iOS experiment ran; visual/stability regressions remain untested, not “zero.”

**12. Ordered next steps**

1. **Agree and run the baseline route first.** Use the preserved ordinary M5 Release app, warm caches, keep 4× filtering, the same output/motion-blur settings and two slots, then drive the same route for at least five minutes. No debugger, validation, frame/corpus capture, per-draw trace or heavy profiler. Keep the iPad foregrounded/unlocked; close Theft4 when that bounded test ends. Record save/route, temperatures/thermal state, settings and executable identity. A capture-free playtest can establish visual behavior, but the existing overlay cannot establish display percentiles.
2. **Establish reliable telemetry before optimization.** In a separate measurement-only artifact, check Vulkan timing-extension availability, obtain actual display timestamps if supported, and retain lightweight aggregate counters. Do not change presentation scheduling. Measure the measurement overhead. Keep CPU/GPU detailed attribution in a separate diagnostic run; GPU timestamps should be reported only when the required timing path is enabled and valid, rather than pretending an uninstrumented log contains GPU durations.
3. **Test the texture hypothesis in isolation.** First count sampler-only full-fetch mismatches on the baseline route. If material, integrate the conservative predicate in the isolated branch, add real capture/invalidation tests, then repeat the route. Preserve guest generation/lifetime, full mip/layout identity, produced targets, vector fonts and current sampling state. Keep it only with lower conversion/upload churn and no visual/stability/pacing regressions.
4. **Strengthen coverage and failure evidence.** Collect complete unique shader registrations in a separate opt-in run and feed the offline report. Add per-reason bounded failures with retained totals and a strict native-backend acceptance check. Do not fill imagined missing shader IDs or merge semantic identities on compiled-code equality.
5. **Optimize only the next measured CPU cost.** Candidates are per-draw configuration copies, texture/state lock contention and redundant semantic hashing. Preserve guest-write validation until mutation coverage is proven. Each conceptual change gets its own artifact, hash and same-route A/B result.
6. **Run lifecycle and image-quality gates separately.** Test background/foreground, lock/unlock, shutdown with in-flight work, timeout/device loss, output resize, HUD/minimap, reflections, fonts and audio. Follow up the unchecked presenter-intermediate wait and shell/runtime pause boundary. Preserve two safe slots; a three-slot conversion is separate high-risk work without a demonstrated need.

Acceptance remains approximately 30 actual presented FPS in driving, median near 33.33 ms, p95 no worse than baseline, fewer long frames, no new correctness/audio/save/lifecycle failures, no unintended generic renderer, and no texture-generation churn increase. Until those gates are measured, no performance candidate is approved for the known-good build.

The original tracked files, dirty dependency state and preserved executable were checked again after the audit; see [preservation verification](</Users/lukebrosious/Documents/ChatGPT/Theft4 Project/out/audits/m5-mcla-2026-09-17/preservation-verification.json>). The audit added only the requested Git branch/worktree and local evidence outputs. No original source/build artifact, game data or save was edited; no device launch, install, termination or write was performed.
