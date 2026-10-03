# Direct Metal renderer prototype

Branch: `codex/direct-metal-96`, based on the preserved build 95 comparison
(`38e44249`). The game app continues to use its existing renderer. This prototype
is linked into a separate **Theft4 Metal Lab**, with no Vulkan or MoltenVK runtime.

## Working components

`ios/bridge/theft4_native_metal.h/.mm` encodes immutable game-resource views into
Metal render passes and draws. It supports pipeline/vertex declarations, shader
constants, textures and samplers, indexed and non-indexed draws, depth/blending,
MSAA resolve, multiple passes, GPU completion receipts, bounded frame leases,
and CAMetalLayer drawable presentation. Aborted and completed frames reclaim
their slots. Normal draws do not perform CPU readback.

`metal_shader_export` translates the exact embedded stock SPIR-V corpus offline.
It retains the native color-output epilogue, alpha testing and Xenos sample-mask
generation. Vertex Y conversion follows Metal's viewport convention. The three
guest constant banks become ordinary Metal buffer slots 0/1/2; pointer arithmetic
is derived from these parameters inside the shader. The host does not provide
Vulkan buffer-device addresses.

Descriptor-array accesses are resolved against the shipped fetch ABI, then
replaced with compact per-shader Metal texture/sampler indices. Non-descriptor
shader operations stay identical. The exported manifest records game fetch
slots, Metal indices, stage inputs and shader variants. Shaders with unknown
fetches, incompatible layouts or excessive bindings reject. Runtime argument
buffers and global Vulkan descriptor heaps are not required.

The stock corpus contains 1,356 shaders and 706 additional late variants:
**2,062 iOS Metal libraries compile successfully**, using Metal 2.4 with an iOS 16
shader deployment target. Actual maximum sampler count is eight per stage.
This is compilation coverage; every game pipeline/pass combination still needs
validation. The test app itself uses the project's iOS 26 deployment target.

## Validation

The Mac M1 Max passes 16 checks: vertex color, texture tint/orientation, 16/32-bit
indexed draws and offsets, depth occlusion, blending, late alpha discard, output
scaling/clamping order, MSAA resolve, Xenos alpha coverage, ASTC sampling,
render-target sampling, VS/PS constant banks, packet admission/lifetimes and the
device BC capability gate. Every pixel is compared to an independent CPU oracle
with a one-byte UNORM tolerance. Old Vulkan descriptor fields are deliberately
poisoned. Real single-slot and sparse-slot 0/15 shader tests verify that only
descriptor operands change and malformed/unreflected inputs reject.

Metal Lab additionally renders a game-shader preview directly into its drawable,
without a CPU image upload. Reports and raw pixel images are saved in its own
Files container. M5 and A12Z installs are staged; hardware runs require the
devices unlocked on Home. The existing game apps, saves and prepared ASTC cache
are separate. No gameplay FPS improvement has been established for this backend.

## Reproduce

Use Xcode's developer directory and the installed CMake Python package:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
python3 -m cmake -S tools/direct-metal -B out/direct-metal/build -DCMAKE_BUILD_TYPE=Release
python3 -m cmake --build out/direct-metal/build --parallel 6
out/direct-metal/build/metal_descriptor_contract_test
out/direct-metal/build/metal_shader_export out/direct-metal/corpus
python3 tools/direct-metal/compile_shaders.py out/direct-metal/corpus out/direct-metal/ios --platform ios
```

The shader compiler script discovers Xcode's installed Metal component, or
accepts `--compiler-directory`. For the small validation app, export with filter
`gta_im`, compile those libraries, and configure `tools/direct-metal/device` with
the iOS toolchain and `METAL_LIBRARIES` pointing to that directory. Mac GPU checks
use the same exporter and `--platform macos` libraries, followed by `metal_probe
libraries output`. The host sandbox may require GPU access for that command.

## Next integration boundary

1. Extract the existing renderer's immutable frame/resource state into a backend
   independent frame plan. Keep game ordering, shader/pipeline selection rules,
   generation identities and owner-page retirement intact.
2. Add Metal realizations for the existing converted texture/buffer payloads,
   including the prepared ASTC cache, mip/cube layouts, render-target aliases,
   depth formats and CPU-visible resolve/readback operations. Reuse uploads by
   resource generation and share one fence-owned constant arena per frame.
3. Map complete fixed-function state and all game vertex formats into cached
   Metal pipelines. Complete post-processing/compute shader migration and active
   override policy handling; these are outside the stock shader audit.
4. Preserve pass dependencies, loads/stores, resolves, HDR/output policy and GPU
   completion ownership. Replace Vulkan recording/submission after a complete
   captured game frame can be reproduced accurately through Metal.
5. Compare matching real scenes on M5 and A12Z, measuring CPU assembly/encoding,
   GPU time, memory and image correctness. Keep build 95 available for comparison.

The renderer currently admits a conservative set of vertex formats and per-vertex
streams, uses complete constant-bank bounds, and trusts the adapter's validated
index maximum. These boundaries must be expanded before enabling it for all game
frames. Removing the API translator does not remove the game's CPU work or GPU
shading workload; the gameplay gain needs measurement.
