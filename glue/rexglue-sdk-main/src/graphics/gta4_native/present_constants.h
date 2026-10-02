#pragma once
#include <cstdint>
namespace rex::graphics::gta4_native {
// GLSL push-constant ABI shared by presentation and fused SMAA.
struct NativePresentConstants {
  int32_t source_width = 0, source_height = 0;
  int32_t destination_width = 0, destination_height = 0;
  float hdr_headroom = 1.0f;
  uint32_t output_mode = 0, hdr_mode = 0;
  float paper_white_nits = 203.0f, peak_nits = 400.0f;
  float shoulder_start = 0.0f, shoulder_power = 2.5f;
};
static_assert(sizeof(NativePresentConstants) == 44);
}  // namespace rex::graphics::gta4_native
