# Install a sideloaded Theft4 IPA

These instructions are for the unsigned ARM64 IPA attached to the Theft4 0.2.0
GitHub release. The IPA contains the application and statically recompiled game code,
but it does **not** contain Grand Theft Auto IV game data or a title update.

## Requirements

- An ARM64 iPhone or iPad running iOS/iPadOS 26.0 or later.
- AltStore, SideStore, or another IPA installer that can sign the app with your
  Apple account. The release IPA is deliberately unsigned.
- Approximately 7 GB of free device storage for a prepared base-game install,
  in addition to the app and any space required by the sideloading tool.
- Your own legally obtained supported Xbox 360 game and title update:
  - title ID `545407F2`;
  - USA retail media ID `6AC07221`;
  - base executable version `0.0.0.5`;
  - GTA IV title update 8, applying `0.0.0.5` to `0.0.8.5`;
  - supported `default.xexp` SHA-256
    `480aee5e2b42707791e7571bb8407c5bb3f6c7534f07f9beb426db4cfc648fd3`.

An ISO that works in an emulator is not necessarily this revision. Do not
rename an incompatible update or bypass the validation checks.

## 1. Sideload the IPA

1. On the release page, download the file ending in `ios-arm64.ipa`. Do not
   download GitHub's **Source code** archives as a substitute for the IPA.
2. Import the IPA into AltStore, SideStore, or your preferred compatible
   sideloading tool and let that tool sign it with your Apple account.
3. Install Theft4 on the destination iPhone or iPad. Version 0.2.0 uses the
   original `com.theft4.bringup` bundle identifier. Update the existing app
   in place to retain its game files and saves. Do not delete the old app.
   The separate `com.theft4.m5lab` app is not the update target.
4. Open Theft4 once, then close it. First launch creates the shared transfer
   location at **Files → Browse → On My iPhone/iPad → Theft4 → game** and the
   file `COPY GAME FILES HERE.txt` beside it.

Free Apple-account signing normally expires and must be refreshed on the
sideloading tool's schedule. That behavior is controlled by the signing tool,
not Theft4.

## 2. Prepare the game folder on a computer

The iOS app cannot use a raw `.iso` or an unopened title-update package. It
expects the output of Theft4's validated staging process. The staging process:

- verifies the exact USA retail base and matching TU8 pair;
- copies `default.xex` from the base game;
- extracts the validated patch as the sibling file `default.xexp`;
- preserves the base archives and extracts their required loose-file trees;
- adds the required `aes_key.bin` and `.install-manifest`.

Developers building from this repository can create that folder with
`theft4_stage`. The destination must not already exist:

```sh
theft4_stage /absolute/path/to/your-game.iso \
  /absolute/path/to/your-tu8-package \
  /absolute/path/to/NEW-staging-directory
```

After the command reports `Staging VERIFIED`, the folder to transfer is
`NEW-staging-directory/game`. See [the iOS application build notes](IOS_APP_BUILD.md#real-game-staging-and-loader-bring-up)
for the current developer build and simulator invocation. Keep the original ISO
and update package as backups; staging does not modify them.

## 3. Check the exact folder structure

Copy the **contents** of the prepared `game` directory, not the directory
itself. Before transfer, its top level should resemble:

```text
game/
├── default.xex          # USA retail base, version 0.0.0.5
├── default.xexp         # matching TU8 patch, target version 0.0.8.5
├── aes_key.bin
├── .install-manifest
├── common.rpf
├── xbox360.rpf
├── audio.rpf
├── common/
├── xbox360/
├── audio/
└── update/              # present when staging an STFS/SVOD TU package
```

`default.xex` and `default.xexp` must be siblings at the root of `game`.
Do not put `default.xexp` only inside `update/`. If the title update was supplied
to the staging tool as a raw `default.xexp`, no `update/` directory is required.
Do not add a second nesting level such as `game/game/default.xex`.

## 4. Copy the files to the device

Choose either method and wait for the copy to finish completely:

- **On the device:** open **Files → Browse → On My iPhone/iPad → Theft4 →
  game**, then copy everything *inside* the prepared computer-side `game`
  folder into this folder.
- **From a Mac:** connect the device, open **Finder → your device → Files →
  Theft4**, open `game`, and drag everything *inside* the prepared `game` folder
  into it.

The correct final paths are:

```text
On My iPhone/iPad/
└── Theft4/
    ├── COPY GAME FILES HERE.txt
    └── game/
        ├── default.xex
        ├── default.xexp
        ├── aes_key.bin
        ├── .install-manifest
        ├── common.rpf
        ├── xbox360.rpf
        ├── audio.rpf
        ├── common/
        ├── xbox360/
        ├── audio/
        └── update/      # only when produced by staging
```

Do not copy the ISO, the unopened title-update package, the outer staging
directory, or a folder named `game` into the device's existing `game` folder.

## 5. Verify and start

1. Reopen Theft4 after the transfer completes.
2. Open **System** and select **Verify Game Files**. Resolve any reported base or
   title-update mismatch before continuing.
3. Return to **Play** and start the game. A physical controller works whether
   touch controls are enabled or disabled.

Theft4 stores runtime settings, caches, and saves in its private app container,
separate from the shared `Documents/game` folder. An in-place update keeps this
container. Deleting the app removes it, including saves.

## 0.2.0 graphics and diagnostics

- **Graphics** selects 540p, 720p, 900p or 1080p internal resolution, FSR,
  shadows, draw distance, model detail, reflections, anti-aliasing, filtering
  and motion blur. **Apply Performance Preset** starts from 540p + FSR and
  conservative quality settings. Changes apply at the next game launch.
- **Interface** has the frame counter, a frame-time graph and touch controls.
  The graph shows frame publication intervals against a 33.3 ms target.
- For a low-FPS report, open **System** before starting the game and enable
  **Detailed Performance Capture**. In the slow scene, double-tap the frame-time
  graph and allow 600 frames to finish. Quit and relaunch, then tap
  **Download Latest Log Capture**.
  Save or share the dated text bundle from **Files → On My iPhone/iPad → Theft4
  → Diagnostics**. Include the device, route, graphics settings, play duration
  and whether the device felt hot. Profiling adds overhead; also describe an
  ordinary run with capture off.

TestFlight testers install the signed 0.2.0 build from TestFlight, then follow
the same game-folder and diagnostics steps. Do not sideload the unsigned GitHub
IPA over a TestFlight install unless you deliberately manage signing with the
same bundle identifier and preserve the existing app container.

## Common installation failures

| Message or symptom | Cause and fix |
|---|---|
| `default.xex` is missing | The prepared folder was nested as `game/game`. Move the inner folder's contents up one level. |
| `default.xexp` is missing | The TU package itself was copied instead of being staged. Run the validated staging process and copy its sibling `default.xexp`. |
| TU8 or patch mismatch | The update is for another base revision or region. Use the exact `0.0.0.5 → 0.0.8.5` update described above. |
| App still shows no game after copying | The transfer may still be running or may have gone to iCloud Drive instead of **On My iPhone/iPad → Theft4 → game**. |
| Files disappear after reinstalling | Uninstalling an iOS app removes its container. Update in place where possible and keep separate backups of game data and saves. |
