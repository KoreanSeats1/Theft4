#pragma once
#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace rex::graphics::gta4_native {
// Exact title format IDs, including the format37 float-pair helper targets.
constexpr VkFormat NativeSurfaceFormat(uint32_t raw_format,bool depth) {
  if(depth)return raw_format==0x1A220197 ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_UNDEFINED;
  switch(raw_format) {
    case 0x18280186:return VK_FORMAT_R8G8B8A8_UNORM;
    case 0x1A2201BF:return VK_FORMAT_R16G16B16A16_SFLOAT;
    case 0x2DA2ABA4:return VK_FORMAT_R32_SFLOAT;
    case 0x2D22AB9F:case 0x2D20AB8D:return VK_FORMAT_R16G16_SFLOAT;
    case 0x2D22ABA5:return VK_FORMAT_R32G32_SFLOAT;
    default:return VK_FORMAT_UNDEFINED;
  }
}
}
