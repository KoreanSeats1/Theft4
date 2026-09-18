# Theft4 VERSION — iOS release

<!-- Keep the installation section in every iOS release from 0.1.3 onward. -->

## What changed

- Summarize user-visible changes here.

## Install the sideloaded IPA

1. Download the attached `ios-arm64.ipa` file. GitHub's **Source code** ZIP is
   not the IPA.
2. Install it with AltStore, SideStore, or another compatible sideloading tool.
   The IPA is unsigned and the tool must re-sign it with your Apple account.
3. Launch Theft4 once so it creates **Files → Browse → On My iPhone/iPad →
   Theft4 → game**, then close the app.
4. On a computer, prepare your legally obtained supported Xbox 360 USA retail
   game (title ID `545407F2`, media ID `6AC07221`, base `0.0.0.5`) with the
   matching TU8 patch (`0.0.0.5 → 0.0.8.5`). A raw ISO or unopened TU package
   cannot be copied directly to the device.
5. Copy the **contents** of the prepared `game` folder into the device's existing
   Theft4 `game` folder. `default.xex` and `default.xexp` must be directly inside
   it—not under `game/game` and not only inside `update/`.
6. Reopen Theft4, choose **System → Verify Game Files**, then start the game.

Exact staging instructions, the required file tree, title-update hash, Finder
and Files paths, update-in-place warning, and troubleshooting:
https://github.com/KoreanSeats1/Theft4/blob/main/docs/IOS_SIDELOAD_INSTALL.md

Requires ARM64 iOS/iPadOS 26.0 or later. No copyrighted game files, title
updates, saves, signing identity, or provisioning profile are included.

## Download integrity

**IPA SHA-256:** `REPLACE_WITH_RELEASE_HASH`
