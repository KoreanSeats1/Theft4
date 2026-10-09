# iOS Mods

The launcher includes a **Mods** tab. Its first entry, **Custom Time Cycle**, is
an optional bundled `timecyc.dat` replacement with an example screenshot and a
developer-credit area. It is off by default. The selection is saved across app
launches, independently of graphics presets and diagnostics.

### iPhone launcher layout

On iPhone, the five launcher tabs share a compact horizontal navigation bar.
Settings use the available panel width rather than a landscape sidebar. Portrait
uses one column of cards; landscape uses two columns when at least 680 points
remain inside the safe horizontal margins. Narrower windows and accessibility
text sizes keep one column. Cards reflow using the existing controls, so changing
orientation does not recreate switches or reset selections. Each tab scrolls
vertically, and the home indicator and display cutout remain outside the panel.

The phone's Custom Time Cycle card uses a 132 × 99 point example thumbnail with
the developer credit beside it. Phone descriptions are shorter while retaining
the launch requirement, reversibility and gameplay caveats. Segmented choices
use compact labels and at least 44-point control height. iPad keeps its existing
sidebar layout and full-size preview. The isolated preview supports `--portrait`
and `--landscape` with `--display`, `--interface`, `--system` or `--mods` for visual
checks; it does not start the game or access physical-device game containers.

Enable it before pressing Play. To change it after starting the game, save,
close Theft4, reopen, and change the toggle. Disabling it uses the original file
on the next game launch. It changes the title's lighting, sky, fog and weather
parameters rather than supplying replacement textures. Appearance depends on
time, weather and graphics settings; the preview is an example, not a promise
that every scene looks identical.

## Resources and credits

The supplied resources are kept in `ios/Theft4/Mods/CustomTimeCycle/`:

- `timecyc.dat`: the supplied replacement, preserved byte for byte.
- `preview.jpg`: the supplied example screenshot, preserved byte for byte.
- `metadata.plist`: developer credit, **SBerrix**, supplied by the user. The mod
  card displays their name and “Thank you for the custom time cycle!” beside
  the example on iPhone and below it on iPad.

Both the game bundle and isolated launcher-preview bundle copy these resources
to `Mods/CustomTimeCycle`. This first implementation is a curated bundled mod,
not a general installer for arbitrary PC mods or executable hooks.

## File loading and reversibility

The controller passes the selected bundle directory to the startup worker only
when enabled. Off explicitly removes the override from the launch environment.
Verification and texture preparation continue using the real game directory.

After runtime setup and before launching guest code, startup validates the
replacement and mounts a read-only host device. A guest VFS symbolic link
redirects only the canonical base-game path:

`\Device\Harddisk0\Partition1\xbox360\data\timecyc.dat`

The title's `platform:`/`xbox360:` aliases and the runtime's `game:`/`d:` aliases
converge on that path. Aliases created later by the title still see the override.
Similar filenames such as `timecyc.dat.bak` continue resolving to their original
entries, despite the VFS's prefix-based link matching. This uses the existing
absolute-path `OpenFile` handling; it does not change the SDK or renderer.

No game file is renamed, patched, deleted, copied over or backed up. Mod Off in
a fresh runtime simply omits the extra devices and links. Saves, game verification,
shader caches and prepared ASTC textures keep their existing paths. There is no
conversion sweep and no added per-frame rendering pass, polling or file hashing.
The changed atmosphere parameters themselves may affect the title's workload.

### Build 127: packed-file loading

Physical M5 file tracing showed that the title opens `game:/xbox360.rpf` and
loads its time-cycle entry internally. It never opens the loose table through
the kernel VFS, so the original loose-file override could be enabled while
leaving packed installs visually stock.

When the mod is On and `xbox360.rpf` exists, startup now prepares a private
archive under `Library/Application Support/Theft4/startup/mods/custom-timecycle`.
It decrypts the RPF2 table with the installed game's AES key, resolves exactly
`data/timecyc.dat` through the directory graph, appends the bundled replacement
as ordinary uncompressed data, and changes only that entry's size, offset and
storage flags. The modified table retains the original 16-pass AES encryption.
All original payload bytes and other table entries are preserved. The existing
loose-file link remains available for installs or title paths that use it.

The kernel VFS also redirects the original archive path to this read-only
private file. The two custom devices have distinct, non-overlapping mount
prefixes because the SDK resolves devices in registration order. Similar names
such as `xbox360.rpf.bak` still resolve to the original game root.

Preparation is outside gameplay and requires roughly one archive's additional
storage (about 58 MiB for the inspected installation). Subsequent launches reuse
the private file after small header/table/replacement checks. Its identity
includes source path, archive table, size and modification time, bundled data
and preparation version, so normal game-file or mod updates produce a fresh
copy. A corrupt replacement or table is rebuilt. Gameplay uses normal file I/O;
there is no per-frame repacking, hashing, extra shader pass or original-file edit.
Off restores the original archive route on the next app launch.

The encrypted and plaintext synthetic fixtures cover exact entry targeting,
duplicate basenames in other directories, sibling preservation, cache reuse,
corruption repair, source invalidation and malformed-header rejection. A separate
test against the real encrypted Xbox archive confirms one logical entry changes
and every original payload byte outside the table is identical. Device proof
must additionally confirm the title opens the private archive and parses the
replacement; a launcher enabled message alone is insufficient.

The supplied file and the inspected original each have 99 numeric rows with 134
values per row (nine weather tables, eleven time slots each). Startup rejects
missing, oversized, malformed, non-finite or truncated replacement files and
missing original base-game files with an explicit message. This structural check
is not a claim that every lighting parameter has been validated in gameplay.
The original file need not match a fixed checksum, preserving user file sets.

## Validation

`tests/ios/mods` is a standalone host harness using the real ReXGlue filesystem.
It checks valid and malformed tables, missing-resource rejection, case-insensitive
and slash-normalized aliases, aliases installed after the mod, absolute file
opens, read-only access, similar filenames, untouched sibling files, and a fresh
Mod-Off runtime returning to the original. Fixtures use temporary directories;
no device game files or user preferences are touched.

```sh
cmake -S tests/ios/mods -B out/mods-validation/host -DCMAKE_BUILD_TYPE=Release
cmake --build out/mods-validation/host --parallel 4
ctest --test-dir out/mods-validation/host --output-on-failure
```

`ios/launcher-preview` supports `--mods` to show the real card without starting
the game. Final device validation should compare the same save, time and weather
with the mod On and Off, and confirm returning Off restores the original look.

## Native Aspect Ratio

The second Mods entry is an independent, saved opt-in switch, off by default.
It fills the app window by rendering additional scenery. On narrower iPad
landscape screens it retains the original horizontal field of view and expands
vertically; wider iPhones retain vertical field of view and expand horizontally.
It does not zoom or crop the original 16:9 scene to hide the bars. This uses the
existing direct Metal renderer and does not add another composite pass.

Select the mod before Play. The window shape is latched when creating the game
render graph. Rotation or multitasking afterward fits that same shape into the
new window without stretching; reopen to launch at the new full-window shape.
Videos and fixed loading artwork preserve their authored proportions. The mod
is not a promise that authored cinematic mattes will disappear.

### Camera, visibility and effects

The port already contains verified aspect adapters; iOS formerly forced their
16:9 mode. The new startup policy supplies the actual launch-window dimensions
independently of scene-resolution rounding and enables their display mode.
A headless iOS runtime has no desktop display-window object, so the resolution
selector now consults this explicit display shape; desktop/default behavior
continues using its existing fallback.

The primary screen-camera owners are `0x820B9284`, `0x820212F4` and `0x820BCB40`.
For these cameras, `sub_821ED2F0` supplies the resolved aspect and
`sub_828BDAD8` temporarily expands the authored vertical FOV on narrow screens.
It runs the original projection builder, its view-projection rebuild
`sub_828BD1D8`, and its visibility-plane rebuild `sub_828BD770`. Authored FOV
input is then restored to prevent repeated rebuilds accumulating expansion.
Resolved projection coefficients, cached tangents (+712/+716) and visibility
planes remain expanded. Late camera-owner assignment is checked at viewport
binding, including the independent phone's homogeneous safe-area translation.

This updates the camera information the title's render graph, geometry culling,
lighting and post effects consume. For example, `sub_82293878` and
`sub_822CF9D0` upload the current viewport's resolved tangents, near and far
values, while the native environmental-data capture reads its updated projection
and view-projection matrices. Shadow maps, reflections, radar and other
independent offscreen views retain their own shapes and depth conventions; they
must not be stretched to the display ratio. End-to-end shadow coverage in the
extra scenery still needs device gameplay checks.

The existing graph resource-size and attachment-aware viewport/scissor hooks
receive the expanded extent, covering the scene, depth and screen-space passes.
Scene resolution, FSR's logical video budget and the physical drawable are
selected separately. Integer fitting is performed once, matching the actual
render-graph selector; the launcher reports that resulting allocation. Existing
fixed-height choices expand one axis at the original central pixel density.
Native Pixels uses the full window's physical pixels. The existing 4095-pixel
logical resource limit bounds future very large windows uniformly rather than
clamping axes independently; exceptionally large/tall windows can therefore
reduce pixel density. Fixed-output phone/older-device profiles retain their
output-height budget while allowing the additional aspect dimension.

### Interface and cost

