# Theft4 ASTC device experiment

This branch builds a separate `Theft4 ASTC` app (`com.lukebrosious.theft4.astc`).
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

Actual BC textures are discovered only when gameplay loads them. The native
renderer already untile/swaps their blocks, hashes the payload, and enumerates
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

The test app forces ASTC conversion even if its GPU supports BC, so it can be
compared on a modern iPad. A production variant should gate this on the actual
GPU format capabilities and decide whether on-device encoding time is
acceptable. The first encounter may pause rendering while compression runs;
subsequent encounters should be faster. This must be measured on device before
merging or promising a first-run duration.

## Build and test

Configure a Release iOS build from the isolated branch with the same headless
runtime flags used by the ordinary app, plus:

```
-DTHEFT4_ENABLE_GTA4_NATIVE_BACKEND=ON
-DTHEFT4_ASTC_EXPERIMENT=ON
-DTHEFT4_LAB_BUILD=OFF
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
