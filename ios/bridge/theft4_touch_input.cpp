#include "theft4_touch_input.h"
#include <algorithm>
#include <mutex>

namespace {
std::mutex mutex;
theft4_touch_pad touch{}, previous{};
uint32_t packet = 0;
bool Equal(theft4_touch_pad a, theft4_touch_pad b) {
    return a.buttons == b.buttons && a.left_trigger == b.left_trigger &&
        a.right_trigger == b.right_trigger && a.lx == b.lx && a.ly == b.ly &&
        a.rx == b.rx && a.ry == b.ry;
}
void MergeStick(int16_t tx, int16_t ty, int16_t &px, int16_t &py) {
    // Choose a whole stick vector, never synthesize a diagonal from two devices.
    const auto magnitude = [](int16_t x, int16_t y) {
        return int64_t(x) * x + int64_t(y) * y;
    };
    if (magnitude(tx, ty) > magnitude(px, py)) { px = tx; py = ty; }
}
}
void theft4_touch_store(theft4_touch_pad state) {
    std::lock_guard lock(mutex);
    touch = state;
}
void theft4_touch_reset(void) { theft4_touch_store({}); }
uint32_t theft4_touch_merge(theft4_touch_pad *physical) {
    std::lock_guard lock(mutex);
    physical->buttons |= touch.buttons;
    physical->left_trigger = std::max(physical->left_trigger, touch.left_trigger);
    physical->right_trigger = std::max(physical->right_trigger, touch.right_trigger);
    MergeStick(touch.lx, touch.ly, physical->lx, physical->ly);
    MergeStick(touch.rx, touch.ry, physical->rx, physical->ry);
    if (!Equal(*physical, previous)) { ++packet; previous = *physical; }
    return packet;
}
