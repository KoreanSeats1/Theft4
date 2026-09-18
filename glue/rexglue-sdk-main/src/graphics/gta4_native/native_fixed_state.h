#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <xxhash.h>

namespace rex::graphics::gta4_native {

// Same state/layout as the renderer snapshot; independent of Vulkan for tests.
template <size_t RenderTargetCount>
struct NativeFixedFunctionStateStorage {
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
  std::array<uint32_t, RenderTargetCount> blend_controls{};
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
  uint32_t alpha_to_mask_enable = 0;
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

  bool operator==(const NativeFixedFunctionStateStorage&) const = default;
};

template <typename State, typename Visitor>
void VisitNativeFixedStateFields(State& state, Visitor&& visit) {
  visit(state.depth_enable);
  visit(state.depth_function);
  visit(state.depth_write_enable);
  visit(state.depth_clamp_enable);
  visit(state.clip_control);
  visit(state.user_clip_plane_enable_mask);
  visit(state.clip_plane_bits);
  visit(state.negative_one_to_one_clip_space);
  visit(state.cull_mode);
  visit(state.polygon_mode);
  visit(state.blend_enable);
  visit(state.blend_controls);
  visit(state.source_blend);
  visit(state.destination_blend);
  visit(state.blend_operation);
  visit(state.source_blend_alpha);
  visit(state.destination_blend_alpha);
  visit(state.blend_operation_alpha);
  visit(state.blend_constants);
  visit(state.alpha_test_enable);
  visit(state.alpha_function);
  visit(state.alpha_reference);
  visit(state.alpha_to_mask_enable);
  visit(state.alpha_to_mask);
  visit(state.stencil_enable);
  visit(state.two_sided_stencil);
  visit(state.stencil_fail);
  visit(state.stencil_depth_fail);
  visit(state.stencil_pass);
  visit(state.stencil_function);
  visit(state.stencil_reference);
  visit(state.stencil_mask);
  visit(state.stencil_write_mask);
  visit(state.back_stencil_reference);
  visit(state.back_stencil_mask);
  visit(state.back_stencil_write_mask);
  visit(state.ccw_stencil_fail);
  visit(state.ccw_stencil_depth_fail);
  visit(state.ccw_stencil_pass);
  visit(state.ccw_stencil_function);
  visit(state.scissor_enable);
  visit(state.slope_scaled_depth_bias_bits);
  visit(state.depth_bias_bits);
  visit(state.depth_bias_enable);
  visit(state.depth_bias_representable);
  visit(state.color_write_mask);
  visit(state.sample_mask);
  visit(state.viewport_bits);
  visit(state.scissor);
}

template <typename State>
auto NativeFixedStateBytes(const State& state) {
  std::array<uint8_t, sizeof(State)> bytes{};
  size_t offset = 0;
  VisitNativeFixedStateFields(state, [&](const auto& field) {
    using Field = std::remove_cvref_t<decltype(field)>;
    static_assert(std::is_trivially_copyable_v<Field>);
    std::memcpy(bytes.data() + offset, &field, sizeof(field));
    offset += sizeof(field);
  });
  return bytes;
}

template <typename State>
uint64_t HashNativeFixedFunctionState(const State& state) {
  const auto bytes = NativeFixedStateBytes(state);
  return XXH3_64bits(bytes.data(), bytes.size());
}

}  // namespace rex::graphics::gta4_native
