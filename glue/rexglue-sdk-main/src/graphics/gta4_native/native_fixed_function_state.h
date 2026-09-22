#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include <xxhash.h>

#include <rex/graphics/gta4_native/title_commands.h>

namespace rex::graphics::gta4_native {

struct NativeFixedFunctionState {
  uint32_t depth_enable = 0;
  uint32_t depth_function = 0;
  uint32_t depth_write_enable = 0;
  uint32_t depth_clamp_enable = 0;
  uint32_t clip_control = 0;
  uint32_t user_clip_plane_enable_mask = 0;
  std::array<uint32_t, 4> clip_plane_bits{};
  uint32_t negative_one_to_one_clip_space = 0;
  uint32_t cull_mode = 0;
  uint32_t polygon_mode = 0;
  uint32_t blend_enable = 0;
  std::array<uint32_t, kRenderTargetCount> blend_controls{};
  uint32_t source_blend = 0;
  uint32_t destination_blend = 0;
  uint32_t blend_operation = 0;
  uint32_t source_blend_alpha = 0;
  uint32_t destination_blend_alpha = 0;
  uint32_t blend_operation_alpha = 0;
  std::array<float, 4> blend_constants{};
  uint32_t alpha_test_enable = 0;
  uint32_t alpha_function = 0;
  float alpha_reference = 0.0f;
  // Xenos RB_COLORCONTROL bit 4. This is deliberately captured separately
  // from alpha test: foliage may request alpha-to-mask while alpha test is
  // disabled.
  uint32_t alpha_to_mask_enable = 0;
  // Native shader layout: four RB_COLORCONTROL offsets in bits 0-7 and the
  // enable in bit 8. This remains dynamic and is not a pipeline-key field.
  uint32_t alpha_to_mask = 0;
  uint32_t stencil_enable = 0;
  uint32_t two_sided_stencil = 0;
  uint32_t stencil_fail = 0;
  uint32_t stencil_depth_fail = 0;
  uint32_t stencil_pass = 0;
  uint32_t stencil_function = 0;
  uint32_t stencil_reference = 0;
  uint32_t stencil_mask = 0;
  uint32_t stencil_write_mask = 0;
  uint32_t back_stencil_reference = 0;
  uint32_t back_stencil_mask = 0;
  uint32_t back_stencil_write_mask = 0;
  uint32_t ccw_stencil_fail = 0;
  uint32_t ccw_stencil_depth_fail = 0;
  uint32_t ccw_stencil_pass = 0;
  uint32_t ccw_stencil_function = 0;
  uint32_t scissor_enable = 0;
  uint32_t slope_scaled_depth_bias_bits = 0;
  uint32_t depth_bias_bits = 0;
  bool depth_bias_enable = false;
  bool depth_bias_representable = true;
  uint32_t color_write_mask = 0;
  uint32_t sample_mask = 0xFFFFu;
  std::array<uint32_t, 6> viewport_bits{};
  std::array<int32_t, 4> scissor{};

  bool operator==(const NativeFixedFunctionState&) const = default;
};

inline constexpr size_t kNativeFixedFunctionFingerprintWordCount =
    62 + kRenderTargetCount;

// Serialize logical fields only. Structure padding never enters the digest,
// and float/int bits retain signed zero and NaN payload identity.
inline std::array<uint32_t, kNativeFixedFunctionFingerprintWordCount>
NativeFixedFunctionFingerprintWords(const NativeFixedFunctionState& state) noexcept {
  std::array<uint32_t, kNativeFixedFunctionFingerprintWordCount> words{};
  size_t index = 0;
  const auto add = [&words, &index](uint32_t value) { words[index++] = value; };
  const auto add_u32 = [&add](const auto& values) {
    for (const auto value : values) {
      add(uint32_t(value));
    }
  };
  const auto add_f32 = [&add](const auto& values) {
    for (const float value : values) {
      add(std::bit_cast<uint32_t>(value));
    }
  };

  add(state.depth_enable);
  add(state.depth_function);
  add(state.depth_write_enable);
  add(state.depth_clamp_enable);
  add(state.clip_control);
  add(state.user_clip_plane_enable_mask);
  add_u32(state.clip_plane_bits);
  add(state.negative_one_to_one_clip_space);
  add(state.cull_mode);
  add(state.polygon_mode);
  add(state.blend_enable);
  add_u32(state.blend_controls);
  add(state.source_blend);
  add(state.destination_blend);
  add(state.blend_operation);
  add(state.source_blend_alpha);
  add(state.destination_blend_alpha);
  add(state.blend_operation_alpha);
  add_f32(state.blend_constants);
  add(state.alpha_test_enable);
  add(state.alpha_function);
  add(std::bit_cast<uint32_t>(state.alpha_reference));
  add(state.alpha_to_mask_enable);
  add(state.alpha_to_mask);
  add(state.stencil_enable);
  add(state.two_sided_stencil);
  add(state.stencil_fail);
  add(state.stencil_depth_fail);
  add(state.stencil_pass);
  add(state.stencil_function);
  add(state.stencil_reference);
  add(state.stencil_mask);
  add(state.stencil_write_mask);
  add(state.back_stencil_reference);
  add(state.back_stencil_mask);
  add(state.back_stencil_write_mask);
  add(state.ccw_stencil_fail);
  add(state.ccw_stencil_depth_fail);
  add(state.ccw_stencil_pass);
  add(state.ccw_stencil_function);
  add(state.scissor_enable);
  add(state.slope_scaled_depth_bias_bits);
  add(state.depth_bias_bits);
  add(uint32_t(state.depth_bias_enable));
  add(uint32_t(state.depth_bias_representable));
  add(state.color_write_mask);
  add(state.sample_mask);
  add_u32(state.viewport_bits);
  for (const int32_t value : state.scissor) {
    add(std::bit_cast<uint32_t>(value));
  }
  return words;
}

inline uint64_t HashNativeFixedFunctionState(
    const NativeFixedFunctionState& state) noexcept {
  const auto words = NativeFixedFunctionFingerprintWords(state);
  return XXH3_64bits(words.data(), sizeof(words));
}

}  // namespace rex::graphics::gta4_native
