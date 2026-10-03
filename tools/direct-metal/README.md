# Direct Metal renderer prototype

Branch: `codex/direct-metal-96`, based on the preserved build 95 comparison
(`38e44249`). The game app continues to use its existing renderer. This prototype
is linked into a separate **Theft4 Metal Lab**, with no Vulkan or MoltenVK runtime.

## Working components

`ios/bridge/theft4_native_metal.h/.mm` encodes immutable game-resource views into
Metal render passes and draws. It supports pipeline/vertex declarations, shader
constants, textures and samplers, indexed and non-indexed draws, depth/blending,
MSAA resolve, multiple passes, GPU completion receipts, bounded frame leases,
depth-only pipelines, all 17 game vertex streams, per-instance inputs, and
CAMetalLayer drawable presentation. Aborted and completed frames reclaim
their slots. Normal draws do not perform CPU readback.

`metal_shader_export` translates the exact embedded stock SPIR-V corpus offline.
It retains the native color-output epilogue, alpha testing and Xenos sample-mask
generation. Vertex Y conversion follows Metal's viewport convention. Every stock vertex shader
also has a separate negative-one-to-one depth variant; the MSL exporter maps
`z` to `(z + w) / 2` while preserving homogeneous `w`. The effective draw state
selects this variant, leaving ordinary zero-to-one shaders unchanged. The three
guest constant banks become ordinary Metal buffer slots 0/1/2; pointer arithmetic
is derived from these parameters inside the shader. The host does not provide
Vulkan buffer-device addresses.

Descriptor-array accesses are resolved against the shipped fetch ABI, then
replaced with compact per-shader Metal texture/sampler indices. Non-descriptor
shader operations stay identical. The exported manifest records game fetch
slots, Metal indices, stage inputs and shader variants. Shaders with unknown
fetches, incompatible layouts or excessive bindings reject. Runtime argument
buffers and global Vulkan descriptor heaps are not required.

`theft4_metal_shader_catalog` and `theft4_metal_shader_store` provide the game's
hash/stage/variant lookup and fetch-slot interface. The complete manifest is
checked against the embedded stock cache, including masks and shader stages.
Library reads and function creation happen on cache misses. Specialization
values contain only bits used by the shader. Sparse fetch slots retain their
ordering, and malformed metadata or incomplete binding transactions reject.

`theft4_metal_resources` realizes CPU-converted, immutable source generations.
Buffers and sampled textures reuse their Metal objects while the source owner,
generation and conversion identity agree. Texture shape, swizzle and every
mip/slice/pitch range are checked. Retirement uses weak CPU owners; submitted
Metal command buffers keep their GPU resources alive. The cache does not own
mutable render targets or resolve aliases. These initial immutable uploads use
shared storage; private staging and upload batching belong in the frame adapter.

The stock corpus contains 1,356 shaders and 706 additional late variants:
plus 650 vertex depth-convention variants: **2,712 iOS Metal libraries compile successfully**, using Metal 2.4 with an iOS 16
shader deployment target. Actual maximum sampler count is eight per stage.
This is compilation coverage; every game pipeline/pass combination still needs
validation. The test app itself uses the project's iOS 26 deployment target.

## Validation

The API-free `theft4_render_plan` contract carries converted immutable buffers,
sampled images, shaders, and effective pipeline/draw state. The Metal adapter
realizes and caches those packets. Its diagnostic CBOR codec preserves shared
resource ownership and rejects malformed references, integer overflow, invalid
index/instance ranges, and attachment-role mismatches. Unsupported override
shaders, partial sample masks, fans, and missing GPU-produced inputs reject
explicitly rather than approximating their effects.

An optional `THEFT4_NATIVE_METAL_CAPTURE` lab build records the exact bytes after
frontend vertex/index/texture conversion. It is OFF by default; capture also
requires `THEFT4_METAL_CAPTURE=1` at launch. Its worker writes up to 24 scene-indexed
and 8 other pipeline families into `Documents/MetalDrawCaptures/run-*`. Resident
copies are bounded at 64 MiB, output resources at 128 MiB, and queued files at
four. The game remains on its comparison renderer. Captures contain game asset
data and must stay private, outside Git and published reports.

Metal Lab build 5 includes the full stock shader catalog and an isolated draw
replayer. Transfer a selected private capture run into its `Documents/GameDrawInputs`
and reopen the Lab. Each admitted draw renders on cleared matching attachments,
repeats with cached GPU resources, compares repeat pixels, and saves a PNG/report.
The visible preview samples that GPU target directly into the drawable. This
does not yet reproduce prior pass contents, GPU-generated aliases, or a complete
game frame. Its measured draw GPU time is not a gameplay FPS benchmark.

The expanded host checks include constant-color blending and RGBA write masks
on BGRA targets, immutable draw reuse, retirement before submission, private
serialization/replay, and GPU target sampling. The replay self-test is clearly
labeled as synthetic validation geometry; real game capture requires user Play.

