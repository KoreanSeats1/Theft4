#pragma once

#include <stdlib.h>

// Consolidated build-93 configuration. These are app policy, not persisted
// comparison settings; upgrading cannot revive an obsolete experiment choice.
#define THEFT4_DEFAULT_FRAME_SCHEDULING 1
#define THEFT4_DEFAULT_DIRECT_GUEST_CLOCK 1
#define THEFT4_DEFAULT_RUNTIME_WAIT_FIXES 0
#define THEFT4_DEFAULT_RUNTIME_WAIT_POLICY "0"

static inline void theft4_apply_performance_defaults(void) {
    setenv("THEFT4_COMMAND_STREAM", "1", 1);
    setenv("THEFT4_CPU_CLEANUP", "1", 1);
    setenv("THEFT4_MEMORY_RECOVERY", "1", 1);
    setenv("THEFT4_GRAPHICS_PREPARATION", "1", 1);
    setenv("THEFT4_FUSED_SMAA", "1", 1);
    setenv("THEFT4_HARDWARE_SMAA", "1", 1);
    setenv("THEFT4_FRAME_RESOURCE_SHARING", "1", 1);
    setenv("THEFT4_FRAME_ASSEMBLY", "1", 1);
    setenv("THEFT4_PARALLEL_TEXTURE_CONVERSION", "1", 1);
    setenv("THEFT4_PARALLEL_PREPARATION", "1", 1);
    setenv("THEFT4_RENDERER_EFFICIENCY", "1", 1);
    setenv("THEFT4_PREWARM_TARGET_REUSE", "1", 1);
    setenv("THEFT4_DIRECT_GUEST_CLOCK", "1", 1);
    setenv("THEFT4_RUNTIME_WAIT_FIXES", "0", 1);
}
