// Audit-only experiment. This does not change the renderer's cache predicate.
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "gta4_frame_limiter.h"
#include <rex/graphics/xenos.h>

namespace {
using Fetch = std::array<uint32_t, 6>;
// Conservative proposal based on xenos.h: retain every unclassified bit,
// especially mip range, source/layout, signs, swizzle and numeric conversion.
constexpr Fetch sampling_masks = {0x1FFu << 10, 0, 0, 0xFFFu << 19,
    3u | (3u << 10) | (0x3FFu << 12), 3u | (3u << 3) | (15u << 5)};
constexpr Fetch proposed_content_key(Fetch words) {
  for (size_t i = 0; i < words.size(); ++i) words[i] &= ~sampling_masks[i];
  return words;
}
}

TEST_CASE("Audit: named Xenos fields distinguish sampling and content") {
  namespace xenos = rex::graphics::xenos;
  xenos::xe_gpu_texture_fetch_t original{};
  const auto raw = [](const auto& fetch) {
    Fetch words{}; std::memcpy(words.data(), &fetch, sizeof(words)); return words;
  };
  const auto same_content = [&](auto change) {
    auto changed = original; change(changed);
    CHECK(raw(changed) != raw(original));
    return proposed_content_key(raw(changed)) == proposed_content_key(raw(original));
  };
  CHECK(same_content([](auto& f) { f.min_filter = xenos::TextureFilter::kLinear; }));
  CHECK(same_content([](auto& f) { f.mag_filter = xenos::TextureFilter::kLinear; }));
  CHECK(same_content([](auto& f) { f.mip_filter = xenos::TextureFilter::kLinear; }));
  CHECK(same_content([](auto& f) { f.aniso_filter = xenos::AnisoFilter::kMax_4_1; }));
  CHECK(same_content([](auto& f) { f.clamp_x = static_cast<xenos::ClampMode>(1); }));
  CHECK(same_content([](auto& f) { f.lod_bias = -16; }));
  CHECK(same_content([](auto& f) { f.border_color = static_cast<xenos::BorderColor>(1); }));
  CHECK_FALSE(same_content([](auto& f) { f.base_address = 0x1234; }));
  CHECK_FALSE(same_content([](auto& f) { f.mip_address = 0x4321; }));
  CHECK_FALSE(same_content([](auto& f) { f.format = static_cast<xenos::TextureFormat>(1); }));
  CHECK_FALSE(same_content([](auto& f) { f.endianness = static_cast<xenos::Endian>(1); }));
  CHECK_FALSE(same_content([](auto& f) { f.pitch = 8; }));
  CHECK_FALSE(same_content([](auto& f) { f.tiled = 1; }));
  CHECK_FALSE(same_content([](auto& f) { f.stacked = 1; }));
  CHECK_FALSE(same_content([](auto& f) { f.size_2d.width = 127; }));
  CHECK_FALSE(same_content([](auto& f) { f.size_2d.height = 127; }));
  CHECK_FALSE(same_content([](auto& f) { f.size_2d.stack_depth = 5; }));
  CHECK_FALSE(same_content([](auto& f) { f.dimension = xenos::DataDimension::kCube; }));
  CHECK_FALSE(same_content([](auto& f) { f.packed_mips = 1; }));
  CHECK_FALSE(same_content([](auto& f) { f.mip_min_level = 1; }));
  CHECK_FALSE(same_content([](auto& f) { f.mip_max_level = 4; }));
  CHECK_FALSE(same_content([](auto& f) { f.swizzle = 1; }));
  CHECK_FALSE(same_content([](auto& f) { f.exp_adjust = 1; }));
}

TEST_CASE("Audit: full fetch comparison misses for every classified sampler bit") {
  const Fetch original = {0xCAFEBABE, 0x12345678, 0x34567890,
                          0xDEADBEEF, 0x76543210, 0xAABBCCDD};
  unsigned sampler_bits = 0, preserved_bits = 0;
  for (size_t word = 0; word < 6; ++word) {
    for (unsigned bit = 0; bit < 32; ++bit) {
      auto changed = original;
      changed[word] ^= uint32_t(1) << bit;
      CHECK(changed != original); // Current six-dword equality necessarily misses.
      if (sampling_masks[word] & (uint32_t(1) << bit)) {
        CHECK(proposed_content_key(changed) == proposed_content_key(original));
        ++sampler_bits;
      } else {
        CHECK(proposed_content_key(changed) != proposed_content_key(original));
        ++preserved_bits;
      }
    }
  }
  std::cout << "AUDIT_KEY sampler_bits=" << sampler_bits
            << " preserved_bits=" << preserved_bits << '\n';
}

TEST_CASE("Audit: proposal changes only identity comparison, not invalidation") {
  Fetch a{};
  const auto b = a;
  const auto reusable = [](Fetch x, Fetch y, bool dirty, bool same_lifetime) {
    return !dirty && same_lifetime && proposed_content_key(x) == proposed_content_key(y);
  };
  CHECK(reusable(a, b, false, true));
  CHECK_FALSE(reusable(a, b, true, true));
  CHECK_FALSE(reusable(a, b, false, false));
  // Mip min/max remain content identity because CaptureTextureResource decodes
  // only this interval; blindly masking the entire LOD word is unsafe.
  for (unsigned bit = 2; bit <= 9; ++bit) {
    auto mip = a; mip[4] ^= 1u << bit;
    CHECK_FALSE(reusable(a, mip, false, true));
  }
}

TEST_CASE("Audit: synthetic sampler alternation comparison counts") {
  Fetch prior{}, prior_key{};
  unsigned old_misses = 0, proposed_misses = 0;
  for (unsigned i = 0; i < 100; ++i) {
    Fetch current{}; current[3] = (i % 2) << 19;
    const auto key = proposed_content_key(current);
    old_misses += i == 0 || current != prior;
    proposed_misses += i == 0 || key != prior_key;
    prior = current; prior_key = key;
  }
  CHECK(old_misses == 100);
  CHECK(proposed_misses == 1);
  std::cout << "AUDIT_SYNTHETIC requests=100 current_misses=" << old_misses
            << " proposed_misses=" << proposed_misses
            << " runtime_measurement=false\n";
}

TEST_CASE("Audit: existing 30 Hz clock preserves phase through jitter and suspension") {
  namespace limiter = gta4::frame_limiter;
  constexpr int64_t origin = 1000000;
  auto d = limiter::Plan({}, 30, origin);
  for (unsigned i = 1; i < 300; ++i) {
    auto previous = d.next_state.next_deadline_ns;
    d = limiter::Plan(d.next_state, 30, previous + (i % 2 ? 1000000 : -1000000));
    CHECK_FALSE(d.late_reset);
  }
  CHECK(d.next_state.next_deadline_ns == origin + 10000000000LL);
  const auto wake = d.next_state.next_deadline_ns + 5000000000LL;
  d = limiter::Plan(d.next_state, 30, wake);
  CHECK(d.late_reset);
  CHECK(d.wait_until_ns == 0);
  CHECK(d.next_state.next_deadline_ns > wake);
  const auto next = limiter::Plan(d.next_state, 30, wake + 1000);
  CHECK(next.should_wait(wake + 1000));
}
