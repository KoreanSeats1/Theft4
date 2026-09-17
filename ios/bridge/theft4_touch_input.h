#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Host-endian snapshot. The Xbox bridge performs guest-endian conversion.
typedef struct theft4_touch_pad {
    uint16_t buttons;
    uint8_t left_trigger, right_trigger;
    int16_t lx, ly, rx, ry;
} theft4_touch_pad;

void theft4_touch_store(theft4_touch_pad state);
void theft4_touch_reset(void);
// Merge user-0 physical input with touch; returns a coherent packet number.
uint32_t theft4_touch_merge(theft4_touch_pad *physical);
#ifdef __cplusplus
}
#endif
