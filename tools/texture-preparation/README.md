# Static texture scan and tests

This macOS Arm64 tool runs the same bounded archive/resource parser and texture
canonicalization used by the isolated iOS ASTC preparation service. It reads
game files without modifying them. It writes only the requested manifest or
decoded diagnostic output. No gameplay or frame capture is required.

Build and validate from the repository root:

```sh
python3 -m cmake -S tools/texture-preparation -B out/build/texture-tools -DCMAKE_BUILD_TYPE=Release
python3 -m cmake --build out/build/texture-tools --parallel 4
out/build/texture-tools/astc_texture_test
out/build/texture-tools/texture_manifest_test
```

Generate a manifest from a legally supplied extracted Xbox game and its key:

```sh
out/build/texture-tools/texture_manifest /path/to/game /path/to/game/aes_key.bin /tmp/archive-texture-manifest.json
```

The manifest records source containers, archive entries and byte extents,
texture-object offsets, names, formats, mip counts, sizes and content cache
keys. Duplicate source locations remain visible; output totals count unique
keys. Unsupported or malformed resources produce warnings, while cancellation
and installation limits stop the scan. Static 2D BC1/BC2/BC3 textures are
eligible; cubes, arrays, 3D and GPU-generated textures retain runtime handling.

The decoder diagnostic can extract a loose resource for local inspection:

```sh
out/build/texture-tools/texture_manifest --resource /path/to/file.xtd /tmp/resource.raw
```

Revisit every unique texture and verify its canonical payload still matches
the manifest key, without encoding or writing cache files:

```sh
out/build/texture-tools/texture_manifest --verify-manifest /path/to/game /tmp/archive-texture-manifest.json
```

Do not commit game data, decrypted resources, keys or generated manifests. The
iOS app independently generates its manifest and ASTC cache in its container.
