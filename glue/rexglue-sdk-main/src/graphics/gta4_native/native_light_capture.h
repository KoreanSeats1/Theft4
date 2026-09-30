#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// All times are host monotonic ticks except the two renderer CPU fields (ns).
// Exactly one renderer writes; UI reads only on explicit save/background.
// Fields104-106 are per-present pipeline work, valid on every row. Fields107-115
// are compiler/cache snapshots sampled with sample_valid bit8. Pipeline ticks
// reflect driver-call durations (including background work), not GPU time.
// Fields116 onward are bounded command/phase/boundary and activity metadata.
// Phase IDs follow RenderPhase; counts describe commands visited in recording.
// Fields164-166 are cumulative prewarm work counters;167 is the launch mode.
// Fields168-175: efficiency launch mode, then cumulative renderer work counters.
// Dynamic counts are state groups, not individual Vulkan calls (stencil groups
// may emit three or six calls). Counter snapshots are taken at publication end.
enum { REX_LIGHT_FIELDS = 176 };
typedef struct rex_light_sample { uint64_t value[REX_LIGHT_FIELDS]; } rex_light_sample;
// Renderer-thread only; actual driver calls/publication, never pipeline hits.
// Work outside a title frame is retained until the next recorded title frame.
void rex_gta4_light_record_pipeline_work(uint64_t compile_ticks, uint64_t wait_ticks,
                                         uint32_t creates);
const char* rex_gta4_light_capture_extra_columns(void);
void rex_gta4_light_capture_write_fault_snapshot(int fd);
uint64_t rex_gta4_light_capture_start(void);
void rex_gta4_light_capture_stop(void);
void rex_gta4_light_memory_warning(void);
uint64_t rex_gta4_light_capture_frequency(void);
uint32_t rex_gta4_light_capture_read(uint64_t *cursor, rex_light_sample *out,
                                    uint32_t capacity, uint64_t *lost);
#ifdef __cplusplus
}
#endif
