#pragma once

#include <arm_neon.h>
#include <cstddef>
#include <cstdint>

namespace theft4::audio {

inline float32x4_t LoadBigEndianFloat4(const uint8_t* source) {
  return vreinterpretq_f32_u8(vrev32q_u8(vld1q_u8(source)));
}

inline void StoreBigEndianFloat4(uint8_t* destination, float32x4_t value) {
  vst1q_u8(destination, vrev32q_u8(vreinterpretq_u8_f32(value)));
}

// Aligned, non-overlapping buffers (or exact in-place operation), 256 floats.
// Match sub_82199BC8's four independent accumulators, not a sequential ramp:
// repeated +step and repeated +(4*step) are NOT bit-equivalent in float32.
inline void MixGainRamp(uint8_t* destination, const uint8_t* source,
                        const uint8_t* initial_gain, const uint8_t* gain_step) {
#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
  const auto step = LoadBigEndianFloat4(gain_step);
  const auto step2 = vaddq_f32(step, step);
  const auto step3 = vaddq_f32(step2, step);
  const auto step4 = vaddq_f32(step2, step2);
  auto gain0 = LoadBigEndianFloat4(initial_gain);
  auto gain1 = vaddq_f32(gain0, step);
  auto gain2 = vaddq_f32(gain0, step2);
  auto gain3 = vaddq_f32(gain0, step3);
  for (size_t offset = 0; offset < 1024; offset += 64) {
    const auto out0 = vaddq_f32(vmulq_f32(LoadBigEndianFloat4(source + offset), gain0),
                               LoadBigEndianFloat4(destination + offset));
    const auto out1 = vaddq_f32(vmulq_f32(LoadBigEndianFloat4(source + offset + 16), gain1),
                               LoadBigEndianFloat4(destination + offset + 16));
    const auto out2 = vaddq_f32(vmulq_f32(LoadBigEndianFloat4(source + offset + 32), gain2),
                               LoadBigEndianFloat4(destination + offset + 32));
    const auto out3 = vaddq_f32(vmulq_f32(LoadBigEndianFloat4(source + offset + 48), gain3),
                               LoadBigEndianFloat4(destination + offset + 48));
    StoreBigEndianFloat4(destination + offset, out0);
    StoreBigEndianFloat4(destination + offset + 16, out1);
    StoreBigEndianFloat4(destination + offset + 32, out2);
    StoreBigEndianFloat4(destination + offset + 48, out3);
    gain0 = vaddq_f32(gain0, step4);
    gain1 = vaddq_f32(gain1, step4);
    gain2 = vaddq_f32(gain2, step4);
    gain3 = vaddq_f32(gain3, step4);
  }
}

}  // namespace theft4::audio
