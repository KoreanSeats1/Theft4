#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// All times are host monotonic ticks except the two renderer CPU fields (ns).
// Exactly one renderer writes; UI reads only on explicit save/background.
enum { REX_LIGHT_FIELDS = 23 };
typedef struct rex_light_sample { uint64_t value[REX_LIGHT_FIELDS]; } rex_light_sample;
uint64_t rex_gta4_light_capture_start(void);
void rex_gta4_light_capture_stop(void);
void rex_gta4_light_memory_warning(void);
uint64_t rex_gta4_light_capture_frequency(void);
uint32_t rex_gta4_light_capture_read(uint64_t *cursor, rex_light_sample *out,
                                    uint32_t capacity, uint64_t *lost);
#ifdef __cplusplus
}
#endif
