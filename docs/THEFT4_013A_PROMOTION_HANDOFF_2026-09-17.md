# Theft4 0.1.3(a) M5 promotion handoff

Source branch: `codex/ipad-m5-mcla-experiments`

Source base: `814905f975d70972d81b2c7c06c3e18340145b0f`

Current source head: `cde6fe2b6fcef76bdc1a5d7076c6a262693e2357`

Do not merge this branch wholesale. It contains Lab identity/build machinery,
diagnostics, an unresolved texture-cache experiment and asynchronous pipeline
behavior that may omit draws while compilation finishes.

## Release-safe shared renderer patches

### 1. Promote `b4563d5d` in full

`b4563d5d5e5b6ec327d3360f14a30ef22f43b6c3` changes only
`graphics_system.cpp`:

- reads the immutable draw-state transport string by reference instead of
  copying it per command;
- avoids a second texture-prefix XXH3 hash for every non-font texture;
- retains the full content hash and the font prefix hash where it is used.

This is semantics-preserving, compiled in every subsequent signed device Lab
build, and was exercised during all reported device runs.

### 2. Promote only the shared renderer and test files from `cde6fe2b`

Use these paths from `cde6fe2b6fcef76bdc1a5d7076c6a262693e2357`:

- `glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp`
- `glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.h`
- `glue/rexglue-sdk-main/src/graphics/gta4_native/native_fixed_function_state.h`
- `glue/rexglue-sdk-main/tests/unit/CMakeLists.txt`
- `glue/rexglue-sdk-main/tests/unit/graphics/native_fixed_function_state_test.cpp`

Exclude its two iOS bridge files. The shared patch replaces 49 seeded XXH3
calls with one hash over 66 explicitly serialized logical words. It retains the
capture/finalize/record integrity comparisons, excludes padding, and preserves
float, signed-zero, NaN-payload and signed-scissor bit identity. The digest is
process-internal and is not persisted.

Validation:

- 68 host test cases and 5,390 assertions passed under AddressSanitizer and
  UndefinedBehaviorSanitizer.
- Optimized ARM64 benchmark: old 291.145 ns, new 15.850 ns, 18.37x for the
  helper in the recorded run.
- The complete iOS Release target compiled, passed release verification and
  passed strict code-sign verification.
- Signed artifact receipt:
  `out/m5-lab/artifacts/cde6fe2b6fce-20260917T234453Z/receipt.json`.
- This exact candidate was deliberately not installed after the release
  promotion decision, so it still needs a normal-app smoke run after promotion.

Suggested extraction rather than a whole-commit cherry-pick:

```sh
git show --format= cde6fe2b -- \
  glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.cpp \
  glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.h \
  glue/rexglue-sdk-main/src/graphics/gta4_native/native_fixed_function_state.h \
  glue/rexglue-sdk-main/tests/unit/CMakeLists.txt \
  glue/rexglue-sdk-main/tests/unit/graphics/native_fixed_function_state_test.cpp \
  | git apply -3
```

The release owner should inspect the staged diff, run the host unit suite and
build/sign the ordinary `com.theft4.bringup` app before committing.

## Optional release tooling

The following commits are build/test tooling and may be promoted independently
if the normal release branch does not already have equivalent checks:

- `2697898e` adds the Release build verifier and tests.
- `636797b1` makes Lab signing-entitlement verification machine-readable.
- `25a4146f`, `498260ed` and `0750e532` concern frozen dependency exports and
  Lab staging. They are not runtime optimizations and are unnecessary for the
  normal app unless that workflow is also being adopted.

## Keep Lab-only or omit from 0.1.3(a)

### Isolated app/worktree machinery

Keep `e079ab7e`, `d202a54e`, `25a4146f`, `498260ed`, `636797b1`,
`0750e532`, `2b8bd8db` and the Lab-specific portions of later commits out of
the shipping app identity. They create or support `com.theft4.m5lab` and are
useful for future experiments only.

### Sampler-independent CPU texture-content reuse

Do not enable or ship the behavior from `d8c75a8a` in 0.1.3(a). Its renderer
CVar defaults to false, while Lab startup forces it on. Device play felt the
same or slightly better after warm-up, but initial loading took longer and new
area stutters remained. No matched FPS or tail-latency improvement was proven.
The helper/tests may remain on the experiment branch.

### Render-worker stall attribution

Keep `7dfff41b` and `062ae98a` Lab-only. They successfully attributed one
504 ms producer stall to `pipeline-wait` at indexed draw 1088/2722, but they add
phase/progress atomic checks throughout hot renderer paths. The installation and
evidence-only documentation commits are `b880ce49` and `fe13ffae`.

### Asynchronous pipeline draw deferral

Keep all of `d7ffffa5` out of the release:

- `gta4_native_async_pipeline_no_wait` and both no-wait paths in
  `graphics_system.cpp`;
- the changed nonblocking promotion behavior in
  `native_pipeline_compiler.h` and its test;
- `THEFT4_LAB_ASYNC_PIPELINES` startup enablement.

It removes the synchronous pipeline wait by returning a null pipeline and
skipping the affected draw until compilation finishes. One run logged more
than 3,000 deferred draws for a pending pipeline, so missing geometry/materials
can persist. A later run logged 32 deferrals and no 500 ms producer stall, but
frame rate still fell well below target. This is not release-safe behavior.

### Lab profiler launch hook

Exclude `ios/bridge/theft4_boot.cpp` and `ios/bridge/theft4_startup.cpp` from
`cde6fe2b`. They add `THEFT4_LAB_NATIVE_PROFILE`, which is useful only for a
future Lab diagnostic run.

## Device evidence and known regression

The installed async-pipeline Lab was built from `d7ffffa5`. During the monitored
route, 21 complete 300-frame windows measured:

- minimum: 22.80 FPS;
- median: 28.43 FPS;
- maximum: 30.02 FPS;
- final eight windows: 23.90, 23.37, 26.84, 28.43, 23.34, 25.57, 22.80 and
  23.96 FPS.

That session logged 32 pipeline deferrals, zero producer stalls at the existing
500 ms threshold, zero publish failures and zero fatal errors. The later
sustained 22-26 FPS periods did not coincide with new pipeline deferrals. Audio
summaries remained free of underruns, rebuffers, drops, clipping and nonfinite
samples. Evidence:
`out/m5-lab/validation/async-pipelines/live-fps-4.log`.

The result does not meet the locked-30 target. It proves that pipeline waiting
was one severe new-area hitch but not the only frame-pacing limit. Do not claim
locked 30 FPS for 0.1.3(a).

An attempted 15-second Time Profiler capture disconnected after 1.831 seconds
and contained no Theft4 process samples. It is invalid for attribution. The
trace is `out/m5-lab/validation/async-pipelines/cpu-slow-scene.trace`.

The final `cde6fe2b` artifact was built and signed but not installed. After the
profiler disconnect, no Theft4 process was running. No command in this final
pass installed, launched, modified or removed the ordinary
`com.theft4.bringup` app.

## Release acceptance after promotion

For 0.1.3(a), build and install the ordinary Theft4 identity with only the two
shared CPU patches above, then run:

1. launch-to-scene smoke and save/load;
2. the same fast-driving city route for at least five minutes;
3. present-window FPS plus frame-time tail collection;
4. checks for visual/resource errors and audio underruns.

Acceptance remains a paced 30 FPS with no new stalls or visual regressions. The
current evidence supports the shared CPU reductions, but it does not establish
that they alone produce a locked frame rate.
