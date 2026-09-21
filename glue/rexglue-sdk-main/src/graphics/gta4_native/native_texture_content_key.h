#pragma once

#include <array>
#include <cstdint>

#include <rex/graphics/xenos.h>

namespace rex::graphics::gta4_native {

// Ignore only sampling fields, never image layout or conversion fields. In
// particular, CaptureTextureResource decodes only mip_min_level..mip_max_level,
// so those eight bits remain part of the content identity. Unknown bits are
// retained. Each draw captures its own fetch for GetOrCreateSampler.
inline std::array<uint32_t, 6> NativeTextureContentKey(
    const xenos::xe_gpu_texture_fetch_t& fetch, bool ignore_sampling) {
  std::array<uint32_t, 6> words{fetch.dword_0, fetch.dword_1, fetch.dword_2,
                              fetch.dword_3, fetch.dword_4, fetch.dword_5};
  if (ignore_sampling) {
    // Clamp XYZ; min/mag/mip/aniso/arbitrary filters; volume filters, aniso
    // walks and LOD bias; border color, tri clamp and aniso bias.
    words[0] &= ~(0x1FFu << 10);
    words[3] &= ~(0xFFFu << 19);
    words[4] &= ~(3u | (3u << 10) | (0x3FFu << 12));
    words[5] &= ~(3u | (3u << 3) | (15u << 5));
  }
  return words;
}

inline bool NativeTextureContentMatches(const xenos::xe_gpu_texture_fetch_t& a,
                                        const xenos::xe_gpu_texture_fetch_t& b,
                                        bool ignore_sampling) {
  return NativeTextureContentKey(a, ignore_sampling) ==
         NativeTextureContentKey(b, ignore_sampling);
}

}  // namespace rex::graphics::gta4_native
