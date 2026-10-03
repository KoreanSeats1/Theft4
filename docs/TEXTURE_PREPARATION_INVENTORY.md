# Theft4 ASTC device experiment

This branch starts from the committed build 93 renderer and builds a separate
`Theft4 ASTC` app (`com.lukebrosious.theft4.astc`).
It has its own iOS container, so the ordinary app's game files, settings,
saves, and caches are separate. It is an experiment for device validation, not
an ordinary release build. Its ASTC code is compiled only with
`THEFT4_ASTC_EXPERIMENT=ON` and the native GTA IV renderer enabled.

## One-time preparation and observed list

After the user supplies and verifies the extracted game and TU8 inside this
app's `Documents/game`, the ASTC setup reads loose Xbox texture dictionaries,
drawables and fragments (`.xtd`, `.xdr`, `.xdd`, `.xft`) and RPF2/IMG3 resource
entries. It decrypts archive tables when needed, decompresses bounded RSC5/RSC85
resources, and validates embedded texture objects. It uses the renderer's
tiling, endian, mip layout and content-hash routines to generate the same
cache keys as gameplay. Game files are read only.

The local TU8 scan found 99,054 source texture records, representing 49,867
unique BC1/BC2/BC3 textures and 2,567,750,080 bytes (2.39 GiB) of ASTC 4x4
payload. It matched 2,452 of 2,460 previously observed runtime keys. The eight
unmatched keys retain the runtime path. The scan also reports 35 unsupported
RPF warnings from audio archives; these are retained in the manifest rather
than treated as evidence of complete coverage. 3D textures, arrays, cubes and
GPU-produced textures are outside the static 2D preparation pass.

The cyan
**ASTC Texture Compatibility** switch in System controls conversion for the
next Play session. It starts On when the device reports no direct BC support,
and Off when direct BC support is present. With the switch On, first launch
shows an explanation, then indexes and prepares static textures before Play.
Progress includes counts, reused/new conversions and an estimated remaining
time. The UI adds a cyan progress bar with a percentage for each
of the two steps (checking files, preparing textures), an estimate for the
current step, and explicit one-time setup notes. Longer notes scroll on phones.
Gameplay is not started during preparation. The app stays awake while
active and pauses preparation when backgrounded. Pause/resume retains
completed atomic cache files; a killed process also retains those files.
Resuming revisits the manifest and reuses the saved textures.

The service checks remaining disk space plus a 1 GiB reserve and persists a
cache budget sized for the complete manifest plus 64 MiB headroom. The default
runtime-only budget remains 1 GiB; preparation can raise it up to 16 GiB. Cache
entries are not evicted. Completion requires every manifest key to have been
saved. Subsequent launches compare source filenames, sizes and mtimes and
check saved cache filenames, without re-encoding the game. A changed source
set rescans; missing cache files require preparation again. Matching cache
keys are reused across both cases. The System settings page offers
**Delete Prepared Texture Cache**, with Cancel and destructive confirmation.
Deletion is blocked during preparation/gameplay and removes only `astc-v1`
and its completion/index markers. The source manifest, diagnostics, original
game and saves remain; preparation is required again before ASTC Play.

The notes give iPhone 15 Pro (A17 Pro), iPhone 16 (A18) and M3/M4/M5 iPads as
examples of devices that skip the requirement. Apple's
[GPU feature tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf)
list BC support on all Apple9 GPUs and some Apple7/Apple8 iPad GPUs. The app
uses `MTLDevice.supportsBCTextureCompression`, rather than inferring support
from the device name or age. A forced comparison toggle remains available in
the experimental app.

Files live under `Library/Application Support/Theft4/texture-preparation`:
`archive-texture-manifest.json`, `preparation-state.json`,
`prepared-cache-index.json`, `cache-budget.txt` and `astc-v1/*.bin`. Corrupt
payloads are checksum-checked and rebuilt when read. With the switch Off,
unsupported BC textures use RGBA8 for the control comparison. With it On,
textures outside the manifest may still encode at first encounter; encoder or
format failures use RGBA8. 3D BC textures use RGBA8 in this experiment.

The append-only `texture-preparation/observed-textures.jsonl` records the
unique content key, source format, dimensions, mip count, input/output bytes,
cache hit, preparation time, and result. It contains metadata, not texture
pixels. The observed log, source inventory and preparation summary are included
in the app's diagnostic export. A single
drive cannot find every texture; different locations, saves, mods, and title
updates can produce different observed lists. Base-game source files should be
similar for an identical installation, but each app installation prepares its
own derived cache.

Turning the switch On forces preparation even if the GPU supports BC, for
comparison on modern hardware. Production should retain actual capability
gating. Unprepared first encounters can pause rendering while compression
runs. A successful source scan is not proof that every runtime texture is
covered or that other frame-time stalls have been fixed. Device acceptance
and first-run duration still need measurement before merging.

## Older-device rendering path

The ASTC test app checks Metal's Apple GPU family and BC support, plus Vulkan's
BC3 sampled-image support and fragment sampler limit. On Apple GPU families
before Apple 10, when BC3 is unsupported and the GPU allows at most 16 samplers,
the native renderer compacts each draw's active sampler bindings to fit that
limit and presents its final frontbuffer with a direct Vulkan blit. On the
11-inch iPad Pro (2nd generation, A12Z), the normal fullscreen present shader
produced a black screen even though the frontbuffer contained pixels; the
direct path displayed the opening 3D cutscene at the 30 FPS cap. This
capability check applies to other older GPUs with the same limits without
changing the newer GPU path. It is independent of the cyan conversion switch,
which controls texture format preparation.

