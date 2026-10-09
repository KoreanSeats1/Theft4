# Install a sideloaded Theft4 IPA

These instructions are for the unsigned ARM64 IPA attached to the
[Theft4 0.3.1 GitHub release](https://github.com/KoreanSeats1/Theft4/releases/tag/v0.3.1).
The IPA contains the application and statically recompiled game code,
but it does **not** contain Grand Theft Auto IV game data or a title update.

## Copy-and-paste setup prompt for Codex or Claude

Copy the entire block below into Codex or Claude. Use a session with access to
your computer's files and tools if you want the assistant to carry out setup;
a chat-only session can guide you through the manual steps. You can paste it
without editing it: the assistant will ask for your device and input locations.
This is a proposed setup workflow, not a guarantee of automatic installation.
Apple sign-in, signing, device trust and some Files actions may need your input.

```text
Help me install and set up Theft4 on my iPhone or iPad, using the official
release and my own legally obtained game files. Carry out the steps your tools
support, and guide me through any steps that require me. Do not stop at a plan.

Start by reading the current official instructions:
https://github.com/KoreanSeats1/Theft4/blob/main/README.md
https://github.com/KoreanSeats1/Theft4/blob/main/docs/IOS_SIDELOAD_INSTALL.md
https://github.com/KoreanSeats1/Theft4/blob/main/docs/IOS_RELEASE_BUILD.md
Official releases: https://github.com/KoreanSeats1/Theft4/releases

Use the latest stable official release with an iOS ARM64 IPA, not a development
or Lab build. Prefer the published app over compiling the entire game. If a
TestFlight build is already available to me, I can use that instead of
sideloading. Do not assume a GitHub release is available on TestFlight.

First ask only for missing setup information, together: my device model and
iOS/iPadOS version, my computer OS, where my GTA IV ISO/extracted files and
matching title update are located, whether Theft4 is already installed, and
which signing/sideloading tool or TestFlight access I already have. Inspect
only the locations I provide or approve. The current release requires iOS or
iPadOS 26 or later; check the current guide in case that changes.

Then work through this checklist:
1. Check compatibility and storage. Allow about 7 GB for the prepared game,
   plus the app and transfer/staging space; devices needing ASTC preparation
   need about 2.4 GB extra. Keep my original inputs unchanged. Never download
   game data, title updates or keys from unofficial sources, or bypass checks.
2. Download the official release IPA and check its SHA-256 against the published
   checksum when supplied. GitHub Source code ZIPs are not installable apps.
   Use my existing signing method; let me enter Apple credentials and complete
   trust, verification or signing prompts myself. Never ask me to paste a
   password into chat. Explain any new software or account requirements first.
3. Close Theft4 before updating. Update the official com.lukebrosious.theft4
   app in place so game files, saves, settings and prepared caches are retained.
   Do not uninstall it or delete/replace its data. If I have only an old Lab
   app, explain its separate container and help back up its data first.
4. Check whether I already have a verified prepared game folder. Otherwise use
   Theft4's validated staging tool and its documented build instructions, or
   an official Game Preparer if one is actually supplied with the release.
   Never assume that a helper binary exists or that a fresh source build works.
   If a required tool/dependency is unavailable, tell me the exact blocker.
   The supported pair is Xbox 360 USA GTA IV, title ID 545407F2, media ID
   6AC07221, base 0.0.0.5, and TU8 patching to 0.0.8.5. Check the documented
   patch hash and the tool's validation result; filenames alone prove nothing.
   Stage into a new destination. Proceed only after verification succeeds.
5. Open the installed app once to create its Files transfer folder, then close
   it. Copy ALL CONTENTS of the verified computer-side game folder, including
   hidden .install-manifest, into Files > On My iPhone/iPad > Theft4 > game.
   Preserve the supplied layout: default.xex and default.xexp must be siblings,
   with aes_key.bin, the archives and their extracted loose-file directories.
   Do not copy the ISO/unopened update or create game/game. Wait for the entire
   transfer to finish. Use the guide's Files/Finder method; if you cannot control
   the transfer, give me the exact source/destination and wait for me to finish.
6. Reopen Theft4 and use System > Check Game Files. Check the transferred
   installation before asking for another update package. Resolve actual
   validation errors rather than renaming files or disabling validation.
7. If the app requires one-time ASTC texture preparation, let it finish before
   Play. Explain the progress/ETA and extra space, keep the app open, preferably
   on power, and preserve its resumable cache. Detection is by GPU capability,
   not an assumed iPhone age cutoff; BC-capable devices skip this requirement.
   Later launches reuse the results unless cache/game files change.
8. Start with normal release defaults, optional mods and performance capture
   off. Help me enable touch controls or pair my controller. Ask me to tap Play
   and confirm a visible controllable 3D scene and working audio; installation
   alone is not proof of working gameplay. If it fails, collect the exact error
   and guide me through the current Long Performance Capture/export workflow.

Keep game data, saves, signing material and device identifiers private; do not
upload them to GitHub or a public issue. Do not change project source, graphics
code, game files or security settings to make installation pass. Report the
installed version/build, verification result, texture-preparation status and
what gameplay I actually confirmed. Distinguish completed steps from anything
still waiting on my input; do not promise locked 30 FPS on every device.
```

## Requirements

- An ARM64 iPhone or iPad running iOS/iPadOS 26.0 or later.
- AltStore, SideStore, or another IPA installer that can sign the app with your
  Apple account. The release IPA is deliberately unsigned.
- Approximately 7 GB of free device storage for a prepared base-game install,
  in addition to the app and any space required by the sideloading tool.
  GPUs without direct BC texture support also need about 2.4 GB for the saved
  ASTC texture cache; keep additional free space for preparation and transfers.
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
3. Install Theft4 on the destination iPhone or iPad. The official release uses
   `com.lukebrosious.theft4`, the official app identifier. Update that app
   in place to retain its game files and saves. Do not delete the old app.
   The separate `com.theft4.m5lab` app is not the update target and has a
   separate container.
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
2. Open **System** and select **Check Game Files**. This checks a prepared,
   already-updated installation before requesting another update package.
   Resolve any reported base or title-update mismatch before continuing.
3. Complete the one-time texture preparation if the app requires it for your
   GPU. Keep Theft4 open, preferably on power, and allow the lengthy first run;
   the progress bar and ETA describe the work remaining. Pause/resume retains
   completed conversions. Normal launches reuse the saved cache. BC-capable
   GPUs skip this requirement automatically; deleting the cache or replacing
   game files can require preparation again.
4. Return to **Play** and start the game. A physical controller works whether
   touch controls are enabled or disabled.

Theft4 stores runtime settings, caches, and saves in its private app container,
separate from the shared `Documents/game` folder. An in-place update keeps this
container. Deleting the app removes it, including saves.

## 0.3.1 graphics and diagnostics

- Gameplay uses a centered 16:9 scene by default. The optional **Mods → Native
  Aspect Ratio** expands the view to the launch window while preserving
  interface proportions; more visible scenery can increase rendering cost.
  **Graphics** selects Native Pixels, 540p, 720p,
  900p or 1080p internal resolution, FSR,
  shadows, draw distance, model detail, reflections, anti-aliasing, filtering
  and motion blur, plus adjustable sharpening. **Frame Speed** keeps the selected
  resolution and reduces other quality costs. Changes apply at the next game launch.
- **Interface** has the frame counter, a frame-time graph and touch controls.
  The graph shows frame publication intervals against a 33.3 ms target.
- Normal play suppresses development telemetry. For a low-FPS report, open
  **System → Long Performance Capture** before Play. It resets Off on each
  app launch. Reproduce the issue, then hold the enabled frame-time graph and
  choose **Stop and save capture**, or background the app to save. Capture also
  saves after its five-minute limit. Reopen the launcher and select
  **System → Download Latest Log Capture**.
  Save or share the dated text bundle from **Files → On My iPhone/iPad → Theft4
  → Diagnostics**. Include the device, route, graphics settings, play duration
  and whether the device felt hot. Profiling adds overhead; also describe an
  ordinary run with capture off. See the [full capture and submission guide](../README.md#capture-and-submit-a-performance-log).

TestFlight testers update an available signed build in TestFlight, then follow the
same game-folder and diagnostics steps. The official app identifier is
`com.lukebrosious.theft4` for both distribution methods. The historical Lab
app uses `com.theft4.m5lab` and keeps separate game files and saves. Before
switching from Lab to official Theft4, copy or back up the Lab data.

## Back up or transfer saves

The save-transfer controls are included in 0.2.1 build 52.

With the game closed, reopen Theft4 and choose **System → Export Saves to
Files**. In Files, open **On My iPhone/iPad → Theft4 → Save Exports** and copy
the entire dated `Theft4-Saves-…` folder to your backup destination. On the
destination device, choose **System → Import Saves from Files**, select that
folder, and confirm the replacement. Theft4 validates the export and creates
a dated backup of existing saves in **Save Exports** before importing. Restart
the game after import. The export contains GTA IV saves and profile data, not
the game installation or title update. The separate historical Lab app has a
different private container and is not migrated automatically.

## Common installation failures

| Message or symptom | Cause and fix |
|---|---|
| `default.xex` is missing | The prepared folder was nested as `game/game`. Move the inner folder's contents up one level. |
| `default.xexp` is missing | The TU package itself was copied instead of being staged. Run the validated staging process and copy its sibling `default.xexp`. |
| TU8 or patch mismatch | The update is for another base revision or region. Use the exact `0.0.0.5 → 0.0.8.5` update described above. |
| App still shows no game after copying | The transfer may still be running or may have gone to iCloud Drive instead of **On My iPhone/iPad → Theft4 → game**. |
| Files disappear after reinstalling | Uninstalling an iOS app removes its container. Update in place where possible and keep separate backups of game data and saves. |
