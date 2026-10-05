#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
// Process-owned mode. The launcher sets it before runtime construction; a
// settings change takes effect after reopening, never midway through a frame.
#ifdef __cplusplus
inline bool theft4_retail_mode() noexcept {
  static const bool enabled=[] {const char* v=getenv("THEFT4_RETAIL_MODE");return v&&strcmp(v,"1")==0;}();
  return enabled;
}
#else
static inline bool theft4_retail_mode(void) {
  const char* v=getenv("THEFT4_RETAIL_MODE");return v&&strcmp(v,"1")==0;
}
#endif

// Applied before runtime construction. Engineering launch flags cannot override
// Retail Mode; a fresh diagnostic launch retains its supplied flags untouched.
#ifdef __cplusplus
inline
#else
static inline
#endif
void theft4_apply_retail_diagnostic_policy(void) {
  if (!theft4_retail_mode()) return;
  const char* flags[] = {
    "THEFT4_DIAGNOSTICS",
    "THEFT4_DIAGNOSTIC_CAPTURE",
    "THEFT4_NATIVE_CONTENT_PROBE",
    "THEFT4_AUDIO_TIMING",
    "THEFT4_GPU_FLIGHT_TRACE",
    "THEFT4_PERFORMANCE_CAPTURE",
    "THEFT4_MOTION_BLUR_TRACE",
    "THEFT4_METAL_CAPTURE",
    "THEFT4_METAL_PASS_PROFILING",
    "THEFT4_DRAW_BOUNDS_METRICS",
    "THEFT4_TRANSFER_METRICS",
    "REX_GTA4_HELP_TRACE",
    "REX_GTA4_BULB_SOURCE_TRACE",
    "REX_GTA4_BULB_PIPELINE_TRACE",
    "REX_GTA4_EMISSION_TRACE",
    "REX_GTA4_EMISSION_PROBES",
    "REX_GTA4_EMISSION_VARIANTS",
    "REX_GTA4_EMISSION_COLOR_READBACK",
    "REX_GTA4_CUTOUT_BOOLEAN_TRACE",
    "REX_GTA4_GLASS_OUTPUT_TRACE",
  };
  const char* paths[] = {
    "REX_GPU_FLIGHT_TRACE_PATH",
    "REX_AUDIO_HANDOFF_DIR",
    "THEFT4_FRAME_CAPTURE_DIR",
    "REX_GTA4_FADE_ARM_FILE",
    "REX_GTA4_EMISSION_ARM_FILE",
    "REX_GTA4_EMISSION_OUTPUT",
  };
  for (size_t i=0;i<sizeof(flags)/sizeof(flags[0]);++i) setenv(flags[i],"0",1);
  for (size_t i=0;i<sizeof(paths)/sizeof(paths[0]);++i) unsetenv(paths[i]);
}