Existing HUD/font/queued-draw/menu/phone layout adapters preserve proportions.
Edge-anchored HUD components and the phone move inside normalized iOS safe-area
insets. Centered content and world-projected anchors do not receive that extra
safe-area translation. Game simulation, physics, traffic, input, draw-distance
and quality selections are unchanged by this toggle.

More pixels and visible scenery can increase GPU work, CPU submissions and
memory use. There is no claim of unchanged FPS. The option is independent of
graphics presets, Custom Time Cycle and ASTC preparation. It does not modify
game files or require another texture conversion. Off restores the original
16:9 policy on the next fresh session.

### Validation and remaining device check

The standalone host harness adds the following checks:

- 220 expanded render/FSR/profile/display combinations, including actual iPad
  and iPhone shapes, portrait/multitasking and larger future targets. It compares
  reported allocations against the real SDK resolution policy.
- The existing centered 16:9 resolution contract and aspect-layout roundtrips.
- The shipping camera adapters and original generated PPC projection,
  view-matrix multiplication and frustum builders. The build extracts those
  bodies from their current sources rather than maintaining a separate math
  implementation. Guest page validation, logging, the `tan` library boundary
  and GPU-only constant publication are isolated test doubles.
- All three supported screen owners in both normal and reversed-depth modes:
  additional edges admitted by actual culling planes, unchanged depth/W,
  updated projection/tangent data, no cumulative FOV growth, late owner repair,
  offscreen isolation, and phone safe-area translation including a 16:9 window.

Builds and CPU checks cannot establish perfect coverage in every gameplay pass.
Before release, compare the same save with On/Off on an iPad and a wide iPhone:
check the retained center, extra geometry at all edges, shadows in daylight and
night lighting, mirrors/water, HUD/radar/phone, menus/hit regions, aiming and
cutscenes. Repeat with FSR and native pixels, and rotate/resize after launching
to confirm proportional fitting. Record performance at identical quality
settings. No physical-device installation or gameplay result is claimed by the
host harness.

The isolated launcher preview accepts `--native-aspect` to enable the switch
and `--aspect-mod` to scroll to this card, without starting the game.

### Build 125: odd-width effects resolve correction

On-device build-124 diagnostics reproduced a rejected color resolve when the
M5's expanded scene was 2421 × 1668. The title's half-size 4x sample view covers
2420 × 1668 sample positions, leaving the last column outside that effects view.
The bloom chain also floors both dimensions of odd intermediate targets. The
Metal resolve selector previously required an identical sample width, although
it already allowed a cropped height, and rejected the whole frame.

Build 125 permits a fully contained color sample rectangle with the same guest
placement base and sample pitch. It selects the newest live writer across that
row layout before checking containment, format and defined contents. A newer
narrow or short write therefore cannot expose an older larger allocation. Exact
placement keys and depth rules remain unchanged. Copy coordinates use the
writer's full extent, preserving the crop rather than stretching it over the
last row or column. This corrects the effects aliasing without reducing the
scene resolution or changing camera framing.

The host regression covers the actual 2421 × 1668 scene, 605 × 417 intermediate,
odd phone shapes and original even modes, including oversized, mismatched,
empty and depth-source rejection. Physical-device results are recorded in
`out/mods-validation/native-aspect-failure-20261006/`; compilation and host
results alone do not establish on-device presentation success.

The same device's crash report separately showed the in-game Quit action
calling `exit()`: C++ static destruction began on the main thread while guest
threads still accessed synchronization objects. The fresh-launch action now
uses `_Exit()` after its explicit settings save and queued capture writes. It
does not destroy a running runtime in place or run process-wide destructors
concurrently with guest execution.

The subsequent M5 build-125 run published thousands of frames beyond the
previous first-3D rejection, and the user confirmed the expanded picture worked.
They also reported misaligned shadows: presentation success is therefore not
the same as complete aspect-ratio correctness.

### Build 126: camera-derived projection consistency

Inspection of TU8 `sub_827BCD28` and `sub_827BCF90` found two auxiliary render
paths constructing projections through `sub_828BE580` from the primary
viewport's authored FOV. The primary matrices and cached tangents were already
expanded, but these consumers reread +696 directly. The second path additionally
computes its own tan/sin/cos for frustum fitting from that same authored value.

The native projection hook now expands the FOV argument at exactly the two
verified return addresses, `0x827BCE90` and `0x827BD198`, after checking the source
is a supported screen camera at the current render/output extent. Three matching
trigonometric call sites in the second path receive the same resolved half-angle,
including the original single-precision multiplication. This keeps the auxiliary
projection and fitting calculations consistent with the primary camera without
temporarily editing shared camera memory, expanding unrelated light-space or
reflection views, or accumulating FOV changes. Authored camera input remains
available to animation and gameplay.

The CPU regression now executes the original PPC auxiliary projection setter
and builder for both call sites and all three screen owners in both depth modes.
It demonstrates the old mismatch on narrow displays, compares all 16 corrected
projection coefficients to the primary matrix, checks the trigonometric
half-angle against the cached tangent, and preserves unrelated projection calls.
This supplements the existing camera/frustum checks; daylight shadow placement
still requires a physical-device comparison before release.

### Build 128: copied-camera auxiliary projections

The user still observed shadow displacement increasing toward the screen edges
in build 126. The M5 trace subsequently showed the auxiliary paths accepting
standalone/copied `grcViewport` objects rather than only owner-embedded screen
cameras. The owner whitelist therefore skipped valid source cameras. Those
copies can retain a resolved projection and tangent while +696 remains the
authored animation input.

At the same two verified auxiliary call sites, the adapter now derives the FOV
from the source's cached vertical tangent, validating its consistency with the
source projection coefficient and projection scale. It retains the full-output
extent gate. This uses the projection actually represented by that camera,
without expanding the authored FOV twice or treating every offscreen camera as
a screen camera. The three associated trigonometric inputs use the same result.
Unrelated callers and invalid/inconsistent camera data retain their original
inputs. The tests exercise all original camera cases with copied-camera owner
values, including the values observed on-device. Physical shadow alignment
validation remains pending; a passing camera test alone is not a visual claim.

Diagnostic traces now deduplicate consecutive projection shapes before counting
their bounded per-role budget, preserving evidence beyond repeated loading
frames. They remain disabled during normal retail launches.

### Build 129: retain resolved projections in lighting snapshots

The next M5 trace established a second, direct mismatch: for the same authored
45-degree camera and 2421×1668 output, geometry's vertical tangent was 0.5073446
and projection Y coefficient 1.9710469, while deferred lighting and post-effects
used 0.41421357 and 2.4142134. The ratio is the aspect expansion factor. A
reconstruction ray therefore disagreed increasingly with geometry toward the
screen edges. The user confirmed build 128 still had moving shadows, while the
timecycle replacement was now visibly applied.

The two original complete viewport-copy adapters now prepare a late-assigned
screen source before copying it. The projection adapter also recognizes a plain
copied viewport only when its full-output extent, stored aspect, cached tangents
and projection scales agree with an already expanded screen camera. Subsequent
original projection rebuilds preserve that expansion while restoring the
authored FOV input. This avoids an address-based provenance cache and does not
expand arbitrary generic, reflection or shadow-map cameras.

The regression reproduces the original loss during a copied-camera rebuild,
then checks all 16 projection coefficients and screen-ray reconstruction from
center to ±0.95 normalized device coordinates. It also checks resolution before
copying a camera whose screen owner was assigned after construction. All eight
host tests pass. On-device agreement and visual alignment remain validation
gates. The user reported staticky audio during the non-retail diagnostic run;
normal-retail audio must be checked separately before attributing that symptom
to diagnostic overhead or a regression.

Build 129 was installed and its app version verified on M5 and Air. The M5
trace now shows matching geometry, deferred-lighting and post-effect tangents
and projection scales, including animated FOV values. The user confirmed the
shadows now look correct. The diagnostic process was terminated and the M5 app
reopened without tracing overrides. The user confirmed the static is gone in
that normal run. Air's visual comparison remains unverified.

### Builds 130–131: bandwidth pass for the expanded viewport

The user reported 38–42 ms in the expanded view after validating the camera
and audio fixes. The saved build-129 outdoor trace contains GPU envelopes of
roughly 33–39 ms while renderer CPU work remains below 26 ms in those samples.
There are no new pipeline compilations or buffer evictions in the cited heavy
frames. This points the next investigation toward GPU work rather than another
cache-size increase. It is not a controlled aspect-ratio comparison, and the
coarse timings cannot identify an individual expensive shader or pass.

The corrected build 131 changes ordered attachment preparation without changing geometry,
shaders, projection, draw distance, shadows, or texture precision:

- Consecutive full clear-only passes can combine their attachment sets when
  extents and sample counts match. The first geometry pass that loads that
  complete set inherits the original clear values as Metal load actions.
  This removes intermediate clear stores and subsequent loads, allowing the
  initialization and geometry work to share tile memory. Draw order remains
  unchanged. A pass assembled before its draw is appended uses the same rule.
- Copies, resolves, rectangular clears, clears after draws, incompatible
  mip/layer/extents/samples, and aliased MRT slots keep their boundaries.
  Combined depth/stencil attachments must refer to the same storage. A
  clear-only prefix never adds unused attachments to a real draw's contract.
- The existing full-overwrite optimization now recognizes the packed depth
  alias shader. It always writes every color channel without discard; an
  old destination load/clear is unnecessary when coverage is complete, with
  blending, depth/stencil tests, channel masks, and sample coverage checked.
  This runs after successful whole-frame admission; admission is not weakened.