For BC-incompatible iPhones and iPads in the sub-7 GiB physical-memory tier,
the ASTC app also defaults to one native frame in flight and disables the
speculative pipeline, texture-content cache, and draw-reuse paths. This stable
control combination avoided an A12Z GPU fault seen with build 93 defaults;
the individual fault trigger has not yet been isolated. Launch overrides remain
available for comparisons. These are test-app defaults, not changes to the
ordinary Theft4 build or its performance branch. The device's existing
limited-memory graphics preset keeps anti-aliasing off on the tested iPad.

The ASTC experiment now defaults **Runtime Wait Improvements** Off, matching
the user's M5 comparison after their reported regression. This overrides the
inherited build 93 registration default only in the ASTC app, preserves an
explicit saved choice, and requires a full restart when changed. It is
independent of ASTC conversion and does not invalidate the prepared cache.
Whether it contributes to the A12Z's reported 8 FPS remains unmeasured.

After successful completion verification, the ASTC launcher now primes cache
storage accounting once per process on a utility queue. Previously the first
new runtime texture could enumerate about 50,000 prepared files while saving
its result on the renderer thread. The latest capture included three keys
outside the static manifest; its first encode-and-save span was 4,761 ms,
versus 7 and 18 ms for the next two. This is consistent with cold accounting,
but the old observation log does not separate encoding from cache-write time.
Priming reads file metadata only, preserves the cache budget, and performs no
texture conversion. A racing cache write or confirmed deletion uses the same
mutex. This work is reached only by the ASTC preparation flow, so ordinary
BC-capable launches do not run it. No performance improvement is claimed until
another runtime cache miss is observed on device.

The user put integration with the main build on hold on 2026-10-02. Changes
remain on the isolated ASTC branch; the main checkout and its ongoing renderer
work must not be modified as part of this follow-up.

## Build and test

Configure a Release iOS build from the isolated branch with the same headless
runtime flags used by the ordinary app, plus:

```
-DTHEFT4_ENABLE_GTA4_NATIVE_BACKEND=ON
-DTHEFT4_ASTC_EXPERIMENT=ON
-DTHEFT4_LAB_BUILD=ON
-DTHEFT4_BUNDLE_IDENTIFIER=com.lukebrosious.theft4.astc
-DTHEFT4_DISPLAY_NAME=Theft4 ASTC
-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
```

Use the user's signing team for an installable device build. Install only the
ASTC bundle ID. Copy the user's legally supplied game and a disposable test
save into this app's separate container. Complete preparation, verify
pause/relaunch reuse and the completion marker, then drive a repeatable route,
export diagnostics, relaunch and drive the same route. Compare missing
textures, visible quality, conversion stalls, cache-hit count, disk use and
frame pacing. Visit a new area to exercise any remaining misses. The test-only
`--theft4-prepare-textures` launch argument starts preparation without Play.

`tests/ios/theft4_astc_texture_test.cpp` checks BC1/BC2/BC3 decoding, ASTC
output, cache reuse, corruption recovery, persisted budgets, cache deletion
recovery and observed-list creation with synthetic blocks.
`tests/ios/theft4_texture_manifest_test.cpp` validates resource/archive bounds,
synthetic LZX decoding, exact cache identities, deduplication, cancellation,
source changes and manifest path containment. The host-only scan/test tool is
documented in `tools/texture-preparation/README.md`.
On 2026-10-02, both host test binaries passed, all 49,867 unique textures
passed a second manifest-key reconstruction check, the iOS Release compiler
check passed, and the updated test app installed on the A12Z iPad. Its
on-device indexing screen was visually checked with live progress and a Pause
button. On-device preparation finished at 19:45:04 local: all 49,867 keys
were persisted, with 47,019 newly encoded and 2,848 reused. After a user pause
and resume during indexing, the completed run took 38 minutes 15 seconds:
6 minutes 59 seconds indexing and 31 minutes 16 seconds preparation. Including
the earlier interrupted indexing attempt, elapsed time was 41 minutes 23 seconds.
ASTC payload is 2.39 GiB; cache metadata and filesystem overhead add to that.
The subsequent UI/cache-control revision also passed both host tests and the
iOS Release compiler check. It is saved as an installable app under
`out/device-apps/astc-preparation-ui-20261002/Theft4.app`; it was installed after
completion, as requested. Normal launch displayed the ready launcher with
ASTC On and no preparation overlay or new sweep. A copied real cache entry
matched the host's canonical source hash, mip layout and payload checksum and
was reused without encoding. The normal-launch console did not capture the
early cache verification message, so no precise launch-check time is claimed.
Completion JSON and a readiness screenshot are retained under
`out/texture-reports/a12z-2026-10-02/`. Gameplay comparison and on-device cache
confirmation-dialog inspection remain pending; the completed cache was retained.
The encoder is Arm's `astc-encoder` 5.3.0 (Apache-2.0),
vendored under `thirdparty/astc-encoder` with its license bundled in the app.

## References for import-time preparation

The [Xbox 360 GTA IV setup guide](https://github.com/luisxl15/GTA-IV-RECOMP-XBOX-360/blob/main/docs/SETUP.md)
documents the extracted `game/`, `xbox360/`, `common/`, and title-update layout.
The [RAGE Console Texture Editor source](https://github.com/indirivacua/RAGE-Console-Texture-Editor)
includes `GTAIV.TextureResource.Xbox360.pas` for Xbox 360 texture dictionaries.
The [GTA IV Modding Toolkit IMG3 parser](https://github.com/Heidric/GTAIVModdingToolkit/blob/main/core/img3.py)
documents IMG entry offsets, sectors, padding and encrypted-table handling.
These are format references; the manifest generated from each installed copy
is the preparation input. It contains paths and metadata, not game pixels or
an embedded AES key.
