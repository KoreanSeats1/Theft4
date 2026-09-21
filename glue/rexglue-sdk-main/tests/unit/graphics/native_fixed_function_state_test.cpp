#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <type_traits>

#include "graphics/gta4_native/native_fixed_function_state.h"

namespace rex::graphics::gta4_native {
namespace {

NativeFixedFunctionState MakeFingerprintState(uint32_t seed = 1) {
  NativeFixedFunctionState state{};
  uint32_t next = seed;
  const auto value = [&next] { return next++; };
  state.depth_enable = value();
  state.depth_function = value();
  state.depth_write_enable = value();
  state.depth_clamp_enable = value();
  state.clip_control = value();
  state.user_clip_plane_enable_mask = value();
  for (auto& item : state.clip_plane_bits) item = value();
  state.negative_one_to_one_clip_space = value();
  state.cull_mode = value();
  state.polygon_mode = value();
  state.blend_enable = value();
  for (auto& item : state.blend_controls) item = value();
  state.source_blend = value();
  state.destination_blend = value();
  state.blend_operation = value();
  state.source_blend_alpha = value();
  state.destination_blend_alpha = value();
  state.blend_operation_alpha = value();
  for (auto& item : state.blend_constants) item = std::bit_cast<float>(value());
  state.alpha_test_enable = value();
  state.alpha_function = value();
  state.alpha_reference = std::bit_cast<float>(value());
  state.alpha_to_mask_enable = value();
  state.alpha_to_mask = value();
  state.stencil_enable = value();
  state.two_sided_stencil = value();
  state.stencil_fail = value();
  state.stencil_depth_fail = value();
  state.stencil_pass = value();
  state.stencil_function = value();
  state.stencil_reference = value();
  state.stencil_mask = value();
  state.stencil_write_mask = value();
  state.back_stencil_reference = value();
  state.back_stencil_mask = value();
  state.back_stencil_write_mask = value();
  state.ccw_stencil_fail = value();
  state.ccw_stencil_depth_fail = value();
  state.ccw_stencil_pass = value();
  state.ccw_stencil_function = value();
  state.scissor_enable = value();
  state.slope_scaled_depth_bias_bits = value();
  state.depth_bias_bits = value();
  state.depth_bias_enable = true;
  state.depth_bias_representable = false;
  state.color_write_mask = value();
  state.sample_mask = value();
  for (auto& item : state.viewport_bits) item = value();
  for (auto& item : state.scissor) item = std::bit_cast<int32_t>(value());
  return state;
}

TEST_CASE("fixed-function fingerprint serializes every logical field once",
          "[gta4-native][fixed-function][fingerprint]") {
  const NativeFixedFunctionState state = MakeFingerprintState();
  const auto words = NativeFixedFunctionFingerprintWords(state);
  REQUIRE(words.size() == kNativeFixedFunctionFingerprintWordCount);

  size_t index = 0;
  uint32_t expected = 1;
  // Every non-boolean input was assigned the next integer bit pattern.
  for (; index < 52; ++index) CHECK(words[index] == expected++);
  CHECK(words[index++] == 1u);
  CHECK(words[index++] == 0u);
  for (; index < words.size(); ++index) CHECK(words[index] == expected++);
  CHECK(index == words.size());
}

TEST_CASE("fixed-function fingerprint excludes structure padding",
          "[gta4-native][fixed-function][fingerprint]") {
  static_assert(std::is_trivially_copyable_v<NativeFixedFunctionState>);
  NativeFixedFunctionState first = MakeFingerprintState(100);
  NativeFixedFunctionState second = first;
  constexpr size_t padding_begin =
      offsetof(NativeFixedFunctionState, depth_bias_representable) + sizeof(bool);
  constexpr size_t padding_end = offsetof(NativeFixedFunctionState, color_write_mask);
  static_assert(padding_end > padding_begin);
  std::memset(reinterpret_cast<std::byte*>(&first) + padding_begin, 0xA5,
              padding_end - padding_begin);
  std::memset(reinterpret_cast<std::byte*>(&second) + padding_begin, 0x5A,
              padding_end - padding_begin);
  CHECK(first == second);
  CHECK(NativeFixedFunctionFingerprintWords(first) ==
        NativeFixedFunctionFingerprintWords(second));
  CHECK(HashNativeFixedFunctionState(first) == HashNativeFixedFunctionState(second));
}

TEST_CASE("fixed-function fingerprint retains floating-point bit identity",
          "[gta4-native][fixed-function][fingerprint]") {
  NativeFixedFunctionState positive_zero{};
  NativeFixedFunctionState negative_zero{};
  negative_zero.alpha_reference = -0.0f;
  CHECK(HashNativeFixedFunctionState(positive_zero) !=
        HashNativeFixedFunctionState(negative_zero));

  NativeFixedFunctionState nan_a{};
  NativeFixedFunctionState nan_b{};
  nan_a.blend_constants[2] = std::bit_cast<float>(0x7FC00001u);
  nan_b.blend_constants[2] = std::bit_cast<float>(0x7FC00002u);
  CHECK(HashNativeFixedFunctionState(nan_a) != HashNativeFixedFunctionState(nan_b));
}

TEST_CASE("fixed-function fingerprint is stable across copied randomized states",
          "[gta4-native][fixed-function][fingerprint]") {
  uint32_t seed = 0xC001D00Du;
  for (uint32_t iteration = 0; iteration < 256; ++iteration) {
    seed = seed * 1664525u + 1013904223u;
    const NativeFixedFunctionState state = MakeFingerprintState(seed);
    const NativeFixedFunctionState copy = state;
    CHECK(HashNativeFixedFunctionState(state) == HashNativeFixedFunctionState(copy));
    NativeFixedFunctionState mutation = state;
    mutation.viewport_bits[iteration % mutation.viewport_bits.size()] ^= 1u;
    CHECK(HashNativeFixedFunctionState(state) != HashNativeFixedFunctionState(mutation));
  }
}

}  // namespace
}  // namespace rex::graphics::gta4_native
