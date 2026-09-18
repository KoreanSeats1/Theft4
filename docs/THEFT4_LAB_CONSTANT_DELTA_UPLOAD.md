# Lab 16: direct constant-delta upload experiment

The Lab 15 control capture showed ample GPU headroom and tied frame-time
variation primarily to CPU command volume, native publication, producer
lateness and upload volume. Native constant handling materialized and converted
complete 4 KiB vertex and 3.5 KiB pixel blocks even when an immutable version
changed only a few register ranges from an already-bound parent.

Lab 16 adds a fenced frame-arena fast path for that ordered-draw case. When the
exact parent version already has an allocation in the active frame slot, the
renderer copies its host-order bytes to the child's allocation and applies only
the validated guest-order delta ranges. Complete snapshots, missing parents,
invalid deltas and allocation failures retain the existing full materialization
path. The cache retains the child version until the exact slot fence completes;
no allocation or raw byte pointer crosses frame-slot ownership.

This experiment is intended to reduce constant materialization, endian
conversion and temporary heap-allocation work. It does not change shaders, graphics settings,
the 30 FPS limiter, descriptor epochs, constant contents or GPU lifetime rules.
Acceptance requires the same-view logger-off comparison first, followed by a
bounded lightweight capture only if normal play remains correct.
