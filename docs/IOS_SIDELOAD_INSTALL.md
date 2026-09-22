# Install a sideloaded Theft4 IPA

These instructions are for the unsigned ARM64 IPA attached to a Theft4 GitHub
release. The IPA contains the application and statically recompiled game code,
but it does **not** contain Grand Theft Auto IV game data or a title update.

## Requirements

- An ARM64 iPhone or iPad running iOS/iPadOS 26.0 or later.
- AltStore, SideStore, or another IPA installer that can sign the app with your
  Apple account. The release IPA is deliberately unsigned.
- Approximately 7 GB of free device storage for the extracted base-game install,
  in addition to the app and any space required by the sideloading tool.
- Your own legally obtained supported Xbox 360 game and title update:
  - title ID `545407F2`;
  - USA retail media ID `6AC07221`;
  - base executable version `0.0.0.5`;
  - GTA IV title update 8, applying `0.0.0.5` to `0.0.8.5`;
  - the app validates the extracted `default.xexp` against SHA-256
    `480aee5e2b42707791e7571bb8407c5bb3f6c7534f07f9beb426db4cfc648fd3`.

An ISO that works in an emulator is not necessarily this revision. Do not
rename an incompatible update or bypass the validation checks.

## 1. Sideload the IPA

1. On the release page, download the file ending in `ios-arm64.ipa`. Do not
   download GitHub's **Source code** archives as a substitute for the IPA.
2. Import the IPA into AltStore, SideStore, or your preferred compatible
   sideloading tool and let that tool sign it with your Apple account.
3. Install Theft4 on the destination iPhone or iPad. If an older build is
   already installed, use the same bundle identity if you want the sideloading
   tool to update it in place. Deleting the old app also deletes its app-local
   files and saves.
4. Open Theft4 once. After it creates the shared transfer location, it asks you
   to close the app and copy your extracted base-game files to **Files → Browse
   → On My iPhone/iPad → Theft4 → game**. The file `COPY GAME FILES HERE.txt`
   beside the folder repeats those instructions.

Free Apple-account signing normally expires and must be refreshed on the
sideloading tool's schedule. That behavior is controlled by the signing tool,
not Theft4.

## 2. Prepare the base game on a computer

Extract your legally obtained Xbox 360 GTA IV disc image using your normal lawful
dumping workflow. The iOS app cannot use a raw `.iso`. Its copied `game` folder
must contain `default.xex` at the top level, together with the required game
archives and loose files from that same extraction. Do not add another folder
level such as `game/game/default.xex`.

Keep the raw title-update file separate for now. On the next Theft4 launch, the
app opens a Files picker for it and extracts the supported patch itself. The
picker accepts an Xbox content/STFS title-update package (often named `TU_…`) or
a raw `default.xexp`; it rejects a mismatched region, base version, or patch.

## 3. Check the exact folder structure

Copy the **contents** of the extracted base-game directory, not the directory
itself. Before transfer, its top level should resemble:

```text
game/
├── default.xex          # USA retail base, version 0.0.0.5
├── common.rpf
├── xbox360.rpf
├── audio.rpf
├── common/
├── xbox360/
└── audio/
```

The title update is selected in Theft4 after the base-game transfer. Do not put
the raw `TU_…` file in `game` manually.

## 4. Copy the files to the device

Choose either method and wait for the copy to finish completely:

- **On the device:** open **Files → Browse → On My iPhone/iPad → Theft4 →
  game**, then copy everything *inside* the extracted computer-side game folder
  into this folder.
- **From a Mac:** connect the device, open **Finder → your device → Files →
  Theft4**, open `game`, and drag everything *inside* the extracted game folder
  into it.

The correct final paths are:

```text
On My iPhone/iPad/
└── Theft4/
    ├── COPY GAME FILES HERE.txt
    └── game/
        ├── default.xex
        ├── common.rpf
        ├── xbox360.rpf
        ├── audio.rpf
        ├── common/
        ├── xbox360/
        └── audio/
```

Do not copy the ISO, the raw title-update package, or a folder named `game` into
the device's existing `game` folder.

## 5. Verify and start

1. Reopen Theft4 after the base-game transfer completes.
2. Select the matching title-update file in the Files picker. Theft4 validates,
   extracts, and installs it before showing **Ready to Play**.
3. Open the main screen, return to **Play**, and start the game. A physical controller works whether
   touch controls are enabled or disabled.

Theft4 stores runtime settings, caches, and saves in its private app container,
separate from the shared `Documents/game` folder. Replacing or deleting the app
can therefore remove saves even if the transferred game folder was backed up.

## Common installation failures

| Message or symptom | Cause and fix |
|---|---|
| `default.xex` is missing | The extracted folder was nested as `game/game`. Move the inner folder's contents up one level. |
| `default.xexp` is missing | Select the matching title update in Theft4's Files picker so the app can extract and install it. |
| TU8 or patch mismatch | The update is for another base revision or region. Use the exact `0.0.0.5 → 0.0.8.5` update described above. |
| App still shows no game after copying | The transfer may still be running or may have gone to iCloud Drive instead of **On My iPhone/iPad → Theft4 → game**. |
| Files disappear after reinstalling | Uninstalling an iOS app removes its container. Update in place where possible and keep separate backups of game data and saves. |
