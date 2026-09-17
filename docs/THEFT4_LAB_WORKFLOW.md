**Theft4 Lab: isolated M5 experiments and eventual integration**

Lab is an independent signed app (`com.theft4.m5lab`, display name `Theft4 Lab`).
The ordinary M5 app remains `com.theft4.bringup`. The launcher labels Lab builds
as experimental. App-local Documents, preferences, saves, caches and logs are
separate; Lab must not gain shared app-group, cloud or keychain entitlements.
Game files and a test save must be copied into Lab's own container.

**What is versioned**

Work on `codex/ipad-m5-mcla-experiments` in its separate worktree. The initial
history is ordinary main `a25c54cf`, preserved tracked M5 changes `814905f9`, then
audit `ead522f5`. Runtime optimizations are not part of the Lab setup. Defaults
remain ordinary Theft4 unless the explicit Lab CMake configuration is selected.

All source changes are committed in the worktree before building. Build staging,
private dependencies, app archives, save copies and raw logs stay in ignored
`out/m5-lab/`. Do not commit game data, signing material, saves or binary payloads.
The frozen M5 source includes local dependency patches not represented by the
superproject alone; Lab copies that complete source and the five hashed graphics
archives. Do not run dependency-update helpers inside the frozen snapshot.

**Prepare, build and install**

Run from the experimental checkout. Substitute the actual frozen baseline,
development team, CMake executable and physical M5 device identifier:

```sh
python3 tools/theft4_lab.py prepare \
  --baseline /absolute/path/to/m5-stable \
  --team YOUR_TEAM_ID \
  --cmake /absolute/path/to/cmake
python3 tools/theft4_lab.py build --jobs 4
python3 tools/theft4_lab.py verify
python3 tools/theft4_lab.py install --device YOUR_M5_DEVICE_ID
```

Preparation creates private APFS file clones, overlays the committed worktree
sources and configures a dedicated Release build. There are no hard links back
to baseline inputs. Source links escaping the private copy are rejected. Each
build verifies the prepared source/dependency manifests, Release optimization,
bundle identity, code signature, team and container entitlements. Installation
reverifies the archived app's hash and accepts only the fixed Lab identity.
No ordinary-app install, uninstall, launch or data-write operation exists in
this tool. Direct manual Xcode/device commands do not inherit these guards.

The source digest excludes `glue/rexglue-sdk-main/out`, where the SDK writes its
generated runtime archive. That directory is inside Lab's private source copy;
it cannot overwrite the original checkout's archive. Every other source file
remains covered by the snapshot check.

Each successful build is archived with a receipt containing source commit,
source/dependency digests and executable/app hashes. Re-running prepare after
a new commit refreshes the overlay, including deleted tracked files. Author
changes in the worktree, never in generated `out/m5-lab/source`. A changed
dependency revision requires a separately reviewed dependency snapshot.

**Data and initial validation**

Before the first test, export the ordinary app's preferences and
`Library/Application Support/Theft4/startup` while the game is not writing saves.
Keep an untouched backup and hash manifest. If the app was running during an
export, label it a live copy; it is not a guaranteed coherent save backup.
Copy a working test save into Lab; never move the original. The runtime's user
directory is `Library/Application Support/Theft4/startup/user`.
`Documents/game` contains the prepared game installation, not the private saves.
Use independent game-file copies; no shared writable game or save directory.

First verify the unchanged-renderer Lab build: separate app identity, launcher,
game verification, startup, test-save load, settings, audio, foreground/background
and a repeatable five-minute driving route. Match baseline output/filter/motion
blur settings. Different app identity and clean caches are confounders; warm
caches and establish Lab's own baseline before testing optimization candidates.
Launcher success or a build passing does not count as gameplay acceptance.

**Experiment and result discipline**

Use one conceptual runtime change per commit. Where practical, add an opt-in
switch with the baseline path as default. Do not stack unproven changes. Record:

| Field | Required evidence |
|---|---|
| Identity | Experiment ID, hypothesis, source commit, artifact receipt |
| Conditions | M5 device, save/route, warm-up, output/filter settings, thermal state |
| Performance | Actual presentation timing when supported, long-frame counts, relevant CPU/GPU/cache counters |
| Correctness | HUD, reflections, fonts, texture streaming, audio, save/load and lifecycle |
| Decision | Keep/reject/defer, evidence links, limitations and rollback commit |

The first performance candidate is guest sampler-only texture-content cache
churn. Count it before implementing a cache-key change. Preserve existing 30 Hz
pacing, filtering and FSR until measurements justify a separate experiment.

**Combining findings and merging back**

1. Keep accepted experiments as small commits and attach their test evidence.
   Revert rejected experiments in Lab; preserve their findings in the report.
2. Create a new integration worktree/branch from the then-current main. Resolve
   the original eight uncommitted M5 changes against `814905f9` explicitly; do
   not reset the original checkout or blindly replay the baseline snapshot.
3. Cherry-pick accepted feature commits into integration. A whole-branch merge
   is appropriate only if every included change is intended. Lab tooling may
   merge separately because its default build identity remains ordinary Theft4.
4. Build the combined candidate as Lab, repeat the same-route and lifecycle
   checks, and review the complete diff. Individually successful changes can
   interact; their combined result needs its own receipt and acceptance record.
5. Merge the reviewed integration branch into main. Building/installing a new
   ordinary Theft4 release is a separate promotion step after acceptance, with
   the preserved M5 app and coherent save backup retained for recovery.

The Lab setup authorizes experiment infrastructure and an isolated app. It does
not automatically promote any optimization or replace the working M5 app.