The Mac M1 Max passes 20 checks: vertex color, texture tint/orientation, 16/32-bit
indexed draws and offsets, depth occlusion, blending, late alpha discard, output
scaling/clamping order, MSAA resolve, Xenos alpha coverage, ASTC sampling,
render-target sampling, VS/PS constant banks, packet admission/lifetimes and the
device BC capability gate. The additional checks cover runtime shader caching,
immutable resource reuse, depth-only passes and per-instance stream 16. Rendering
pixels are compared to independent CPU oracles with at most a one-byte UNORM
tolerance. Cached RGBA/ASTC mip sampling and 19 cube/array/volume planes are
verified through the GPU. Old Vulkan descriptor fields are deliberately
poisoned. Real single-slot and sparse-slot 0/15 shader tests verify that only
descriptor operands change and malformed/unreflected inputs reject.

Metal Lab additionally renders a game-shader preview directly into its drawable,
without a CPU image upload. Reports and raw pixel images are saved in its own
Files container. Build 4 uses a `UIWindowSceneDelegate` and scene manifest;
the window is created for its scene, checks begin after foreground activation,
and drawable presentation waits for an active scene. Build 3's legacy window
setup triggered `UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption`
on iPadOS 27 before any graphics checks ran. Apple requires the
[scene-based lifecycle](https://developer.apple.com/documentation/uikit/transitioning-to-the-uikit-scene-based-life-cycle)
for apps built with the latest SDK on that OS.
Hardware checks require a reachable, unlocked device. The existing game apps,
saves and prepared ASTC cache are separate. No gameplay FPS improvement has been
established for this backend.

## Reproduce

Use Xcode's developer directory and the installed CMake Python package:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
python3 -m cmake -S tools/direct-metal -B out/direct-metal/build -DCMAKE_BUILD_TYPE=Release
python3 -m cmake --build out/direct-metal/build --parallel 6
out/direct-metal/build/metal_descriptor_contract_test
out/direct-metal/build/metal_shader_export out/direct-metal/corpus
out/direct-metal/build/metal_shader_catalog_test out/direct-metal/corpus/manifest.tsv
python3 tools/direct-metal/compile_shaders.py out/direct-metal/corpus out/direct-metal/ios --platform ios
```

The shader compiler script discovers Xcode's installed Metal component, or
accepts `--compiler-directory`. For the small validation app, export with filter
`gta_im`, compile those libraries, and configure `tools/direct-metal/device` with
the iOS toolchain and `METAL_LIBRARIES` pointing to that directory. Mac GPU checks
use the same exporter and `--platform macos` libraries, followed by `metal_probe
libraries output`. The host sandbox may require GPU access for that command.
Set `METAL_CATALOG_MANIFEST` when configuring to include the full-corpus metadata
test in CTest. For provenance, `METAL_SOURCE_REVISION` records the source commit
in GPU reports; device reports also record the app build. Bundled libraries must
include their matching `manifest.tsv`.

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
or per-instance streams with step rate one, uses complete constant-bank bounds,
and trusts the adapter's validated
index maximum. These boundaries must be expanded before enabling it for all game
frames. Removing the API translator does not remove the game's CPU work or GPU
shading workload; the gameplay gain needs measurement.

## Real-game texture upload validation

The first M5 gameplay capture exposed a neutral-to-Metal plane-pitch mismatch.
The draw adapter now passes a plane pitch only for 3D textures; 2D, array, and
cube uploads select each slice through its offset and slice index. Resource
bounds checks remain unchanged. Metal Lab build 6 adds a GPU pixel oracle with
padded rows and a nonzero payload offset to cover the actual capture contract.
Private gameplay captures remain outside Git.

The isolated replayer initializes an empty depth target to zero for Greater/
GreaterEqual comparisons, and one otherwise. The original pass clear and
attachment contents are not captured; this seed is explicit in each result.
A reversed-depth regression verifies that a full-target draw is visible.
Neither isolated visibility nor repeat identity establishes whole-frame parity.

## Ordered frame execution and host utility programs

The API-free `theft4_frame_plan` now preserves attachment generations,
subresources, load/clear/store actions, MSAA resolves, ordered draws and
rectangular clears, and exact-format texture copies. Mutable render targets
persist across passes and frames. Draws sample GPU-produced inputs directly;
static texture uploads retain the existing generation cache. Admission rejects
undefined reads, feedback, mismatched resolves, and out-of-bounds copies before
encoding. Partial clears/copies never imply that an entire new target is defined.
The target budget is 1 GiB; expired source owners retire their CPU cache entries
while submitted command buffers retain their GPU resources.

The host shader exporter translates the comparison renderer's exact 24 utility
programs, including depth handoffs, resolve conversion, SDR/HDR presentation,
SMAA, split post-processing and sun shafts. Their reflected constants and
texture slots use a separate ABI from guest draw shaders. All 24 compile for
both iOS and macOS. This establishes compilation and library loading coverage;
complete scene correctness still requires actual frame integration. GPU oracles
currently verify ordinary presentation color/orientation/alpha and GPU depth
handoff. Host utility commands are now part of the neutral ordered frame
contract and executor. Their named program, exact constant ABI, static or
GPU-produced texture inputs, fixed state, and sampler types are validated
before encoding. The game frontend still needs to produce this contract.

Metal Lab build 7 adds ordered-pass, rectangular-clear/copy, and host utility
checks, for 28 controlled GPU checks. The rectangle oracle covers independent
MRT clears, depth changes preserving stencil, stencil-only passes, restoration
of game draw state, and partial copies preserving outside pixels. Cold
rectangular-clear pipelines currently compile a small native MSL utility once
per attachment/mask key; prewarming or offline utility variants are needed
before measuring cold full-game startup. The regular shader libraries compile
offline and function/pipeline hits perform no compiler work.

Export and compile host utilities before configuring the Lab:

```sh
out/direct-metal/build/metal_host_shader_export out/direct-metal/host-source
python3 tools/direct-metal/compile_shaders.py out/direct-metal/host-source out/direct-metal/host-ios --platform ios
```

Set `HOST_METAL_LIBRARIES` to the compiled host directory when configuring the
iOS Lab. Host GPU checks expect those libraries and `HOST_SHADER_MANIFEST.json`
in the stock library directory's `Host` subdirectory. The full game still uses
the preserved comparison renderer; these checks do not establish gameplay FPS
or full-frame parity.

The ordered host-command oracle also verifies presentation from GPU-produced
scene color, a depth handoff between passes, cached host pipelines/constants,
and presentation from an immutable prepared ASTC image. Short constants reject
without changing stored output. Utility compilation coverage does not establish
SMAA, sun-shaft or split-postfx gameplay parity; those need real scene frames.

The ordered adapter also accepts a CAMetalLayer output allocation and presents
it in the same command buffer. Drawable loads, sampled drawable feedback and
blit/readback access to framebuffer-only textures reject explicitly. The final
host presentation program proves complete color coverage with full scissor,
no blend/depth/stencil tests and all channels enabled, allowing a DontCare load
instead of an extra clear. Partial or masked presentation cannot make that
claim. Metal Lab's captured-draw preview now uses two ordered passes and one
submission: the exact private game draw on seeded attachments, followed by the
actual host presentation shader into the drawable. This remains an isolated
slice, not a complete original game frame.

## Negative depth clip integration

The runtime shader key contains the original hash, late-alpha variant, and
vertex depth convention. A `-clip-neg` catalog entry must be a vertex shader,
have a stock base, and preserve its reflected inputs, specialization mask and
fetch bindings. Stock and converted PSOs, functions and bindings cache separately.
The game comparison capture frontend now admits this effective pipeline state
without approximating it. The capture remains an isolated draw mechanism.

The Mac GPU contract uses an actual stock game vertex/pixel shader through the
neutral draw adapter. Twelve cases verify negative depth, non-unit homogeneous W,
near/far endpoints and clipping, unchanged ordinary positive depth, depth-only
rendering, and a non-default viewport depth interval. Stored depth is checked
by a separate stock zero-to-one shader with an Equal comparison. This is shader
and draw-state validation; the full game still requires live ordered frame
production, backend selection and direct Metal presentation.

## Texture preparation in the live frontend

`theft4::astc::PrepareForBackend` accepts immutable untiled BC bytes/mips and an
explicit backend policy. It has no graphics API calls. BC-supported devices with
preparation disabled keep their original source, without duplicate payloads or
cache I/O. Unsupported BC devices retain the prepared ASTC cache, with RGBA control
or fallback when selected/required. Returned converted payloads own their bytes
and mip descriptions; rejected input leaves the previous output intact.

`PrepareNativeTextureDescription` prepares the real frontend's logical/physical
extent, virtual/reflection storage, guest mip count, format/aspect, usage and
sampled-view swizzle/range from explicit capabilities. Its allocation/view metadata
has no driver handles or pointer chains. The existing Vulkan consumer then creates
its allocation and attaches the real image handle. Optional diagnostic texture
packets are now prepared before that allocation or Vulkan upload; exceptional
reflection allocation recovery regenerates them against the accepted extent.

`source::DecodeImageUpload` lowers the prepared sampled metadata and per-mip
upload layout into the API-free immutable image contract. It preserves cube/array
planes, volume depth pitch, compressed block footprints, padded rows and the tight
last row; checks every mip/slice and rejects duplicate/missing or truncated inputs.
It returns layout only; the caller attaches the shared immutable byte generation
after validation. These are the actual game frontend preparation boundaries.
Full ordered Metal game-frame production and backend selection are still required.
