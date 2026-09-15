# Theft4 iOS checkpoint — 2026-09-15

The development build has reached full 3D through the GTA IV opening and the
first player-control state on an M5 iPad Pro. This is an experimental AOT port,
not a finished release or a claim of full-speed gameplay on all devices.

This source checkpoint includes:

- Embedded XMA decoding and native iOS audio output improvements, including
  inline decode, bounded recovery buffering, gain-ramp validation, and opt-in
  diagnostic logging.
- Centered 16:9 presentation at a 1280x720 drawable size.
- Release configuration, ARM64 scheduling tuning without changing the intended
  ISA baseline, and lightweight spin/fence/queue handling improvements.
- Existing shader-storage startup wiring and graphics diagnostics.
- Architecture reports, the audio/aspect history, and the next performance plan.

Start with [the 3D performance plan](../THEFT4_3D_PERFORMANCE_PLAN.md).
Its cache-reader correction and later optimization passes are **not implemented**
by this checkpoint. The next authorized task is to preserve the working baseline,
fix the two cache readers, verify warm pipeline reuse, and measure heavy scenes.

## Measurement limits

Sparse logs show roughly 19–26 guest swaps/s through earlier sustained 3D and
12–17 in later heavy harbor/car scenes. These are not unique displayed FPS.
The long Metal trace failed to retain usable CPU/GPU/display events. A subsequent
short CPU profile identifies graphics command preparation, copies, register
handling and audio mixing as significant CPU work, but does not prove GPU
saturation. See the plan for the exact evidence and limitations.

## Reproduction and publication scope

No ISO, XEX, title update, extracted asset, shader cache, device trace, screenshot,
save, signed application, signing credential, or new generated game payload is
included in this checkpoint. Small hand-maintained spin-hint changes to generated
files already tracked by the repository are retained with the source changes.
Users must supply their own legally obtained compatible game files.

Private device handoff notes and raw measurements remain local. Published report
paths and device identifiers use placeholders. Historical reports describe
different stages of the project; prefer the newest dated status sections.

The checkout's dirty third-party submodules are not separately committed or
pushed. The repository already maintains dependency patches under
`cmake/dependency-patches`; use the documented setup workflow. This checkpoint
does not change submodule revision pins or claim a fresh-clone iOS build test.

The current iOS build also links MoltenVK archives from a sibling XeniOS build
through `THEFT4_XENIOS_IOS_LIB_DIR`. Those archives are not uploaded here.
Independent, reproducible iOS dependency packaging remains work to do. Do not
overwrite a working XeniOS build when experimenting with Theft4 dependencies.

This upload does not implement new performance changes or relaunch the iPad app.
