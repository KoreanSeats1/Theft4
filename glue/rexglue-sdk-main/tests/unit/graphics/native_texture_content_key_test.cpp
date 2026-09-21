#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>

#include "graphics/gta4_native/native_texture_content_key.h"

namespace gta4 = rex::graphics::gta4_native;
namespace xenos = rex::graphics::xenos;

TEST_CASE("Texture content key retains every unclassified fetch bit") {
  const std::array<uint32_t, 6> seed{0xCAFEBABE, 0x12345678, 0x34567890,
                                     0xDEADBEEF, 0x76543210, 0xAABBCCDD};
  xenos::xe_gpu_texture_fetch_t original{};
  std::memcpy(&original, seed.data(), sizeof(original));
  unsigned sampling_bits = 0;
  for (size_t word = 0; word < seed.size(); ++word) {
    for (unsigned bit = 0; bit < 32; ++bit) {
      auto words = seed;
      words[word] ^= 1u << bit;
      xenos::xe_gpu_texture_fetch_t changed{};
      std::memcpy(&changed, words.data(), sizeof(changed));
      // This classification is independent of the implementation masks.
      const bool sampling = (word == 0 && bit >= 10 && bit <= 18) ||
                            (word == 3 && bit >= 19 && bit <= 30) ||
                            (word == 4 && (bit <= 1 || (bit >= 10 && bit <= 21))) ||
                            (word == 5 && (bit <= 1 || (bit >= 3 && bit <= 8)));
      CAPTURE(word, bit);
      CHECK_FALSE(gta4::NativeTextureContentMatches(original, changed, false));
      CHECK(gta4::NativeTextureContentMatches(original, changed, true) == sampling);
      sampling_bits += sampling;
    }
  }
  CHECK(sampling_bits == 43);
  CHECK(gta4::NativeTextureContentMatches(original, original, false));
}

TEST_CASE("Texture content key handles named fields and keeps decoded mip coverage") {
  const xenos::xe_gpu_texture_fetch_t original{};
  const auto matches_after = [&](auto change) {
    auto changed = original;
    change(changed);
    CHECK_FALSE(gta4::NativeTextureContentMatches(original, changed, false));
    return gta4::NativeTextureContentMatches(original, changed, true);
  };
  CHECK(matches_after([](auto& f) { f.mag_filter = xenos::TextureFilter::kLinear; }));
  CHECK(matches_after([](auto& f) { f.min_filter = xenos::TextureFilter::kLinear; }));
  CHECK(matches_after([](auto& f) { f.mip_filter = xenos::TextureFilter::kLinear; }));
  CHECK(matches_after([](auto& f) { f.aniso_filter = xenos::AnisoFilter::kMax_4_1; }));
  CHECK(matches_after([](auto& f) { f.clamp_x = static_cast<xenos::ClampMode>(1); }));
  CHECK(matches_after([](auto& f) { f.clamp_y = static_cast<xenos::ClampMode>(1); }));
  CHECK(matches_after([](auto& f) { f.clamp_z = static_cast<xenos::ClampMode>(1); }));
  CHECK(matches_after([](auto& f) { f.lod_bias = -16; }));
  CHECK(matches_after([](auto& f) { f.border_color = static_cast<xenos::BorderColor>(1); }));
  CHECK_FALSE(matches_after([](auto& f) { f.base_address = 0x1234; }));
  CHECK_FALSE(matches_after([](auto& f) { f.mip_address = 0x4321; }));
  CHECK_FALSE(matches_after([](auto& f) { f.mip_min_level = 1; }));
  CHECK_FALSE(matches_after([](auto& f) { f.mip_max_level = 4; }));
  CHECK_FALSE(matches_after([](auto& f) { f.format = static_cast<xenos::TextureFormat>(1); }));
  CHECK_FALSE(matches_after([](auto& f) { f.endianness = static_cast<xenos::Endian>(1); }));
  CHECK_FALSE(matches_after([](auto& f) { f.sign_x = static_cast<xenos::TextureSign>(1); }));
  CHECK_FALSE(matches_after([](auto& f) { f.pitch = 8; }));
  CHECK_FALSE(matches_after([](auto& f) { f.tiled = 1; }));
  CHECK_FALSE(matches_after([](auto& f) { f.stacked = 1; }));
  CHECK_FALSE(matches_after([](auto& f) { f.size_2d.width = 127; }));
  CHECK_FALSE(matches_after([](auto& f) { f.size_2d.height = 127; }));
  CHECK_FALSE(matches_after([](auto& f) { f.size_2d.stack_depth = 5; }));
  CHECK_FALSE(matches_after([](auto& f) { f.dimension = xenos::DataDimension::kCube; }));
  CHECK_FALSE(matches_after([](auto& f) { f.packed_mips = 1; }));
  CHECK_FALSE(matches_after([](auto& f) { f.swizzle = 1; }));
  CHECK_FALSE(matches_after([](auto& f) { f.exp_adjust = 1; }));
  CHECK_FALSE(matches_after([](auto& f) { f.border_size = 1; }));
  CHECK_FALSE(matches_after([](auto& f) { f.force_bc_w_to_max = 1; }));
}

TEST_CASE("Alternating samplers share texture content without modifying draw fetches") {
  xenos::xe_gpu_texture_fetch_t point{};
  point.base_address = 0x1234;
  point.mip_address = 0x5678;
  point.mip_max_level = 4;
  auto linear = point;
  linear.mag_filter = xenos::TextureFilter::kLinear;
  linear.min_filter = xenos::TextureFilter::kLinear;
  const auto point_before = gta4::NativeTextureContentKey(point, false);
  const auto linear_before = gta4::NativeTextureContentKey(linear, false);
  CHECK(gta4::NativeTextureContentMatches(point, linear, true));
  CHECK_FALSE(gta4::NativeTextureContentMatches(point, linear, false));
  CHECK(gta4::NativeTextureContentKey(point, false) == point_before);
  CHECK(gta4::NativeTextureContentKey(linear, false) == linear_before);
  CHECK(point.min_filter != linear.min_filter);
}
