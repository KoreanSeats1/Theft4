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

**Installed artifact**

Source: `b4563d5d5e5b6ec327d3360f14a30ef22f43b6c3`.

Executable SHA-256: `258e65ce7719b1ec25b69ba75e3d8930d34493413c41c492a1d81bb486f89ca9`.

Receipt: `out/m5-lab/artifacts/b4563d5d5e5b-20260917T202607Z/receipt.json`.

Device evidence: `out/m5-lab/validation/optimizations/`.
