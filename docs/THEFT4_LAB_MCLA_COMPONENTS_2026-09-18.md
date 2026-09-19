# Lab 18: first MCLA draw-path component port

Parent: `1dcc904f8b5b49342bdac29d3f9451355005dfb9` (Lab 0.1.3 build 17).
Branch: `codex/lab-mcla-pipeline-efficiency`.
Backup branch: `codex/lab-backup-before-mcla-20260918`.
The original experiment branch remains at the parent commit.

## Scope

This build adapts MCLA's redundant geometry-binding suppression to Theft4's
existing Vulkan/MoltenVK command recorder and reuses immutable vertex-layout
requirements. It does not replace scene drawing with direct Metal. MCLA's
Objective-C++ renderer is title-specific and cannot be copied into this host
without translating the resource/shader/render-target contracts.

MCLA reference: `EveningGroupLA/METAL_TRANSITION.md`, especially the section
"Bloom-seed control and redundant binding suppression". Theft4 already has
completion-owned frame slots, persistent generation-keyed buffer caches, and
separate texture-content/sampler identities; duplicating those components would
add another ownership mechanism without demonstrated benefit.

Changes:

- Suppress repeat vertex bindings only when binding slot, actual host buffer,
  and byte offset match. Suppress repeat index bindings only when host buffer,
  byte offset, and index format match.
- Cover ordinary primitives, indexed primitives, user-pointer geometry, and
  generated quad/restart indices. All payload validation, conversions, uploads,
  and frame ownership happen before binding suppression.
- Reset binding knowledge at existing command-buffer and external-rendering
  boundaries. Pipeline switches preserve buffer bindings. Changed allocations,
  offsets, or 16/32-bit index formats always issue new binds.
- Memoize successfully resolved vertex-stream requirements on each immutable
  pipeline snapshot, shared by capacity planning, pipeline lookup, and drawing.
  New snapshots explicitly discard inherited requirements; missing/invalid
  layouts retain the original rejection behavior.
- Increment launcher-visible app version to 0.1.3 (18). Retain build 17's
  resolution/FSR controls and all preceding Lab behavior.

No graphics quality or frame-limiter settings change in this experiment.

## Backup and build

The signed Lab 17 app and its build/source/dependency receipts are preserved in
`out/m5-lab/backups/pre-mcla-pipeline-20260918/`. Its executable SHA-256 is
`40867557d5193f6108c2886017e9387158e78c9a41a0a3618cca0e57857fc16b`.
The cloned executable matches that digest and passes strict code-sign verification.
`rollback.json` records the source branch and installable app location.

Build through `tools/theft4_lab.py prepare`, then `build`, then `install`.
This exports the committed branch to the private source snapshot, configures
its dependency graph, rebuilds changed native backend code and affected callers,
relinks the Release app, signs it, checks release settings and Lab identity,
and archives the installable artifact. Frozen binary dependencies are retained
because no third-party library source or ABI changes.

Only bundle `com.theft4.m5lab` is an install target. The ordinary Theft4 app
and the A12X checkout are outside this experiment.

## Validation and acceptance

Before device build: 34 host regression tests / 2,256,057 assertions passed
with AddressSanitizer and UndefinedBehaviorSanitizer. These include a 2,000-draw
state-equivalence trace with changing buffers, offsets, index formats, pipeline
switches, frame resets, and external-rendering invalidation. Existing resource
lifetime, hot-path cache, and buffer-arena cases also pass. All 12 Lab-isolation
tests pass. Synthetic command suppression is not an FPS measurement.

First device comparison: keep the same 1080p resolution and FSR settings as
Lab 17, leave recording off, and compare the same standing/camera and driving
route. Inspect roads, buildings, characters, HUD/minimap, pause overlay, and
new-area transitions. A short subsequent logger run can compare command
recording and driver-bind costs. GPU time must be rechecked at 1080p; the prior
lower-resolution capture cannot establish current GPU headroom.

Device performance and visual acceptance remain pending until actual gameplay.
A complete direct Metal scene backend remains a separate milestone.
