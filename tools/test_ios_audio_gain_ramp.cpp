// clang++ -std=c++20 -O3 -ffp-model=strict tools/test_ios_audio_gain_ramp.cpp -o /tmp/theft4-gain-test
#include "../ios/bridge/theft4_audio_gain_ramp.h"

#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <random>

using Block = std::array<uint8_t, 1024>;
using Vector = std::array<uint8_t, 16>;

float Load(const uint8_t* p) {
  uint32_t u;
  std::memcpy(&u, p, 4);
  return std::bit_cast<float>(__builtin_bswap32(u));
}
void Store(uint8_t* p, float f) {
  uint32_t u = __builtin_bswap32(std::bit_cast<uint32_t>(f));
  std::memcpy(p, &u, 4);
}

// Scalar transcription of the four independent gain registers in the actual
// generated sub_82199BC8, with separate multiply/add and float32 operations.
void Reference(Block& dst, const Block& src, const Vector& start, const Vector& step) {
  for (size_t lane = 0; lane < 4; ++lane) {
    const float s = Load(step.data() + lane * 4);
    const float s2 = s + s, s3 = s2 + s, s4 = s2 + s2;
    const float initial = Load(start.data() + lane * 4);
    float gain[4] = {initial, initial + s, initial + s2, initial + s3};
    for (size_t group = 0; group < 16; ++group) {
      for (size_t vector = 0; vector < 4; ++vector) {
        const size_t offset = group * 64 + vector * 16 + lane * 4;
        const float product = Load(src.data() + offset) * gain[vector];
        Store(dst.data() + offset, product + Load(dst.data() + offset));
        gain[vector] = gain[vector] + s4;
      }
    }
  }
}

int main() {
  std::mt19937 rng(0x82199BC8);
  std::uniform_real_distribution<float> sample(-1.f, 1.f);
  for (int test = 0; test < 20000; ++test) {
    alignas(16) Block dst{}, src{};
    alignas(16) Vector start{}, step{};
    for (size_t i = 0; i < 1024; i += 4) {
      Store(dst.data() + i, sample(rng));
      Store(src.data() + i, sample(rng));
    }
    for (size_t i = 0; i < 16; i += 4) {
      Store(start.data() + i, sample(rng));
      Store(step.data() + i, test % 3 ? sample(rng) * .01f : 0.f);
    }
    const bool in_place = (test % 2) != 0;
    if (in_place) src = dst;
    Block expected = dst;
    Reference(expected, src, start, step);
    theft4::audio::MixGainRamp(dst.data(), in_place ? dst.data() : src.data(),
                               start.data(), step.data());
    if (std::memcmp(dst.data(), expected.data(), dst.size()) != 0) {
      std::printf("FAIL case %d (in_place=%d)\n", test, in_place);
      return 1;
    }
  }
  std::puts("PASS: 20000 byte-exact gain ramp cases (zero/nonzero ramps, disjoint/in-place)");
}
