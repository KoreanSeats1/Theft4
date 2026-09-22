# Theft4 0.1.3(a)

Release date: September 17, 2026
Apple version: 0.1.3 (build 6)
Git tag: `v0.1.3a`

## What changed

This update brings the reusable parts of the A19 optimization work into the
normal Theft4 app. Common render commands now avoid a separate allocation,
fixed-state validation is cheaper when enabled, the render worker performs less
object movement, repeated texture-binding preparation can be reused safely, and
disabled diagnostics no longer perform their expensive setup work.

Production runs no longer start detailed CPU/GPU profiling automatically. On
A19-family iPhones, Theft4 also disables speculative pipeline prewarm at launch
to reduce render-worker backlog; authoritative pipeline creation is unchanged.
Other devices retain the established prewarm behavior. The public executable is
compiled for general ARM64 rather than for one Apple chip scheduler.

The launcher, game-file validation, 720p internal rendering, 1080p FSR1 quality
output, motion blur, 4× texture filtering, audio path, two frame-resource slots,
controller/touch input and save locations remain unchanged.

## What is not included

Theft4 Lab remains separate. This release does not merge its experimental
pipeline deferral, draw-path changes, shader probes or unresolved GPU-lifetime
work. It also does not use the A19-only compiler scheduling preset.

## Validation and expectations

The promoted data structures and memoization pass the focused host suite under
AddressSanitizer and UndefinedBehaviorSanitizer. The isolated A19 suite also
passed 55 cases and 2,263,316 assertions. The first combined A19 device pass,
however, suffered render-worker backlog and ran around 10–16 FPS; its revised
candidate was not gameplay-accepted before this shared release was prepared.

Treat 0.1.3(a) as an experimental public update, not a locked-30-FPS claim.
Matched A19 iPhone and M5 iPad gameplay/soak testing is still required.

## Installation

- TestFlight users can update in place from the external testing group.
- GitHub users receive an unsigned IPA and must sign it with AltStore, SideStore
  or another compatible tool.
- Do not uninstall the prior app if you need its imported game files or saves.
- The release contains no retail game data.

See [Install a sideloaded Theft4 IPA](IOS_SIDELOAD_INSTALL.md) for the supported
base/TU8 identities, exact folder layout and transfer troubleshooting.

## Artifact

- IPA: `Theft4-0.1.3a-6-ios-arm64.ipa`
- Architecture: ARM64
- Signing: unsigned; the sideloading tool must re-sign it
- SHA-256: `2c67b0ab051c86cc2ed81d8dd13cb33137f2fa490f34eab02489014061476115`
