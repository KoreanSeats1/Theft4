**Theft4 Lab setup — 17 September 2026**

The original M5 app remains **Theft4**, bundle ID `com.theft4.bringup`, version
0.1.2, build 4. Its installed name, identifier, version and installation URL
matched the pre-install inventory after Lab was installed. The independent app
is **Theft4 Lab**, bundle ID `com.theft4.m5lab`. Its bundle URL is different.
Neither the original app nor the main branch was renamed.

**Build and source record**

- Experiment branch: `codex/ipad-m5-mcla-experiments`.
- Built source commit: `0750e53262e0fc0cb6bf95c2c9310f07a6a7fa13`.
- Preserved tracked M5 base: `814905f975d70972d81b2c7c06c3e18340145b0f`.
- Lab executable SHA-256: `4b7bc0493e44de61d3c1d7f249fb1bf5b007fd4033b5c49ceef8f9d4e9549838`.
- Frozen M5 executable SHA-256: `b6537db28971738fc927894763c27e1cd5e7d0f77930fdfbce96907b8982b88c` (unchanged).
- Release build, native backend, Apple M5 tuning, ThinLTO off; no renderer
  optimization was introduced by the Lab setup.
- Private source/dependency copies and build outputs live in `out/m5-lab`.
  The archived signed app and provenance receipt are under
  `out/m5-lab/artifacts/0750e53262e0-20260917T200422Z/`.

The shared main checkout independently advanced to `90a7746f` (release 0.1.3)
and its ordinary build output changed while this work ran. Those concurrent
changes were preserved. The earlier all-files preservation comparison therefore
records those differences; it is not represented as an unchanged-main result.
The frozen M5 executable and the original installed M5 app identity remained
intact. Main still defaults to **Theft4 / com.theft4.bringup**, with no Lab
settings added to that checkout by this work.

**Checks completed**

- 35 infrastructure tests passed: 12 isolation, 13 dependency, 5 generator and
  5 Release-verifier tests.
- Actual CMake invocation using Lab mode with the original app ID was rejected.
- Release compiler checks passed for four common response files: `-O3` and
  `NDEBUG` for AOT, bridge and native renderer targets.
- Code signature, exact Lab identity, development team and private-container
  entitlement checks passed. Archived bundle hashes are rechecked by install.
- Device inventory confirms two separate installed apps and the original app's
  unchanged installation record.

Raw evidence is retained locally in `out/m5-lab/validation`, build/configure
logs in `out/m5-lab`, and the signed artifact receipt in the archive above.

**Data and remaining acceptance**

The working app's Library was exported read-only without stopping its process.
This is labeled a **live export**, not a proven coherent save snapshot. It
contains the available user/profile data, logs and caches. Its game installation
was independently exported and verified against the device inventory:
**1,689 files, 7,072,969,417 bytes**. Local content hashes were recorded.

The exported game files and available profile data were copied into Lab's own
container. Its game inventory matched all 1,689 paths and sizes; full file hashes
were recorded on the Mac, not re-read from the destination device. Lab passed
its launcher lifecycle self-test and reached the recompiled game entry point.
The user subsequently confirmed that Lab launches. The agent did not terminate
the original app or overwrite its container.

No five-minute driving test or runtime optimization acceptance has occurred.
The user authorized implementing the optimization candidates before further
driving measurements. GPU lifecycle, real display cadence, visuals,
audio and save/load behavior still need gameplay validation.

Keep future accepted runtime changes in separate commits. Combine them on an
integration branch based on the then-current main, test the combined Lab build,
and merge only accepted changes. Preserve main's original app identity and
current release version. The reusable instructions are in
[THEFT4_LAB_WORKFLOW.md](THEFT4_LAB_WORKFLOW.md).
