# Build92 — hardware SMAA color filtering

Baseline: successful build91 source `2b47579ce70807498e13a2250b56777894129772`.
Latest91 run is preserved in `../build91-user-capture-2026-10-02/`; user reports
“This DEFINITELY HELPED.” It recovers to33.3–33.5ms in several intervals but the
late heavier view still averages40.05ms. CPU fixed work still slows after onset.
This next GPU optimization does not claim to identify or prevent that controller
transition. See the capture’s ANALYSIS.md for measured limits and scene caveats.

## Changes

- Hardware SMAA Filtering defaultsON, independently reversible in System with
  a full app restart. Existing Graphics Preparation and Fused SMAA choices remain.
- Eligible GPU-produced2D, single-mip, single-sample8-bit RGBA/BGRA UNORM images
  permit a sampling-only corresponding sRGB view. It shares original storage;
  no extra full-resolution image, copy or per-frame color-conversion pass.
- The neighborhood blend uses two hardware-filtered, linear-light samples in
  place of eight explicit texel fetches and per-texel RGB conversions. Encoded
  edge detection, original weights, alpha, final color transfer/dithering and
  fused half-precision rounding remain. Hardware filtering/conversion precision
  may differ slightly from the manual floating-point implementation: inspect
  color/AA around edges during the user's comparison.
- An explicit UNORM/sRGB-only format list preserves MoltenVK's optimized Metal
  usage. Matching properties2 capability queries include the actual creation
  list. The image pool compares the two view formats along with all its prior
  creation fields. Arbitrary extension chains remain rejected/unpoolable.
- The sRGB view is created lazily once per source-image lifetime and destroyed
  with that image after the existing submission/descriptor retirement boundary.
  Shader and encoded views share one layout/transition; no extra image barrier.
- Missing capability/extension/function, nonidentity channel mapping, FP16/HDR
  source, allocation/view or optional pipeline failure retains manual filtering.
  Original image-creation contract is retried if optional creation fails.
- Pipeline variants are created once when needed, before recording commands;
  failures do not retry compilation on every frame. Descriptor pool budgets now
  reserve the seventh SMAA sampler descriptor. No new worker or driver rebuild.
- All four capture headers derive build identity from CFBundleVersion, fixing
  the stalebuild90 labels in91. Renderer schema251 appends effective hardware
  switch and cumulative requests/frames/fallbacks; existing fields keep order.

## Validation and undo

Host glslang compilation succeeded for both new shader variants. Manual SMAA
and HDR fallback binaries remain byte-identical to91. Release app compilation
succeeded; source review covered image flags/capabilities, pool identity,
descriptor counts/layouts, color/alpha, barriers and destruction. Instruction
inventory confirms zero explicit OpImageFetch in the hardware color-filtering
function; this is code evidence, not a measured GPU/FPS improvement. No automated
implementation tests or agent gameplay requested/executed. Signature/dSYM/data
preservation checks and device delivery are recorded separately.

Undo: System → Hardware SMAA Filtering OFF; fully close and reopen. This selects
manual filtering and original image creation while retaining91 optimizations.
Signed91 app/dSYM remain at `out/backups/build91-graphics-efficiency/` for exact
binary rollback. Save/settings backups are preserved before updating92.

## User benchmark

1. Same save, Native resolution, SMAA, same graphics options and actual Game
   Mode state. Keep Graphics Preparation/Fused SMAA/Parallel Render Preparation
   ON and the existing Runtime Wait Improvements OFF.
2. New Hardware SMAA FilteringON. Enable Long Performance Capture before Play.
3. Stay inside with position/camera still through the hitch and30seconds after.
   Walk outside, stand still30seconds, then slowly turn30seconds. If no hitch,
   remain in gameplay at least2minutes before finishing.
4. Stop/save; wait5seconds. Report outdoor settled timing, turning stutter and
   any visible color/AA change. Save the run even if the result is worse.
5. If comparing directly, switch only Hardware SMAA FilteringOFF and fully
   restart; repeat. Its capture counters distinguish requested from active.

No external Instruments/sysdiagnose capture is necessary for this comparison.
App installed but left closed; the user starts the run.

## Documentation and implementation sources

[Apple's Metal texture guide](https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Mem-Obj/Mem-Obj.html)
documents sRGB decoding before sampling and storage-sharing views.
[Vulkan image format lists](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageFormatListCreateInfo.html)
extend both creation and properties2 queries;
[view usage](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewUsageCreateInfo.html)
restricts the new view to sampling.
Active driver source: XeniOS/third_party/MoltenVK/MoltenVK/MoltenVK/GPUObjects/
MVKImage.mm, `getMTLTextureUsage` lines1144–1163. Actual app still links the
existing prebuilt driver; no dependency modification. GPU speedup and visual
correctness remain pending the user's run.
