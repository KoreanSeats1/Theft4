#include "theft4_touch_input.h"
#include <cassert>
#include <cstdio>

int main() {
    theft4_touch_reset();
    theft4_touch_pad pad{};
    auto initial = theft4_touch_merge(&pad);
    assert(pad.buttons == 0 && pad.lx == 0);
    theft4_touch_store({0x1000, 255, 0, 30000, 0, 0, -32767});
    pad = {0x2000, 0, 128, 0, 2000, 32000, 0};
    auto pressed = theft4_touch_merge(&pad);
    assert(pressed != initial && pad.buttons == 0x3000);
    assert(pad.left_trigger == 255 && pad.right_trigger == 128);
    assert(pad.lx == 30000 && pad.ly == 0 && pad.rx == 0 && pad.ry == -32767);
    pad = {0x2000, 0, 128, 0, 2000, 32000, 0};
    assert(theft4_touch_merge(&pad) == pressed);
    theft4_touch_reset(); // Hide/background/cancel must not release physical input.
    pad = {0x2000, 0, 128, 0, 2000, 32000, 0};
    auto released = theft4_touch_merge(&pad);
    assert(released != pressed && pad.buttons == 0x2000 && pad.left_trigger == 0);
    assert(pad.lx == 0 && pad.ly == 2000 && pad.rx == 32000 && pad.ry == 0);
    theft4_touch_store({0, 0, 0, 32767, 32767, 0, 0});
    pad = {0, 0, 0, -32768, -32768, 0, 0}; // squared magnitude cannot overflow.
    theft4_touch_merge(&pad);
    assert(pad.lx == -32768 && pad.ly == -32768);
    theft4_touch_reset();
    pad = {};
    theft4_touch_merge(&pad);
    assert(pad.buttons == 0 && pad.lx == 0 && pad.ry == 0);
    puts("touch input merge/release/packet tests passed");
}
