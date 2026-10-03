# Build 91 — graphics preparation and fused SMAA

Baseline: build90, commit `465b7caf8a6e8a2fbb0e95eac01b4d66fe876a69`.
Two independent System switches default ON and require a full app restart.
User preferences, including Runtime Wait Improvements OFF, are retained.

## Implemented

- **Fused SMAA Presentation:** edges and weights retain their original shaders
  and required clears. Compatible equal-extent presentation blends the SMAA
  neighborhood directly into the existing guest-output image. The separate
  FP16 neighborhood target is allocated lazily only when the old chain needs
  it. SDR/HDR/Auto HDR, alpha and dithering share the presentation color code;
  pack/unpack half preserves the old intermediate's rounding. At 2416×1359,
  the omitted logical image is 25.050 MiB. Nominal write/read removal is
  52.534 MB/frame, not measured DRAM bandwidth or a predicted FPS gain.
- **Graphics Preparation:** known shader usage unions include stock early/late
  modules and all accepted overrides. Vertex-only passes can now reuse
  constants; a bank proved unread by all possible selected stages uses a valid
  immutable zero allocation in a separate identity namespace. Unknown usage
  keeps the full-bank path. Guest state versions and updates remain intact.
  Projection memo grows from 64 direct entries to 64 four-way sets per bank.
  Helpers remain the exclusive owners until the existing join/fence boundary.
- Successful pipeline request receipts omit dynamic viewport/scissor, blend
  constants, stencil refs/masks, depth-bias values, alpha reference, coverage
  offsets and clip-plane values. Static enables and policy/lifetime/format/
  layout/stride identities remain. Blend/stencil portability rejection is
  checked on every shared lookup; the exact original per-snapshot memo stays.
- Existing component-mask pass reuse extends across phase labels only when
  physical views, guest descriptors, placement content/revision, extents,
  samples and layouts all match. Only 1x offscreen non-reflection scopes;
  no attached producer resolve or diagnostic scope may cross this path.

## Fallback and boundaries

Disable both new switches and fully close/reopen to select build90 rendering
paths. The signed build90 app/dSYM are also retained for binary rollback.
Fusion preflights all resources before recording and uses the original chain
if it fails. Different extents, SSAA, FSR1 and affected image diagnostics keep
separate presentation. Diagnostic constant validation keeps full-bank bytes.

This changes the active Vulkan/MoltenVK renderer, not the inactive Metal shell.
It retains original draw order, guest alias/lifetime rules, stores, resolves,
thread priorities and simulation timing. It does not introduce Metal4/frame
generation or blindly discard stores. Whole-frame alias/liveness planning and
driver residency traversal remain larger work requiring measured attribution
and complete resource proofs. Existing indexed argument buffers and depth
attachment resolves are preserved.

## Capture counters

Long Capture has 247 renderer fields, with 11 appended fields: effective
switches, cumulative fusion requests/success/fallback, cumulative normalized
pipeline requests/hits, cross-phase scope reuse; current frame unused-bank
bind observations/logical sizes and projection reuse. Unused-bank logical
bytes count observations and must not be treated as measured upload savings;
existing upload counters remain the actual allocation byte evidence. The
SMAA-neighborhood GPU range includes presentation while fused. No new per-draw
timer or external recording is required.

## User comparison

Same save, Native resolution, SMAA and original graphics settings. Both new
switches ON; keep other choices unchanged, including Runtime Wait OFF.
Enable Long Performance Capture, stand inside with the camera still through
the hitch and 30 seconds afterward; go outside, stand 30 seconds, then slowly
turn for 30 seconds. Stop/save and wait five seconds. Note any color/AA/shadow
change. A short unlogged run can confirm ordinary play responsiveness.

If comparison is needed: turn Fused SMAA OFF only and fully restart; then keep
it OFF and turn Graphics Preparation OFF for the old paths. Source/build/
signature/dSYM verification do not establish runtime correctness or speedup.
Performance, effective fusion eligibility and transition recovery remain
pending the user's run. No automated tests or agent gameplay requested/run.

Sources: Apple load/store action guidance
https://developer.apple.com/documentation/metal/setting-load-and-store-actions/
and Vulkan pipeline/dynamic-state specification
https://docs.vulkan.org/spec/latest/chapters/pipelines.html .