Validation: 26 native-rendering contract tests and all eight mod tests pass.
The live Metal backend GPU test on Apple M1 Max reduces a representative
two-color/depth/stencil initialization and consuming pass from five passes to
one. Both color images and the packed depth/stencil image are byte-identical,
including subsequent partial writes. Existing MSAA resolves, depth handoffs,
odd extents, effects, SMAA variants, and readbacks also pass. All 2,736 bundled
shader libraries are byte-identical to build 129. The iOS Release build and
strict app-signature verification succeed.

These are verified reductions in redundant operations, not a measured iPad
frame-time gain. A short per-pass GPU capture of the same heavy outdoor view is
still required to rank geometry, lighting, overdraw and post-effects. Keep the
diagnostic run separate from the normal retail performance comparison: GPU
counters alter the workload, and the earlier diagnostic launch had audio
static that disappeared in the normal run.

Build 130 regressed on the M5 shortly after entering 3D. Its generic pass
builder inferred that an empty scope with all-Clear load actions was a
clear-only operation. The live producer also constructs first-use geometry
scopes in exactly that state, then appends their draws after the builder
returns. Combining a preceding unrelated clear into that scope adds an
attachment absent from the draw's pipeline. An isolated reproduction of
build 130's classification rejects with **“Draw color formats differ from its
ordered pass”**. The normal retail run did not write a fresh diagnostic log;
this is a reproduced code defect, not a log-confirmed on-device diagnosis.

Build 131 replaces that inference with an explicit `AppendClearPass` entry
point at the actual clear producers. Ordinary `AppendPass` never adds an
attachment to a geometry scope, even when its draw has not been appended yet.
CPU coverage includes that exact ordering. The live GPU backend also verifies
a depth/stencil clear followed by a separate first-use color scope and later
draw: no unrelated attachments are introduced, and the output is byte-identical.
The valid five-to-one clear-prefix optimization still passes its GPU comparison.
The user confirmed that build 131's expanded viewport looks correct and its
performance is solid. The remaining reported problem is stretched pause-menu
and minimap UI, addressed separately below.

### Build 132: queued UI proportions

The expanded 3D camera needs its own aspect ratio; the console's authored 2D
interface still needs a proportional 16:9 coordinate mapping. Three gaps in
the existing UI adapters allowed particular draws to bypass that mapping:

- The normalized four-vertex command with vtable `0x82001418` was missing
  from the layout-capture allowlist. Its executor, `sub_821BD138`, retained a
  layout Scope but emitted vertices directly through `sub_828C2290` without
  activating the transform. It now uses the same emission adapter as the
  other normalized radar primitives.
- Queue publication now captures a verified active display-UI layout even
  when the producer is outside a named component Scope. Playback on another
  thread uses that captured context, rather than requiring the original
  producer's viewport to remain active. Instance-token/vtable checks and the
  bounded history still prevent recycled storage from inheriting old layouts.
- The full pause/frontend compositor (`sub_8214DBD0`) now captures its heading,
  tabs, panels and footer, alongside the existing divider-anchored list/slider
  bodies. The HUD radar compositor (`sub_8239C468`) gives its border, mask,
  blips and tiles one bottom-left anchor, rather than scoping only the tiles.

No camera/projection, shadow reconstruction, render-pass optimization, shader,
texture format or gameplay change is included in this correction.

`aspect_ui_test` extracts the shipping adapters and original PPC quad/panel
executors and vertex writer. It checks actual generated vertices on iPad,
wide iPhone and 16:9 shapes, including physical square proportions, untouched
UV/color values, cross-thread queue playback, changing command-size bits,
recycled-command isolation, pixel-space panels, compositor scopes, font-state
restoration and exclusion of already baked glyphs from a second transform.
GPU submission, guest page validation and the font append boundary are isolated
test doubles. The pre-fix extracted adapters fail the same vertex fixture;
all nine mod tests pass with the correction. The existing real camera/frustum
test continues to pass. A device check of the pause screens and minimap remains
required; host tests do not establish complete visual coverage of every menu.

## God Mode

The third Mods switch is **God Mode**, saved as `Theft4ModGodMode`, default Off.
The selection is latched before the guest starts; after Play, save and reopen
Theft4 to change it. The native backend receives an explicit boolean through
`THEFT4_MOD_GOD_MODE`. Unset or any value other than `1` means Off.

The implementation in `gta4_gameplay_mods.cpp` follows the TU8 game code rather
than periodically restoring health. For the current local-player ped only, it
sets the original invincibility bit and the five damage-proof bits at entity
`+280`. At ped `+560`, it disables water/sinking-vehicle drowning, instant death
in water and death when injured, and disables critical hits. These are the
same bits the original script-native setters write. Script changes to these
flags run normally, then reassert the local player's protection. NPCs retain
all original script and damage behavior.

Protection is applied when the local-player lookup publishes a new ped and before
physics updates. The player pointer is read fresh from the game's player table,
so load/respawn transitions do not dereference a cached character address.
A publication hint skips repeated player lookups without heap queries or an
inventory scan; it is reset on runtime configuration and a null local lookup.
The physics boundary still validates and applies the current player each update,
including a respawn that reuses the same address. Every
access checks the guest heap and page permissions. Script handles are resolved
through the ped pool with bounds and generation-tag validation.

The common health-write leaf (`sub_8247E8A0`) blocks reductions to a living local
player while allowing healing/initialization. The `SET_CHAR_HEALTH` core
(`sub_825BB5F0`) also blocks reductions before its original death-task side
effects. The explicit `EXPLODE_CHAR_HEAD` core (`sub_825BBFE0`) cannot kill the
protected player. Damage reactions and the rest of the simulation are retained.
This mod does not make the player's vehicle or every NPC invincible. God Mode
can change the outcome of mission scripts that expect the player to die.

## Unlimited Ammo

The fourth Mods switch is **Unlimited Ammo**, saved as
`Theft4ModUnlimitedAmmo`, default Off. It is independent of God Mode and uses
`THEFT4_MOD_UNLIMITED_AMMO`, latched at the same fresh-session boundary.

TU8 stores the active weapon's magazine at `+28` and total ammunition at `+30`.
Its common firing path (`sub_8226F428`) consumes these separately. The mod runs
the original firing function unchanged, then preserves the player's total
ammo. It never restores the magazine, so magazines empty and the original
reload/animation code still runs. NPC weapons use the unmodified path. The
weapon must belong to the currently published local ped and match its active
inventory slot; after firing that identity is checked again before writing.

Owned firearm/projectile inventory slots receive a finite total-ammo floor of
1,000 when their reserve is low, including owned weapons with no ammo left.
Unarmed/melee and unowned slots are not granted ammunition. The active weapon
and inventory totals agree, and the magazine remains untouched. Higher existing
ammo counts are preserved. Weapon changes, local-player publication and the
physics boundary refresh owned reserves. The firing hook replenishes only the
validated active weapon when needed and preserves its total after firing; it does
not rescan the whole inventory for every shot. Both ammo and protection operations
are skipped when their individual switches are Off.

The original engine's special 25,000 count is a persistent infinite-ammo sentinel.
This mod deliberately does not write that value: ammo acquired while enabled
can be saved, but switching the mod Off restores ordinary consumption rather
than leaving a permanent infinite-ammo weapon in the save. The launcher explains
that replenished ammo counts may be included in a game save. It does not patch,
replace or delete game files or saves.

### Validation and remaining gameplay checks

`tests/ios/mods/gameplay_mod_test.cpp` exercises the shipping adapters with
extracted original TU8 local-player lookup, health leaf, flag setters, firing
consumption block and complete reload function. The guest page validator,
weapon metadata lookup, unrelated ballistics and script-task scheduling are
controlled test doubles; this is not a complete headless game session.

Covered cases include disabled baseline behavior, independent/combined toggles,
NPC isolation, stale script handles, health reductions versus healing, script
flag resets, water/critical-hit protection, explicit head-explosion blocking,
empty-reserve owned weapons, unowned slots, 2,100 shots/70 reloads, 45 slot and
magazine-capacity combinations, higher existing totals, invalid player memory,
weapon-identity mismatches and new local ped publication. iOS Release and the
isolated simulator launcher preview also build successfully. These checks do
not establish end-to-end combat/death behavior or device frame-time cost.

Before publishing, test on-device: gunfire/headshots, fire, explosions, falls,
vehicle collisions, swimming and a sinking vehicle; confirm health/armor are
not consumed. Fire each available weapon type until its magazine empties and
reload manually/automatically, switch weapons and use an owned weapon with zero
reserve. Repeat with both mods Off after a fresh app launch, load/save and switch
characters/episodes where supported. Verify NPCs still take damage and consume
ammo. All earlier Custom Time Cycle and Native Aspect Ratio checks remain
required independently.

## Planned Realistic Vehicle Handling

Research and implementation plan, **6 October 2026**. This mod is not included
in build 124. That build contains the four mods above and the iPhone launcher
layout changes; installation was verified on the M5 iPad and iPhone Air.
Vehicle profile generation, the new toggle and transmission hooks described
below are proposed work, not completed features.

The recommended implementation is an original, curated vehicle profile applied
at startup through a read-only file override. It will retain the game's vehicle
models, collision geometry, renderer and physics timestep. Exact transmission
behavior is a second engineering milestone with its own validation gate. A full
replacement physics engine is not required for the initial handling mod.

### Research findings and compatibility

