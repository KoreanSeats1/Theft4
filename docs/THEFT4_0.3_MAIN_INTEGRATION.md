# Theft4 0.3 main integration record

**0.3.0 (123)** · launcher **0.3** · `com.lukebrosious.theft4`

The user approved source promotion and GitHub publication on 2026-10-06. Main
contains the complete `codex/direct-metal-96` implementation and this release
documentation, published to `KoreanSeats1/Theft4`. The separately authorized
[GitHub release `v0.3`](https://github.com/KoreanSeats1/Theft4/releases/tag/v0.3)
now distributes the audited unsigned build 123 IPA and checksum. TestFlight
publication remains separate.

## What was promoted

The tested implementation is
`295c716ff27d7e22da2888d20d2ed2e941e0d037`. It contains the live direct Metal game
renderer, geometry/constant/cache/lifetime work, capability-gated texture support,
launcher changes and optimized default policy with optional graphs/captures.
The final promotion adds documentation and ignore-rule reconciliation; it does
not change the build 123 implementation.

Local main began at build 94,
`b6b4a823403ff38623ce3a46bab369c858c21251`. Remote main was refreshed before
promotion and publication; its prior revision was
`f5f30b373fc5b397364b81fb840f1d638267c14f`; its restored
README donation block and history are already included. Both are ancestors of
the promoted branch, allowing a fast-forward without rewriting shared history.

The [change manifest](THEFT4_0.3_CHANGE_MANIFEST.json) inventories the development
history and paths. The [changelog](../CHANGELOG.md), [release notes](RELEASE_0.3.md)
and [architecture record](THEFT4_0.3_ARCHITECTURE.md) distinguish the original
upstream foundation, previous Theft4 work and the new renderer's rewrites.

## Existing main work preserved

Fifteen tracked/untracked owner files were backed up byte-for-byte before source
promotion. They include separate logging/cache experiments, tests, source files
and transition-investigation documents. They are excluded from the tested release.

Their recoverable Git stash is named
`pre-0.3-main-owner-experiments-20261006`. An additional exact-file backup, tracked
patch, status and SHA-256 manifest are under the local ignored directory
`out/release-promotions/0.3-main-20261006/`. These local artifacts are not bundled
or published. The promotion leaves the stash available; it does not drop it.

The owner README donation change is already present through remote main. The
`.gitignore` exemptions for the transition investigation and ledger are retained.
The frozen dependency submodule patch state is unchanged. Restoring experiments
later requires reviewing them against the new renderer and running relevant
checks; they must not be silently folded into a build described as tested 123.

## Verification supporting the promotion

- 34 launcher/build tests and 27 CPU contracts passed.
- 45 GPU graphics cases and 32 saved game draw replays passed independently in
  optimized and diagnostic configurations; worker and delayed-GPU upload lifetime
  checks also passed.
- The signed ARM64 Release build has verified `-O3 -DNDEBUG`, both direct Metal
  options On, official identity and private game-asset draw capture Off.
- Strict signature and entitlements were checked; all 2,736 stock/host libraries
  are byte-identical to the preceding tested bundle.
- Build 123 was installed in place and normally launched on the M5. Saved
  preferences confirm migration and removal of the old Retail Mode preference.
- The user reported excellent build 122 gameplay following the final geometry
  pass. Build 123 changes the optional diagnostic controls; no new controlled
  whole-game benchmark is claimed.
- A local unsigned build 123 IPA passed archive, architecture, metadata and
  checksum audits. SHA-256:
  `851d06004a2d52ae0d9f7b7fe88f5ea77ae3e075d0f20e42ca4d2efc0cb0c40b`.

Remaining spikes, focus recovery, dense scenes, long sessions, thermals and
non-M5 performance still require device validation. None is described as
universally solved by source promotion.

## Distribution after source promotion

Both generator and public release helper explicitly select Metal, require offline
game/host manifests and omit private draw capture. The build guide documents the
remaining legacy dependency prerequisites and recipient signing validation.

Follow [the build guide](IOS_RELEASE_BUILD.md) for a public unsigned package,
record the hash of the actual distributed artifact, and verify the intended
sideload/signing flow. Keep game files, updates, saves, prepared user caches,
private draw captures, device logs and development signing material out of it.
The source promotion does not create a GitHub release/tag, upload an IPA or
change TestFlight.

## GitHub source publication

The approved push updates `origin/main` without force-pushing or rewriting shared
history. It publishes the full source history, [verbose changelog](../CHANGELOG.md),
[release notes](RELEASE_0.3.md), [architecture comparison and recomp lessons](THEFT4_0.3_ARCHITECTURE.md),
[change manifest](THEFT4_0.3_CHANGE_MANIFEST.json), build guide and corrected capture
instructions. The old runtime diagrams are replaced with the actual Metal route.

The tested runtime remains the build 123 implementation identified above. This
publication pass changes documentation only. The exact published source revision
is available from GitHub main's commit history; the already installed app retains
its implementation revision in `Theft4SourceRevision`. No app release attachment,
version tag or TestFlight upload is implied by publishing the source.

## GitHub app release

On 2026-10-06, the user separately authorized publication of the downloadable
0.3 release. Tag `v0.3` includes the tested build 123 runtime and the release
documentation; the IPA retains its implementation revision `295c716f`. The
release distributes `Theft4-0.3.0-123-ios-arm64.ipa` and its SHA-256 file.
The IPA hash is
`851d06004a2d52ae0d9f7b7fe88f5ea77ae3e075d0f20e42ca4d2efc0cb0c40b`.

The archive was rechecked for ARM64, version/build, all 2,736 shader libraries,
duplicate entries, ZIP integrity and absence of signing/provisioning material,
game XEX/update files, private draw captures and AppleDouble metadata. Users
must re-sign it using their sideloading tool and update in place to retain data.
The GitHub publication does not imply TestFlight delivery or a universal
locked-30-FPS result.
