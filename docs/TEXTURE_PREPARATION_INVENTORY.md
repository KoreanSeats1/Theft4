# Theft4 ASTC device experiment

This branch starts from the committed build 93 renderer and builds a separate
`Theft4 ASTC` app (`com.lukebrosious.theft4.astc`).
It has its own iOS container, so the ordinary app's game files, settings,
saves, and caches are separate. It is an experiment for device validation, not
an ordinary release build. Its ASTC code is compiled only with
`THEFT4_ASTC_EXPERIMENT=ON` and the native GTA IV renderer enabled.

## First launch and observed list

After the user supplies and verifies the extracted game and TU8 inside this
app's `Documents/game`, setup explains that the initial source scan inventories
filenames only. It scans loose `.xtd` and `.wtd` dictionaries, `.rpf` and `.img`
archives, and drawable/fragment resources that may contain or reference
textures. The bounded, symlink-safe source list is saved to
`Library/Application Support/Theft4/texture-preparation/source-inventory.json`.
No game file is modified by the scan.

Actual BC textures are discovered only when gameplay loads them. The cyan
**ASTC Texture Compatibility** switch in System controls conversion for the
next Play session. It starts On when the device reports no direct BC support,
and Off when direct BC support is present. With the switch Off, encountered BC
textures are still logged, with no ASTC encoding; unsupported BC textures use
an RGBA8 control path instead. With the switch On, the native renderer already untile/swaps
their blocks, hashes the payload, and enumerates
mips and layers. On the ASTC app's first encounter, BC1, BC2, or BC3 textures
are decoded and encoded to ASTC 4x4. A content-keyed file under
`texture-preparation/astc-v1` is reused on later encounters and launches.
The cache is limited to 1 GiB and is written atomically. Corrupt cache files
are rejected and rebuilt. If ASTC encoding or format support fails, RGBA8 is
used for that texture. 3D BC textures use RGBA8 in this experiment. GPU-produced
textures stay on their existing path.

The append-only `texture-preparation/observed-textures.jsonl` records the
unique content key, source format, dimensions, mip count, input/output bytes,
cache hit, preparation time, and result. It contains metadata, not texture
pixels. Both JSON files are included in the app's diagnostic export. A single
drive cannot find every texture; different locations, saves, mods, and title
updates can produce different observed lists. Base-game source files should be
similar for an identical installation, but each app installation prepares its
own derived cache.

Turning the switch On forces ASTC conversion even if the GPU supports BC, so
it can be compared on a modern iPad. A production variant should gate this on
actual GPU format capabilities and decide whether on-device encoding time is
acceptable. The first encounter may pause rendering while compression runs;
subsequent encounters should be faster. This must be measured on device before
merging or promising a first-run duration.

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
save into this app's separate container. On the first run, note source-scan
counts, drive a repeatable route, export diagnostics, relaunch, and drive the
same route. Compare missing textures, visible quality, conversion stalls,
cache-hit count, disk use, and frame pacing with the ordinary app. Visit a new
area to exercise cache misses. Do not interpret a source-container scan or a
successful build as device gameplay acceptance.

`tests/ios/theft4_astc_texture_test.cpp` checks BC1/BC2/BC3 decoding, ASTC
output, cache reuse, corruption recovery, and observed-list creation with
synthetic blocks. The encoder is Arm's `astc-encoder` 5.3.0 (Apache-2.0),
vendored under `thirdparty/astc-encoder` with its license bundled in the app.

## References for import-time preparation

The [Xbox 360 GTA IV setup guide](https://github.com/luisxl15/GTA-IV-RECOMP-XBOX-360/blob/main/docs/SETUP.md)
documents the extracted `game/`, `xbox360/`, `common/`, and title-update layout.
The [RAGE Console Texture Editor source](https://github.com/indirivacua/RAGE-Console-Texture-Editor)
includes `GTAIV.TextureResource.Xbox360.pas` for Xbox 360 texture dictionaries.
This repository also has a prototype `tools/xtd_tools/xtd_cli.c` for listing
textures from a loose XTD. These are format and parser references, not a
complete texture-to-archive manifest for a given game installation. The local
source inventory is authoritative for the installed copy; pre-conversion still
needs to map observed content keys to archive entries or load the assets through
the game's existing asset path before Play.
