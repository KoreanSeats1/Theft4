# Theft4 0.2.0a — Lab foundation, setup and diagnostic update

> **Withdrawn:** This release was superseded by [0.2.0b](RELEASE_0.2.0B.md).
> Do not use build 43 as the current Lab-based official app. The notes below
> are retained for historical context and do not describe the current release.

0.2.0a is the corrective follow-up to 0.2.0. It keeps the **Theft4 Lab** renderer, scheduling, frame-pacing and quality-control implementation as the official Theft4 build. This is not a rollback or a merge with an older render path. The changes make first use and performance reports more practical for testers.

## What is included

- The promoted Lab renderer: Vulkan through MoltenVK/Metal, native GTA IV rendering, AOT ARM64 Release code, bounded frame/resource lifetime, compact command transport, reduced redundant state work and bounded pipeline preparation.
- The Lab graphics controls: internal resolution, FSR, quality settings, performance preset, motion-blur option, FPS display and frame-time graph.
- A first-launch installation route that creates the Files location, explains where prepared game data belongs, detects whether the base game is present, and guides selection of the matching title update.
- Detailed bounded performance capture and **Download Latest Log Capture**. A tester can capture a slow scene, quit, reopen Theft4, then export the most recent capture to the Files/share sheet.

## Get started

1. Install the signed TestFlight build, or sideload the unsigned ARM64 IPA with AltStore, SideStore, or a compatible signing tool.
2. Open Theft4. Follow the first-launch instructions to create **Files → On My iPhone/iPad → Theft4 → game**.
3. Copy the contents of a prepared, legally obtained compatible game folder to that `game` directory. Do not copy a raw ISO or nest it as `game/game`.
4. Select the matching title update when Theft4 asks, then use **System → Verify Game Files** before starting play.
5. Start from **Graphics → Apply Performance Preset** when diagnosing speed/heat. Let the device warm up, then adjust resolution and FSR one at a time.

For the exact supported base/title-update revision and full file layout, see [iOS sideload installation](IOS_SIDELOAD_INSTALL.md). TestFlight and the GitHub IPA use separate bundle identifiers and therefore separate containers; each installation needs its own game files and saves.

## Sending a useful low-FPS report

1. Before launch, open **System** and enable **Detailed Performance Capture**.
2. Reproduce the slow route. Double-tap the frame-time graph and let its bounded 600-frame capture finish.
3. Quit Theft4, reopen it, and select **Download Latest Log Capture**.
4. Save/share the dated diagnostic bundle from Files. Include device model, iOS version, route, graphics settings, play time, and whether it felt hot.

Capture changes runtime cost, so also describe a normal run with capture off. The log lets us distinguish CPU/queue work, GPU/present timing and thermal conditions; it does not itself prove an FPS or temperature uplift.

## Performance position

The Lab command-delivery work produced real improvements in targeted M5 iPad captures, including lower batch-transfer and lock-wait spans. Scene-to-scene frame times and thermal state still vary, so 0.2.0a does **not** claim locked 30 FPS, a universal FPS gain, or a measured temperature reduction. It provides the Lab performance base and the capture workflow needed to validate those outcomes route by route.

## Distribution

- **GitHub:** `v0.2.0a`, with an unsigned `ios-arm64.ipa` for sideload tools.
- **TestFlight:** version `0.2.0`, build `43`; TestFlight requires Apple’s numeric build number, while `0.2.0a` is the public GitHub corrective-release label.
