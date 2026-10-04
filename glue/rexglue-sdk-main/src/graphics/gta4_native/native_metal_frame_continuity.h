#pragma once
#include "postfx_resource_pool.h"

namespace rex::graphics::gta4_native {
// GPU submission boundaries do not end the title frame. Copy this with a
// speculative batch, commit it only on admission, and advance at guest Present
// even when that Present was rejected (the title has still ended its frame).
struct NativeMetalFrameContinuity {
  PostFxScheduler postfx;
  uint32_t frame_identity = 1;
  void EndGuestFrame() {
    postfx.BeginFrame();
    ++frame_identity;
  }
};
}  // namespace rex::graphics::gta4_native
