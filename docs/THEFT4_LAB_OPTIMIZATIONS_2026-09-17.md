**M5 Lab optimization experiment — 17 September 2026**

The user authorized implementation before further driving measurements. These
are candidates to evaluate, not measured performance improvements. Installation
is restricted to Theft4 Lab (`com.theft4.m5lab`). The original Theft4 identity is
`com.theft4.bringup`; main remains a separate working checkout.

**Implemented changes**

| Change | Commit | Intended effect |
|---|---|---|
| Sampler-independent CPU texture identity | `d8c75a8a` | Reuse decoded texture generations when only sampling fields change, avoiding unnecessary decode, hash and image upload work |
| Draw-state reference and conditional font prefix hash | `b4563d5d` | Avoid a string copy on every captured draw/clear and skip font-only prefix hashing for ordinary textures |

The content key ignores 43 sampling bits and retains the other 149 fetch bits.
Mip minimum/maximum, source and mip addresses, format, layout, dimensions,
swizzle and conversion fields remain significant. Dirty textures still take
the content-validation path; payload equality, release handling and virtual
resource checks remain in place. Draw commands retain their current, unmodified
fetch for sampler binding. CPU present-source matching uses the same identity;
GPU-produced fallback matching retains its prior rules.

`gta4_native_texture_content_cache` defaults to false in the renderer. Lab alone
enables it at startup. A fresh Lab launch with
`THEFT4_LAB_TEXTURE_CONTENT_CACHE=0` restores strict fetch comparison in the same
executable. The two small overhead reductions remain enabled in that comparison.
Use the archived pre-optimization Lab build for a complete pre-change control.
Do not update the original app to run either comparison.

Example control launch after Lab has been closed normally:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcrun devicectl device process launch \
  --device 00008142-001C28A022FB401C \
  --environment-variables '{"THEFT4_LAB_TEXTURE_CONTENT_CACHE":"0"}' \
  com.theft4.m5lab --theft4-start-game
```

Use value `1` for the experiment or launch Lab normally. Each run records the
selected mode in `Library/Application Support/Theft4/startup/runtime.log`.
A changed launch environment takes effect only in a new process. Preserve Lab
saves before deliberate A/B reinstalls or lifecycle tests.

The 30 Hz limiter, two native frame slots, 4x anisotropy and FSR defaults were
not changed. This experiment does not add a third in-flight frame, alter Metal
completion ownership, remove correctness checks or reduce rendering quality.

**Verification**

- 61 host cases / 4,776 assertions passed with AddressSanitizer and UBSan.
  These include the new production-key tests, all 192 single-bit mutations,
  named Xenos field checks, sampler preservation, and existing sampler, texture
  policy, frame scheduling, resource-content and frame-limiter tests.
- 12 Lab isolation checks passed.
- Release build and signing checks passed; installation and native startup
  succeeded on the M5 iPad. The startup log confirms the cache experiment enabled.
- The original Theft4 installation record is unchanged. Lab retained all 1,689
  game files (7,072,969,417 bytes), checked by relative path and size.
- The user requested stopping the test. A termination signal was sent to the
  Lab process launched by this task (PID 1142); no further test was run.

**Next evaluation and integration**

Drive the same route from the same save with matching settings and warmed caches.
Compare long-frame/stutter frequency and texture streaming. Check HUD/fonts,
reflections, shadows, audio, save/load and foreground/background transitions.
If visuals regress, use the strict-cache launch mode to isolate the new cache
identity before changing any other setting. Retain logs and the artifact receipt
for each run. No FPS increase or five-minute gameplay acceptance is claimed.

Keep accepted commits separate. Combine them on an integration branch based on
the current Theft4 main, revalidate the combined result in Lab, and merge only
after gameplay acceptance. Do not carry Lab's bundle identity into ordinary
Theft4. Its current release version and independently advanced main history must
be preserved during integration.

**First device run**

The optimized Lab was relaunched as a fresh process (`PID 1148`) after a
different test had overlapped the preceding launch. The startup log confirms
the Lab cache experiment was enabled, the Apple M5 GPU and native renderer were
selected, and the recompiled game entered presentation mode.

The user reported that entering the scene took longer and initially had heavy
stuttering, then smoothed out. Once warm, it felt the same as the working build
or slightly better. The log supports the warm-up boundary but does not establish
the cause or a performance improvement:

- 14 producer-stall warnings occurred between 16:30:06 and 16:32:02. Most were
  about 500 ms; the same blocked episode was also reported at roughly 4 and 8
  seconds.
- The large initial cluster coincided with creation and alias registration for
  scene, reflection and small render surfaces. This is correlation, not proof
  that surface work caused every stall.
- No further producer-stall warning appeared between 16:32:02 and the final log
  sample at 16:33:02.
- Audio counters reported zero underrun frames, rebuffers, dropped frames,
  clipping and non-finite samples. No fatal error, exception or crash was found.

Disposition: **keep installed for controlled comparison; do not merge yet**.
The next discriminating test is the same cold launch and route with
`THEFT4_LAB_TEXTURE_CONTENT_CACHE=0`, followed by the enabled mode with matching
cache warmth. That isolates the texture-content key from the two smaller CPU
overhead changes and from ordinary first-run pipeline/resource warm-up.

**Previous installed artifact**

Source: `b4563d5d5e5b6ec327d3360f14a30ef22f43b6c3`.

Executable SHA-256: `258e65ce7719b1ec25b69ba75e3d8930d34493413c41c492a1d81bb486f89ca9`.

Receipt: `out/m5-lab/artifacts/b4563d5d5e5b-20260917T202607Z/receipt.json`.

Device evidence: `out/m5-lab/validation/optimizations/`.

**Stall-attribution Lab build**

Commit `7dfff41b` adds a Lab-only, lightweight render-worker phase marker to
each existing 500 ms `producer-stall` warning. It reports the active command,
command sequence, phase and elapsed phase time. The phase distinguishes command
dispatch, state snapshot, pipeline prewarm, publish setup, frame-slot wait,
housekeeping, upload-capacity work, command setup, texture preparation, frame
recording, finalization and queue submission. It does not enable the detailed
CPU/GPU profiler and does not change queue limits, frame scheduling or rendering
behavior. Ordinary Theft4 keeps the diagnostic disabled by default.

The signed Release build passed compilation, release verification, bundle and
entitlement checks, and code-signature verification. It was installed as
`com.theft4.m5lab` on the M5 iPad. Device readback after installation showed:

- Theft4 Lab version 0.1.2 build 4 at its new Lab installation URL.
- Original Theft4 version 0.1.2 build 4 at its unchanged installation URL.
- All 1,689 Lab game files and 7,072,969,417 bytes match the prior inventory by
  relative path and size.

Source: `7dfff41b5a2c2eabbfd17505895893c2f5728924`.

Executable SHA-256: `d998ba9810bbd234646c7376c99ac6b0d198848c436410f1a4ce05a9db43b7e1`.

Receipt: `out/m5-lab/artifacts/7dfff41b5a2c-20260917T205215Z/receipt.json`.

Device evidence: `out/m5-lab/validation/stall-attribution/`.

The next run should begin from a fresh Lab launch and repeat the same route. If
a stall recurs, the warning will identify the render-worker phase to optimize.
Gameplay acceptance remains pending until that run.