| Reference | Useful evidence | Constraint for this port |
| --- | --- | --- |
| [Killatomate's Realistic Driving & Flying](https://www.gtagarage.com/mods/show.php?id=5529) | Author describes broad vehicle acceleration, speed, traction and suspension tuning. | Published compatibility is PC-oriented; TU8 compatibility and adaptation/bundling permission have not been established. |
| [Shturmovik2's Realistic Handling & Physics](https://www.nexusmods.com/gta4/mods/195?tab=description) | Author describes vehicle-specific performance and handling targets. | Tested PC versions do not establish 360 compatibility. The published permissions require permission for modifications and asset use. |
| [Chr0m3 x MoDz Realism 1.0](https://community.wemod.com/t/release-chr0m3-x-modz-realism-1-0-gta-iv-tbogt-scripts-xbox-360-pc-ps3/2338) | Original author explicitly released Xbox 360 support using common.rpf handling changes and scripts. | This is an old, broader package with reported bugs. Its payload, TU8 compatibility and reuse terms remain unverified. |
| [IVMB Gearbox](https://www.gtagarage.com/mods/show.php?id=26976) | Author describes adjustable gear ratios and engine power through memory editing. | Uses PC DLLs and ScriptHook/ScriptHook.NET; it is not an iOS or 360 drop-in mod. |
| [Zolika1351's IV SDK handling structure](https://github.com/Zolika1351/iv-sdk/blob/dddbd3f54b146bdb3a9852809e48e5cb169af3cd/include/cHandlingDataMgr.h) | PC source describes derived handling values, including individual reverse/forward gear speed values. | A reverse-engineering lead only. PC addresses, structure offsets, calling conventions and byte order must not be reused for TU8. |

The SDK reference is pinned to revision
`dddbd3f54b146bdb3a9852809e48e5cb169af3cd`. No SDK implementation has been copied
into the port. Existing author packages are references, not bundled assets.
An original profile avoids making the first implementation dependent on an
unverified archive or permission to adapt another author's tuning.

Read-only inspection of the current locally supplied 360 game data established:

- `default.dat` requests `HANDLING common:/data/handling.dat` and
  `IDE common:/data/vehicles.ide`.
- `handling.dat` has **125 ordinary records**, each with **37 tokens including
  the handling ID**, plus separate special handling sections.
- The inspected `vehicles.ide` has **127 model records**: **105 car-type models**,
  seven bikes, eight boats, four helicopters and three trains. These are counts
  for this inspected base-game data, not universal counts for every episode.
- Model names and handling IDs differ in places, such as `landstalker` using
  `LANSTALK`. Some models share a handling ID: Feroci variants, Perennial
  variants and three Stockade variants. A change to one shared row affects all
  of its models.
- Exposed parameters include mass, drag, center of mass, drive bias, gear count,
  drive force/inertia, a velocity parameter, brakes, traction and suspension.
  Literal horsepower, individual gear ratios, torque curves and RPM redline are
  not ordinary columns in this file. Drive force must not be labeled horsepower,
  or the velocity parameter treated as measured top speed.

The private research report at
`out/mods-validation/vehicle-research/STOCK_SCHEMA_REPORT.json` preserves the
inspected file digests and model-to-handling mappings. The release must not ship
the user's original game files or private reverse-engineering image.

### 1. Establish the active TU8 data contract

Resolve the original handling file through the runtime's active VFS and title
aliases before installing an override. Confirm whether title updates or a
supported episode redirect that path; do not assume the raw base directory is
always the effective source. Start with the currently supported base-game TU8
configuration and explicitly identify unsupported configurations.

Verify the parser's field types, permitted gear count, flags and derived-value
initialization against TU8 code. Document finite and structural constraints;
separate hard parser limits from narrower limits chosen for our profile. Preserve
the original bytes for comments, special sections, unknown records and fields
that are not changed. Do not rewrite `vehicles.ide` to split shared rows in this
first version.

**Exit gate:** the complete model/handling map resolves, the active file route is
proven, and a no-change generated file produces identical handling input. Stock
behavior and the original file digest remain unchanged.

### 2. Author a versioned profile and supporting metadata

Add `ios/Theft4/Mods/RealisticVehicles/profile.json` and `metadata.plist`.
The profile contains named parameter overrides keyed by handling ID, a schema
version, profile revision and supported data contract. Metadata contains credits
and source links. Bundle our parameter deltas, not a copy of `handling.dat`.

For each tuned model, keep an engineering target record: fictional vehicle,
chosen real-world counterpart or class, year/trim where applicable, source,
confidence, mass, drivetrain, acceleration, top speed, braking and transmission
targets. Use manufacturer material where available. Fictional composite vehicles
need an explicit chosen target, rather than a claim that one counterpart is
objectively exact. Unknown measurements remain unknown rather than invented.

Begin calibration with ten representative models and their verified handling
IDs: Banshee (`BANSHEE`), Comet (`COMET`), Coquette (`COQUETTE`), Sultan (`SULTAN`),
Sultan RS (`SULTANRS`), Taxi (`TAXI`), Police (`POLICE`), Landstalker (`LANSTALK`),
Bus (`BUS`) and Phantom (`PHANTOM`). Expand by vehicle class after those behave
correctly. Car-type utility vehicles such as forklifts require separate targets;
do not force a passenger-car template onto every IDE entry marked `car`.

The first allowlist covers mass/drag/center of mass, drive bias, gear count,
drive force/inertia/velocity parameter, brakes, steering, traction and suspension.
Retain stock model/handling flags, damage multipliers, monetary value, animation
groups and seat offsets initially. Keep bike, boat, aircraft, train and special
sections unchanged. A shared handling row is calibrated and tested for every
model using it.

**Exit gate:** targets and assumptions are recorded; profile IDs resolve to
supported car models; each changed field has a known TU8 meaning. Full car
coverage is not claimed while only the representative subset is tuned.

### 3. Build startup generation and exact-file mounting

Implement `ios/bridge/theft4_vehicle_mod.h` and `.cpp` for parsing, validation and
generation. Reject duplicate IDs, missing target rows, unknown override keys,
non-finite values, wrong token counts and incompatible schemas. Validate
relationships such as suspension lower/upper limits, traction limits and bias
ranges; do not silently clamp invalid profiles. Bound file sizes and record
counts, and emit numbers with a locale-independent format.

Generate an app-private file under
`Caches/Mods/RealisticVehicles/<content-key>/handling.dat`. The key includes the
effective original file digest, relevant model-map digest, profile digest and
generator schema/version. Publish only a completely validated file through a
temporary file and atomic rename. A matching, validated cache can be reused;
missing or evicted cache is regenerated at startup. This is a small text-data
operation, not another ASTC texture conversion sweep.

Extend the existing exact-file device pattern in `theft4_mod_mount.h`, then mount
from `theft4_startup.cpp` after runtime setup and before guest execution. The
candidate base-game canonical path is
`\Device\Harddisk0\Partition1\common\data\handling.dat`; gate implementation on
the active-route verification above. Guard against prefix matches so similarly
named files resolve normally. Test mixed case and aliases installed later by the
title, and coexistence with the independent time-cycle override.

Mod Off bypasses profile preparation and mounting entirely. It performs no
handling-file hashing, cache generation or per-frame checks. The original game
directory is never overwritten. If enabled preparation fails, present a clear
launch error and let the user disable the mod; do not silently launch stock
handling while showing an enabled switch.

**Exit gate:** original and similar-name files are unchanged, only the intended
file is redirected, corrupt/stale caches cannot be published, and independent
fresh Off/On/Off runtimes restore the original route reliably.

### 4. Add the independent launcher toggle

Use the label **Realistic Vehicle Handling**, preference
`Theft4ModRealisticVehicles`, launcher property `realisticVehicles` and
accessibility identifier `mods.realisticVehicles`. Default Off; save independently
of graphics, diagnostics and all other mods. Latch before Play and disable the
control after guest startup, following the current mod lifecycle.

The card explains that the profile changes vehicle performance, grip and
suspension, including traffic using the same handling data. Save and reopen to
change it. Include the profile version and credits area. Describe the calibrated
coverage accurately; do not advertise exact gearing before the transmission
milestone passes. Rotation and iPhone card reflow must retain the same control
and selection.

**Exit gate:** default/persistence/independence/latching tests pass and both phone
orientations and the iPad launcher remain usable.

### 5. Calibrate actual driving behavior

Provide opt-in development measurement, with a bounded in-memory buffer and
output written at capture completion. It should sample the validated current
vehicle at approximately 10 Hz, not scan the world's vehicle pool. Separate
simulation time from wall time so a slow frame rate does not look like weak
acceleration. Disabled capture adds no polling or file writes in retail mode.

TU8's registered `GET_CAR_SPEED` path currently provides a useful lead:
`sub_8259F100` calls `sub_82594CA0`, which resolves a vehicle handle and obtains
velocity magnitude. `GET_CAR_MODEL` uses `sub_8259F4A8` / `sub_82595458`. These
queries are not yet a completed safe telemetry adapter. Verify speed units,
invalid-handle behavior, pool generations and memory bounds before use; never
retain a vehicle pointer across despawn or scene transitions.

Use three matched stock/mod runs per case with the same route, weather, vehicle
condition and graphics settings. Measure acceleration to 100 km/h where feasible,
rolling acceleration, a sustained level-road top-speed plateau, 100–0 braking,
gear transitions and engine audio. Test cornering, weight transfer, bumps,
wet-road traction, reverse, controller partial throttle and damaged vehicles.
Record initial profile targets before adjusting parameters.

Proposed calibration tolerances are median acceleration within 10%, top speed
within 5% and braking distance within 15% of the selected target. These are
engineering acceptance targets, not current results. Repeatability must be good
enough to distinguish a tuning change from route, traffic or frame-rate noise.
Subjective control quality, usable steering and stable collision behavior are
additional requirements, not consequences of meeting a speed number.

**Exit gate:** the ten-model matrix passes on-device and provides a reproducible
process for the remaining car classes. Only then expand and validate coverage.

### 6. Implement exact transmission support where required

Trace the TU8 handling parser/constructor and the vehicle transmission update.
Prove the guest structure, derived gear tables, RPM meaning, shift decisions,
vehicle-to-handling reference, byte order and calling convention. PC SDK offsets
remain unusable until independently established for this executable. The actual
TU8 transmission functions have not yet been identified.

Prefer a native post-initialization hook that sets coherent derived gear values
once per handling record while retaining the original automatic shift controller.
Determine how mechanical ratios, final drive, wheel radius and redline map to
the engine's gear-speed representation; a list of real ratios cannot be written
blindly into velocity fields. Handle reverse, supported gear-count limits and
engine-audio synchronization. Test NPC and scripted drivers as well as the player.

If a torque curve or shift behavior cannot be represented by initialization,
write the smallest necessary native transmission adapter at the verified update
boundary. Give it immutable precomputed profile data and bounded work. Avoid
script interpreters, global vehicle scans, repeated allocation, per-frame data
parsing, artificial velocity injection and changes to the global physics clock.
Retain the existing timestep guard. A torque/RPM adapter needs separate tests
for traction, engine damage, vehicle exit/despawn and mission-controlled drivers.
Manual gearbox controls are a separate feature, not necessary for this toggle.

**Exit gate:** extracted TU8 behavior and valid fixtures establish the layout;
measured gear speeds, shifts, acceleration and audio agree; Off executes the
original path. Do not claim exact transmission support if only gear count and
drive force were changed.

### 7. Integration, performance and release gates

Add host tests under `tests/ios/mods` for the profile parser/generator and real VFS
mounting, alongside launcher lifecycle tests. Cover malformed rows, duplicate and
missing IDs, shared aliases, special-section preservation, locale, cache eviction,
failed publication, future unsupported data and combined time-cycle/vehicle mods.
Transmission tests must exercise the proved adapter, not only a duplicate model
of its intended behavior.

On both M5 and Air, compare fresh Mod Off and Mod On captures on matched routes
with graphs/capture disabled for the retail comparison. Check traffic braking,
police chases, mission timers, scripted vehicle movement, collisions, save/load,
pause/resume and switching the mod Off on the next launch. Changes apply globally
to models using the tuned data, so faster NPCs and harder timed missions are real
compatibility risks to test. Increased travel speed may also increase streaming
work even when the mod itself adds no steady per-frame overhead.

The data-only stage should add no steady gameplay CPU work. Measure any later
transmission adapter against the existing 30 ms engineering frame-time target;
do not promise a frame-rate gain or locked 30 FPS from this handling mod. Retain
the current renderer, texture cache and geometry optimizations.

| Proposed change | Location |
| --- | --- |
| Original deltas, credits and target metadata | `ios/Theft4/Mods/RealisticVehicles/` |
| Validation and atomic generation | `ios/bridge/theft4_vehicle_mod.h`, `.cpp` |
| Exact VFS override and startup integration | `ios/bridge/theft4_mod_mount.h`, `theft4_startup.cpp` |
| Preference, card, launch configuration | `ios/Theft4/main.m`, `Theft4LauncherView.h`, `.m` |
| Resource/build integration | `ios/CMakeLists.txt`, `ios/launcher-preview/CMakeLists.txt` |
| Profile/VFS and launcher tests | `tests/ios/mods/`, existing launcher test suite |
| Optional verified transmission adapter | `glue/rexglue-sdk-main/gta4-recomp/src/gta4_vehicle_mods.h`, `.cpp` |

Implement phases 1–4 without requiring gameplay input. Phase 5 is the first
necessary driving session; prepare an explicit vehicle/route checklist before
asking the tester. Phase 6 follows the observed limitations of the data-only
profile and the independent TU8 layout proof. Release after phase 7, with credits,
measured coverage and remaining limitations documented. No third-party assets
or executable plugins are incorporated merely because their download is public.


### Build 133: curved radar geometry and pause-panel clipping

The build 132 device screenshots still showed an oval radar ring and Stats rows visible beyond the pause panel. Two additional paths were missing from the UI correction:

- The type-4 health/armor command (`821BCFA0` → `821C4148`) draws curved strips directly through the normalized vertex emitter. Retaining its layout without enabling emission correction left those strips stretched while adjacent rectangle pieces were corrected. The entire normalized command now receives one transform, preserving its UVs, colors, and authored shape.
- Pause rows and glyphs can extend beyond the old 16:9 panel; the wider world view makes those formerly hidden areas visible. Native draw submissions now intersect their existing packed scissor with the fixed pause panel. Deferred font commands (`8200138C` / `821BCEE0`) retain the pause context for clipping while already baked glyphs remain exempt from a second position transform.

The temporary scissor is captured at native submission and restored immediately. Its restoration invalidates fixed/dynamic state for the next draw. Existing narrower bounds and signed window offsets are respected. Gameplay, radar, offscreen pause-map targets, and the original 16:9 layout bypass this clipping adapter. No shaders, camera projections, lighting, texture preparation, or geometry budgets change in this build.

Validation exercises the original generated curved-ring emitter on tablet, wide phone, and 16:9 layouts, compares every emitted position against its proportional baseline, and verifies unchanged UVs/colors. Deferred-text tests check panel clipping, existing scissor intersection, restored state, and no double transformation. Isolated pre-fix reproductions fail the curved-ring and font-context assertions; the corrected nine-test mod suite passes. Device visual confirmation remains required for the complete pause list and minimap composition.


### Build 134: cold geometry conversion and upload allocation audit

The first-use geometry path now executes its cached semantic conversion recipe directly. It avoids repeating shader/declaration selection and skips attributes that need no fixup after the whole-buffer endian copy. For word-aligned, complete records, endian conversion and packed-field fixups operate in roughly 4 KB tiles. Cross-record fields keep the original element-major write order. Overlapping fields, odd strides, truncated records, signed/unsigned colors and packed normals retain the original byte results. Unknown layouts retain the declaration-based converter. Warm immutable owners and the two-entry cache plus bounded fallback are unchanged.

Geometry upload pages now append into unused, 256-byte-aligned ranges across frame boundaries. They never rewind a live page or overwrite an earlier view. Geometry up to 256 KB can use the existing recyclable 256 KB Metal pages; larger geometry retains dedicated uploads. Constant-page boundaries, the GPU completion leases, eviction limits and cache budgets are unchanged. Clear explicitly drops both current pages, so a retained tail cannot reference an erased allocation record. The constructor's `compact_geometry=false` option preserves the prior allocation policy for controlled host comparisons.

An Apple M1 Max host fixture converting 128 × 512 KB meshes (64 MiB) measured median float-only conversion at 6.513 ms before and 1.177 ms after, and packed conversion at 6.928 ms before and 5.998 ms after. These are total fixture times, **not saved milliseconds per iPad frame**. Eight alternating trials were used. An additional field-dispatch rewrite performed worse and was reverted before the shipping build.

Four alternating upload trials of 120 frames with six new, mixed-size meshes per frame reduced Metal buffer creation from 480 to 180 and median retained upload-cache storage from 66,846,840 to 47,185,920 bytes. Median per-burst host CPU time changed from 0.0727 to 0.0562 ms. A sparse, small-mesh stream reduced 120 page allocations to one. Sparse-source residency and lifetime affect these ratios; they are not universal scene results or a reason to increase the cache ceiling.

Validation includes 120,000 randomized full-byte and conversion-count comparisons, 15,000 layout suffix comparisons against the live original converter, AddressSanitizer and UndefinedBehaviorSanitizer, 26 native contracts and nine mod contracts. A real Metal test submits 300 reads while later frames append to the same page, then checks every returned byte after eviction, clearing and cache destruction. Existing 768-frame upload pressure, escaped-buffer lifetime, delayed constant-pool ownership and complete rendering regressions pass. All 2,736 shader libraries are byte-identical to build 133.

This audit also verified that clean guest captures, dirty/unlock/alias handling, required-stream selection, immutable allocation sharing, index bounds and pipeline archive preparation already exist. No geometry, LOD, draw distance, projection, shadows, shader math, simulation work or draw completeness is reduced. No background conversion task, extra frames in flight, deferred draw or artificial pacing delay is added.

The target remains **30 ms of critical-path work within a 33.333 ms frame deadline**. The saved build-129 heavy outdoor samples had approximately 33–39 ms GPU envelopes without new pipelines or upload-cache eviction; this change cannot establish that GPU-heavy views now meet the target. A cold start/new-street capture and a warm repeat are still required for build 134. Unseen Metal pipeline compilation and GPU pass cost remain separate first-use/steady-state investigations. Private source snapshots, benchmark reports and diagnosis are preserved under `out/mods-validation/performance-134`; the signed build 133 is retained as the device rollback baseline.

### Build 135: geometry backing reuse and bounded source retirement

The next geometry audit extends final-release VM backing reuse to immutable meshes larger than 256 KB and up to 16 MiB. Allocations round to the host's VM page size, and their actual Metal buffer lengths count against the existing upload-cache budget. The existing **16 MiB aggregate free-backing ceiling** now covers both small pages and large regions. Exact-size reuse avoids padding a mesh to a power of two. Every upload still creates a new Metal buffer identity; only its storage can recycle, after Metal destroys the previous buffer. Pending GPU reads and escaped ARC references therefore prevent reuse. Optional allocation failures keep the previous immutable-buffer fallback.

Allocation retirement now checks expired geometry owners as well as constant owners. It can release a completely dead page without waiting for a rolling hash-bucket sweep to encounter all its entries. The extra ownership inspection is limited to 1,024 probes and 256 allocation visits per sweep. It never drops a live generation or changes a surviving view's bytes.

The source audit also found that direct Metal returned before the Vulkan callback's resource age clock and CPU shadow reclamation. Explicit guest releases remained active, but age-based CPU geometry cleanup did not run. Metal now advances the resource clock for real title presents, preserves it across internal flushes, and checks at most **32 cached handles per title frame**. Only exclusively cached generations unused for more than the existing 600-frame retention interval qualify. Reclamation releases at most eight sources and normally 2 MiB of retained capacity per frame; one oversized source may retire intact so cleanup can progress. Recently used sources and queued commands' retained generations remain valid. Alias and dirty tracking retire together with the discarded capture; a later draw can recapture authoritative guest bytes.

The retirement index has one ticket per cached handle, including replacement generations. Guest release, lifetime invalidation and device reset remove tickets. Numeric continuation survives cache rehash, replacement and removal; no map iterator crosses a producer transaction. The Metal path does not run the Vulkan map-wide scan/sort, enable its diagnostic profiler, trim authoritative GPU-produced textures or change the GPU queue depth. This is incremental age-based CPU cleanup, **not a new hard total CPU-memory ceiling**. Destruction of one oversized source and reacquisition of a long-unused source still need device pacing measurement.

Four alternating Apple M1 Max trials against the preserved build-134 implementation measured a large-mesh burst fixture at median **1.144 → 0.493 ms** and p95 **1.369 → 0.687 ms**. The candidate reused VM backing 395 times after five VM allocations; both implementations still created distinct Metal buffers for the uploads. A fully dead geometry page surrounded by 32,768 live sources retired in **one sweep instead of 50–51**. Live-page sweep means were 0.0154 ms before and 0.0144 ms after. These are controlled host fixture results, not saved milliseconds per phone frame. The separate retirement-index fixture measured roughly 1.6 microseconds for 32 live-handle visits; that number excludes production map lookups, locking and resource destruction.

Validation covers 40 pending large-mesh GPU reads plus an escaped buffer surviving cache destruction, 300 cross-frame append-only GPU reads, 768 pressure frames, transient constant-pool completion leases, the full Metal rendering regression suite, 27 native contracts, nine mod contracts, and AddressSanitizer/UndefinedBehaviorSanitizer retirement checks. All 2,736 shader libraries match build 134. Camera, shadows, aspect correction, LOD, geometry completeness, shader math and texture preparation remain unchanged. Build 134 is retained as the device rollback app. Device tests are still needed to quantify gameplay pacing, including revisiting an area after CPU capture retirement.

### iPhone 13 Pro Max evidence supplied for 0.3

The supplied archive includes a **0.3.0 build-123** capture from `iPhone14,3`, using the `iphone-6gb` profile, and a separate **0.2.1 build-53** export. It also contains older retained traces and an older runtime log. The sessions are analyzed separately; they are not an identical-route A/B, and old Vulkan messages are not attributed to the Oct 7 direct Metal run. Raw captures, provenance and analysis remain in the private `out/mods-validation/performance-135/iphone13pro-max-0.3` directory.

The build-123 capture has Enhanced 1080p On and FSR, anisotropic filtering, motion blur and depth of field Off. Its header does not identify antialiasing or the active texture compatibility setting. Using the explicit heuristic `commands > 100` selects 1,297 draw-heavy publications:

| Measurement | Median | p95 | Maximum |
| --- | ---: | ---: | ---: |
| Renderer wall span | 35.38 ms | 51.15 ms | 199.98 ms |
| Frontend recording/lowering wall span | 24.55 ms | 36.26 ms | 177.79 ms |
| Backend submission wall span, including admission/waits | 10.05 ms | 14.11 ms | 86.63 ms |
| Renderer-thread CPU, 87 valid sampled publications | 35.20 ms | 48.32 ms | 59.40 ms |
| Publication interval | 51.46 ms | 72.23 ms | 233.98 ms |

Publication IDs join to renderer completion timestamps within 0.005 ms. Publication intervals are still not scanout times, renderer CPU excludes simulation, and category percentiles must not be added. The exported retail development counters can be zero when unavailable: they do not establish zero shader compilations, cache misses or uploads. No GPU durations or thermal-state measurements are present. The large frontend outlier is a reason to investigate first-use lowering and scheduling, not proof of a particular conversion or allocation cause.

The 22 valid draw-heavy memory samples peak at approximately **1,627 MiB footprint**, with no recorded memory warning and a minimum reported available memory of approximately **2,469 MiB**. They do not show OS memory exhaustion, but cannot rule out a smaller upload-cache working-set bottleneck or page-fault stalls. The bundled preparation state reports 49,867 textures complete and approximately 2.39 GiB of ASTC payload; it does not prove the active run used every cached entry or that its current source fingerprint matched. There is no basis here for automatically enlarging cache ceilings, blaming an unfinished preparation sweep, or disabling visual features by device name.

These logs provide concrete CPU-side deadline failures on this phone era. Builds 134–135 reduce shared geometry conversion/allocation overhead and improve retirement without adding an iPhone-only visual downgrade. Existing capability-based BC/ASTC handling and lower-memory upload budgets remain. A matched build-135 phone capture, with the same settings and route followed by a warm repeat and later revisit, is required before reporting a phone FPS gain or compliance with the 30 ms work target.

Further analysis of the same capture adds three useful distinctions. In the 87 valid draw-heavy renderer CPU samples, the median thread CPU/wall ratio is approximately 1.00, and the median system-CPU fraction is approximately 0.338. The CPU readings come from Mach `THREAD_BASIC_INFO`; this is evidence of active renderer CPU work in those sampled spans, rather than predominantly sleeping on the GPU. It does not explain the worst unsampled hitch or eliminate simultaneous GPU bottlenecks. Kernel accounting does not identify the responsible API, but supports investigation of allocation, driver submission and VM handling alongside frontend lowering.

The scheduling capture repeats identical 16,384-iteration register and buffer workloads. On the native renderer, first- versus last-quarter median register CPU time rises from approximately 33.7 to 49.0 microseconds, and buffer CPU time from 17.8 to 23.0 microseconds. The guest present producer shows the same direction. Renderer QoS remains elevated in the periodic observations, and the capture reports no dropped scheduling records. This is a sustained CPU-throughput clue independent of the changing draw list, not proof of thermal throttling: thermal state, hardware core placement and CPU frequencies were not captured. Increasing priority again or changing geometry quality cannot alone account for slower identical register work.

Process-wide counters between sampled frames 300 and 1560 add 213,529 VM faults, 258 page-ins and 2,505 decompressions. The growth supports studying memory churn and first-touch/reuse costs, but includes all game threads and cannot attribute faults to geometry, textures or a specific cache. Large call counts are also not automatically an optimization target: the audio-thread guest fence poll runs about 12,345 times per second but sampled calls average approximately 0.084 microseconds. Nested/wait wall samples overlap and include descheduling. These observations prioritize substantial renderer CPU and VM/driver work over speculative removal of synchronization or small clock wrappers. The private correlation report preserves the cohorts, fixed-work samples and interpretation limits.

### Build 136: less draw preparation and admission allocation

Pass admission now uses bounded local lists for four color attachments plus depth/stencil, and up to twelve attachment/resolve views. It preserves attachment order, exact aspect/subresource identity, duplicate rejection, resolve hazards, defined-content tracking and transactional output. The declaration map and persistent content sets remain; this removes the three per-pass heap containers rather than weakening admission. In a 240-pass, six-attachment resolve fixture, allocations per admission fall from **5,064 to 24**, with the remaining allocations unchanged between one and 240 passes. Host mean admission time is approximately 0.264 to 0.105 ms in that fixture.

Draw preparation borrows temporary Metal texture and sampler references from the worker-owned caches instead of retaining each fetch into a temporary ARC array and then retaining it again into the draw. Completed draw bindings remain strong owners. Encoded Metal command buffers still retain their own resources through GPU completion. Typed dummy images, fetch reflection, samplers, shaders and binding indices remain identical.

Only the private frame preparation path can also use a **512-entry, four-probe texture lookup memo** after full admission of the same immutable frame. It identifies both the exact image pointer and shared ownership, calls the complete image version/shape check on first use, and clears occupied slots at every submission exit. Public `Prepare` and `ImageFor` retain independent strict checks. Collision or overflow falls back to the ordinary lookup; it never drops a draw, grows without limit or borrows a resource past cache ownership. Reopening the shader catalog clears submission views before clearing image entries. The existing CPU/GPU immutable-owner contract is unchanged.

Four alternating Apple M1 Max comparisons against the preserved build-135 implementation measure public preparation of 4,096 synthetic textured draws at median **1.937 to 1.585 ms** with repeated samplers, and **1.987 to 1.636 ms** with mixed samplers. A separate ordered fixture with 4,500 draws, 180 passes and 384 texture owners measures median submission wall time at **6.039 to 5.426 ms** with retail policy enabled. Submission includes admission, realization, driver encoding and any queue backpressure; it is not a pure per-draw CPU timer. These synthetic fixture reductions are **not saved milliseconds per iPad game frame or proof of a locked 30 FPS**. The private baseline uses the original implementation, with one unused argument added solely for compatibility with the new private method declaration. Source snapshots and all trial results remain under `out/mods-validation/performance-136`.

A consecutive sampler lookup experiment improved repeated keys but slowed alternating keys by roughly 6%; it was removed. The shipping sampler cache and exact filtering/address/LOD key stay unchanged.

Validation includes 60,000 comparisons against build-135 admission, exact accept/reject errors and final contents, unchanged failure outputs, AddressSanitizer/UndefinedBehaviorSanitizer, 28 native contracts and nine mod contracts. Real Metal checks cover 1,152 image owners exceeding memo capacity, aliasing control blocks, 2,304 ordered texture draws per round across three rounds, failed-submission recovery, reopening, and GPU reads after immediate CPU-owner release and retirement. Escaped strong draws and encoded packets survive adapter destruction and produce identical pixels. The complete Metal worker regression suite passes, the iOS Release app builds, and all 2,736 shader libraries are byte-identical to build 135.

Geometry bytes, LOD, draw distance, draw order, camera/aspect correction, shadows, HUD, texture compatibility/preparation, shader math, queue depth and upload cache ceilings remain unchanged. No new per-draw retail diagnostic counters or developer logging are added. The work target remains **30 ms within the 33.333 ms deadline**. Device cold-start, heavy-scene and warm-repeat captures are still required to measure actual pacing and first-use gains. The signed build-135 app is retained for rollback.

### Build 137: reuse frame bookkeeping and avoid redundant driver work

The ordered game worker computes redundant attachment loads once and shares that result with store analysis. Empty produced-texture packets bypass the 26-slot walk. Attachment-analysis masks and validated draw ranges reuse worker-owned CPU storage; range entries are cleared on every submission exit and every admission failure. The existing public admission API retains transactional output. Full-frame admission still precedes resource realization and encoding.

Attachment analysis uses a **64 KiB reusable arena with exact-size node recycling**. Its short-lived maps contain numeric surface identities and pass indices only. Arena overflow uses ordinary allocation and frees each overflow node when the analysis ends; it does not become a growing retained cache. A standard-library pool experiment was rejected after allocation tracing showed the system implementation forwarding every map-node allocation upstream. The explicit recycler reuses nodes retired by reads and overwrites during the same analysis. In a warm fixture with 240 passes, six attachments and 4,800 draws, analysis heap allocations fall from **1,449 to zero**, and host mean analysis time is approximately **0.106 to 0.038 ms**. Draw admission allocations fall from **32 to 18**; the remaining surface/content tracking allocations are unchanged. This is CPU bookkeeping reuse, not a change to texture or geometry upload budgets.

The game adapter creates a private render-pass descriptor, fully configures it, then transfers strong ownership to the encoder wrapper. That private path avoids a second descriptor copy. Public `BeginPass` still copies its caller's descriptor, including when a caller changes attachments or store actions immediately afterward. No descriptor is shared between concurrent encoders or recycled while encoding.

Metal buffer-extent and texture-shape caches retain their **64-entry ceilings**. Primary hits use the previous direct lookup; only collisions search up to four slots, with bounded round-robin replacement. Exact object identity admits every hit, strong owners prevent pointer recycling, and replacement does not affect encoded GPU ownership. A miss replaces all traits and ownership directly instead of clearing and assigning ARC ownership twice. Each draw still validates its own buffer offsets, lengths, required bytes and expected texture type. A real Metal fixture cycling four objects that collide in the previous cache reduces both buffer-extent queries and texture-shape queries from **1,024 to four**, with identical pixels and unchanged rejection of invalid cached ranges/types. Existing diagnostic counters remain compiled out of the retail encoding specialization.

Four alternating retail comparisons against preserved build-136 source, using 4,500 ordered draws, 180 passes and 384 texture owners on Apple M1 Max, measure median submission wall time **4.784 to 4.653 ms**, and submitting-thread CPU time **4.692 to 4.624 ms**. These small differences are within observed host variation. They do **not** establish a net gameplay FPS gain, saved iPad frame milliseconds, or compliance with the 30 ms work target. CPU timing uses Mach thread accounting around submission; wall spans can additionally include scheduling and queue admission. Allocation counts and the deliberately colliding driver-query reductions are deterministic. Earlier cache/hash experiments and all source snapshots remain in private `out/mods-validation/performance-137`; only the final primary-hit implementation is packaged.

Validation compares admission decisions, error text, final contents, failure outputs and both attachment masks against build 136 in **60,000 cases**. The scratch tests also cover 4,800 draw ranges, failed admission cleanup, capacity reuse, and arena overflow with 2,048 distinct surfaces. AddressSanitizer/UndefinedBehaviorSanitizer cover admission/analysis and trait-cache ownership replacement. Native contracts, the complete Metal worker regression, real Metal descriptor isolation, and draw/GPU ownership tests cover cache overflow, reopening, failed-submission recovery and pending GPU reads after CPU owners are released. Shader libraries and visual settings remain unchanged. Build 136 is retained for rollback; a matched device capture is still needed to assess first-use spikes and heavy-scene pacing.

### Build 138: immediate curved minimap geometry

The previous radar correction covered queued type-4 health/armor commands. The title also calls the same curved primitive directly from the HUD compositor. That immediate route had an active UI layout but no emission transform, so its curved vertices could retain stretched 16:9 coordinates while neighboring rectangles were corrected. A new test using the original generated primitive reproduces the omission in build 137: an emitted Y of **0.800000012** should be **0.836713139** in the tablet's bottom-anchored proportional layout.

The correction now also wraps the shared normalized primitive (`sub_821C4148`). Its emission scope preserves any already active queued transform, so queued vertices are still mapped exactly once. An unscoped world/offscreen call keeps its original coordinates. No camera, shadow, shader, upload-cache, draw-distance or 3D viewport change is included; all build-137 CPU improvements remain.

Validation compares every emitted position against its original 16:9 reference under tablet, wide-phone and original layouts, with byte-identical UVs/colors and unchanged vertex counts. It tests both immediate and queued routes, context restoration and non-UI exclusion, alongside existing menu/clipping/queued-font contracts. The nine mod tests and the UI AddressSanitizer/UndefinedBehaviorSanitizer run pass. The iOS Release build succeeds. Source snapshots, the failing pre-fix reproduction and corrected test results remain in private `out/mods-validation/ui-aspect-138`. Full minimap composition still requires device visual confirmation; these CPU checks isolate the missed curved-geometry route rather than proving every live layer is correct.

### Build 139: radar viewport, shader metadata and upload allocations

Device feedback confirmed that build 138 did **not** fix the complete minimap. Tracing the TU8 compositor showed the missing distinction: the radar has its own orthographic subviewport. Its map tiles, stencil mask, circular border, health segments and blips use local normalized coordinates. Transforming selected inner vertices as though they were screen coordinates leaves other layers stretched and can apply incompatible corrections.

The radar pass constructor (`sub_8239C9B8`) now corrects the canonical viewport at pass offset 176 through the original window setter (`sub_828BE238`). That setter rebuilds clipped/unclipped bounds and derived viewport data together. The radar compositor and tile producer carry an explicit local identity layout through deferred playback. Local geometry, UVs, depth, colors and draw counts remain untouched; the complete subviewport supplies proportional sizing. Full pause-map and offscreen views are excluded. Invalid guest ranges and extreme/nonfinite pixel boundaries are rejected before address or integer conversion. Normal display changes invalidate the published bounds.

Minimap touch detection now uses the same canonical viewport bounds. This removes the old per-tile coordinate scan and avoids estimating screen coordinates from local map vertices. The existing pause transaction, gesture handling and frontend touch behavior remain in place.

The CPU regression fails with the preserved build-138 viewport adapter (tablet top `0.690047979` versus required `0.747002398`) and passes with build 139. It covers tablet, wide-phone and original 16:9 layouts; rebuilding does not compound the correction. Extracted original window/map/circle/ring emitters preserve local vertex bytes. Tests also cover deferred playback on another thread, display-generation invalidation, full pause-map exclusion, offscreen targets and corrupt inputs. The compositor/pass builder and GPU operations are test doubles: this is **not** proof of correct full-game pixels. Nine mod contracts and UI AddressSanitizer/UndefinedBehaviorSanitizer pass; live minimap confirmation remains required.

Immutable pipeline templates now retain merged shader constant coverage and both bank extents. Each draw consumes that metadata instead of remerging the masks, finding the last register and rescanning for empty banks. Feature-disable and cache-validation switches still request full banks. Version ownership, constant payloads, shader ABI and projection invalidation are unchanged. Ten thousand differential layout cases cover absent/unknown fragment reflection, empty banks and every register boundary; the existing 8,000 constant-state/coverage replays and sanitizers pass.

Texture upload decoding uses a fixed 2,048-bit stack coverage table and reserves the final mip table once. Cold pipeline decoding also reserves its validated attribute count once. Upload validation, pitch/offset calculations, duplicate/missing-subresource rejection and transactional failure outputs are preserved. In a synthetic host test decoding 1,000 BC3 cube descriptions (12 mip levels, six faces), allocations fall from **9,000 to 1,000** with an identical mip-data checksum. Five trials measure median total decoder time **1.612 to 1.190 ms**; these totals describe 1,000 metadata decodes, **not** saved milliseconds per game frame. Real GPU upload, storage I/O and gameplay FPS are not measured by that test.

The supplied IV artwork replaces the master and all 13 iPhone/iPad/marketing icon sizes. Every supplied pixel was already opaque, so dropping its redundant alpha channel does not composite or change the artwork. Both compiled iPhone and iPad icons match the resized inputs exactly. All 2,736 Metal libraries remain byte-identical to build 138. The iOS Release build and compiler optimization verification pass. Build 138 is retained for rollback. Private evidence is under `out/mods-validation/ui-aspect-139`, `performance-139` and `app-icon-139`; no user capture or game asset is published.

### Additional M4 capture findings and limits

The supplied M4 iPad Pro capture contains three build-123 sessions, with duplicate sections deduplicated by content. It predates the later geometry/allocation changes. In the latest session, the draw-heavy workload heuristic (`commands > 100`) has median renderer wall time **13.122 ms**, with median lowering **6.241 ms** and backend **6.103 ms**. Sampled renderer CPU time nearly matches wall time, so reducing CPU work per draw remains useful. Some heavy frames publish after roughly 52–53 ms while their measured renderer takes about 28–29 ms. The remaining interval includes game/command delivery and intentional limiter waits; it cannot be labeled pure simulation time or repaired by assuming a larger texture cache is needed.

An isolated renderer frame takes 42.514 ms, including 37.358 ms in backend submission, then recovers. There are no corresponding live GPU durations or reliable enabled allocation/PSO probes, so the log does not identify its precise driver, upload, scheduling or GPU cause. The latest session peaks around 2,328 MiB with no memory warnings and about 2,792 MiB minimum reported availability; this does not support cache exhaustion. Small fixed-work CPU witnesses slow by roughly 37–39% over the run despite nominal thermal state. That is a throughput/scheduling clue, not proof of thermal throttling. Routes/settings differ between sessions, so they are not a controlled resolution comparison. Current-build matched cold/warm gameplay captures remain necessary to judge the **30 ms** work target, initial spikes, sustained performance and the next architectural change.


### Build 140: preserve warm geometry recipes at cache pressure

The CPU Metal frontend previously discarded all pipeline templates when a new variant arrived at the 4,096-entry ceiling. It now evicts only the least recently used template, retaining the same ceiling, exact key/hash/equality and constant-layout metadata. A warm hit changes linked recency pointers without allocating a node. Draws copy their pipeline descriptors and retain conversion recipe owners before eviction; the independent conversion-recipe cache keeps geometry identities stable. Device resets still clear the cache explicitly. This changes retention policy, not pipeline state, shader bindings, LOD, draw distance, ordering or scene contents.

A pressure fixture using the production key, hash and incremental cache introduces 175 variants per turn alongside a 1,024-template warm set for 25 turns. Descriptor and retained-owner checks match the reference. Template constructions fall from **10,519 to 8,471** in this synthetic workload; 4,375 individual evictions preserve the warm set. This does **not** prove that the device capture reached the ceiling. A separate warm-only lookup microbenchmark measures 94.76 versus 95.84 ns per lookup on the host, including candidate recency updates; these timings do not measure game frame savings and are within ordinary host variation.

Upload VM allocation and overflow deallocation now run outside the reusable backing pool mutex. Exact-size reuse, the 16 MiB free-backing ceiling and Metal/GPU ownership requirements remain unchanged. Only collection/accounting changes remain locked. Deterministic slow-VM tests demonstrate that unrelated retirement can complete during an allocation and that a cache hit can complete during overflow deallocation. Eight concurrent threads perform 8,000 acquire/release cycles with unique live identities and bounded accounting. AddressSanitizer and UndefinedBehaviorSanitizer pass for this extracted production pool and the pipeline pressure fixture. GPU pressure checks preserve 40 pending large reads plus escaped ownership; the streaming fixture preserves 300 cross-frame reads after CPU cache destruction. The pressure fixture reuses 395 VM regions with five allocations over 80 bursts; those counts describe the existing pool plus the new lock scope, not a new allocation reduction relative to build 139.

A deferred Metal binary-archive experiment was evaluated and **excluded**. Pixel readback, descriptor isolation, persisted archive hits and corrupt-archive fallback passed, but the repeated host comparison did not demonstrate lower first-use pipeline cost. Moving optional recording to a later flush changes where work happens without proving a net benefit; build 140 retains build 139's archive implementation. The experiment, source snapshots and individual comparisons remain private under `out/mods-validation/geometry-turn-140`.

Both freshly retrieved M5 captures identify **build 139**, with optional development probes off. The longer capture has 1,850 renderer records over 64.63 seconds. For the workload heuristic of more than 100 commands, renderer wall time is 15.82 ms median, 21.51 ms p95 and 28.94 ms maximum. Publication intervals are 33.78 ms median, 42.44 ms p95 and 63.98 ms maximum. The worst renderer frame has 13.54 ms lowering and 14.61 ms backend wall time. Renderer timing alone therefore does not account for the entire missed frame interval. Gaps include intentional pacing, simulation and command delivery; backend spans can include waits, and publication timestamps are not display scanout/GPU durations.

The longer run reports no sampled memory warnings, about 1,979 MiB peak footprint and at least 3,141 MiB available memory. Two small fixed-work CPU witnesses become approximately 50–58% slower from their first to last quarters despite nominal thermal state at the recorded context. This supports further investigation of changing CPU throughput/scheduling, not a claim of thermal throttling. Cache and compiler counters are disabled, so zeros do not establish absence of misses or uploads. Runtime wait counters show frequent multi-object waits, but sampled wall spans include descheduling and overlapping wrappers; they cannot be counted as CPU execution or directly assigned to a renderer gap. Build 140 does not change the runtime wait policy.

The final 32 native contracts, complete real-Metal worker lifecycle test, iOS Release build and optimization verification pass. All 2,736 compiled Metal libraries remain byte-identical to build 139, and that signed build remains available for rollback. A matched build-140 device route is still required to determine spike reduction and compliance with the **30 ms total work target**. No locked-30-FPS or full-game visual-parity result is claimed from host fixtures.


### Build 141: original icon and CPU wait/draw preparation

The original pre-139 icon master and all fourteen asset-set files are restored byte-for-byte from the retained pre-switch assets. This replaces the supplied artwork without regenerating the original assets.

The aligned M5 build-140 city-view capture contains roughly 4,210 commands per frame in its ending five seconds. Publication intervals are 37.80 ms median, while renderer work is 18.73 ms median (9.04 ms lowering and 8.56 ms backend). That difference includes pacing, simulation and command delivery; it is not a measured simulation CPU span. The transient near 20 seconds reaches 76.96 ms publication and 41.78 ms renderer wall time, alongside slower fixed-work CPU witnesses. These records justify reducing repeated CPU work, but do not identify a cache overflow, distant-object LOD error or GPU cause.

The Darwin legacy multi-object wait previously allocated its lock vector on every polling iteration and truncated its final sub-millisecond sleep to zero. Build 141 retains the vector capacity for the duration of the wait and sleeps until the lesser of its absolute deadline and next one-millisecond poll. Finite waits also check their deadline when mutex admission fails, preventing contention from extending a timeout indefinitely. Signal selection remains in caller order; wait-all only consumes after every object is ready; manual-reset and semaphore behavior are unchanged. The broader corrected/event-subscription wait mode remains governed by its existing policy and is not enabled by this change. The path still polls; sleeping the final remainder replaces busy checking, and the operating system can wake it slightly after the requested deadline.

A differential fixture compiles the exact production wait class and real event/semaphore implementations. It checks wait-any/all, failed wait-all consumption, manual reset, zero/finite/infinite waits, delayed signals and contested deadlines. In a retained host comparison of 200 four-millisecond waits, the old implementation performed 795,922 state checks and used 41.57 ms of thread CPU; the candidate performed 1,770 checks and used 9.16 ms. Wall totals were 867.17 and 874.33 ms respectively. These are synthetic wait costs, not milliseconds saved per gameplay frame. Address/undefined sanitizer validation passes.

The Metal plan adapter reuses the two exact shader metadata records already resolved by its successful PipelineFor call. Consecutive hits carry their associated records, ordinary hits resolve them with their source shader variants, and catalog Open invalidates the pointers. Preparation no longer hashes the same two shader keys again for every draw. Binding construction, resource admission, constant ranges, late-alpha and clip-space variants remain intact. The targeted GPU fixture alternates textured, untextured and depth-only pipelines, rejected shaders and catalog reloads, checking binding shape and identical textured pixel output. Across four host runs, the median of repeated-draw batch medians changed from 1.627 to 1.576 ms, and mixed-draw medians from 1.660 to 1.612 ms for 4,096 synthetic draws. The first pair did not improve; the effect is small and host timing varies. No device FPS gain is inferred from these numbers.

The broader Metal validation exposed an old test assumption that distinct immutable upload versions require distinct buffer objects. Compact geometry deliberately appends non-overlapping views within one buffer. The assertion now checks disjoint ranges and different preserved payloads, retaining the owner-replacement accounting checks. This changes the validation fixture, not upload behavior. All 45 graphics cases and 33 host contracts pass; the iOS Release build passes optimization verification. No geometry, draw-distance, shading or LOD reduction is introduced. A matched build-141 route is required to establish gameplay benefit and the 30 ms total-work target.
