#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vulkan/vulkan_core.h>

#include "native_fixed_function_policy.h"

namespace rex::graphics::gta4_native {

constexpr uint32_t kNativeDefaultVertexRecordSize = 16;

constexpr VkPrimitiveTopology NativePrimitiveTopology(uint32_t primitive_type) {
  switch (primitive_type) {
    case 1: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case 2: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case 3: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case 4:
    case 13: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case 5: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case 6:
    case 8: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    default: return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
  }
}

constexpr bool NativeEffectivePrimitiveRestart(VkPrimitiveTopology topology,
                                                bool guest_restart, bool moltenvk) {
  const bool strip = topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
                     topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
                     topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  return strip && (guest_restart || moltenvk);
}

constexpr uint32_t NativeVertexFormatSize(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SNORM:
    case VK_FORMAT_R16G16_UNORM:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32_UINT: return 4;
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SNORM:
    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32_UINT: return 8;
    case VK_FORMAT_R32G32B32_SFLOAT:
    case VK_FORMAT_R32G32B32_SINT:
    case VK_FORMAT_R32G32B32_UINT: return 12;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R32G32B32A32_UINT: return 16;
    default: return 0;
  }
}

constexpr bool NativeVertexBindingValid(uint32_t stride, uint32_t alignment,
                                        uint32_t maximum_stride) {
  return alignment && stride >= alignment && stride <= maximum_stride &&
         stride % alignment == 0;
}

constexpr bool NativeVertexAttributeFits(uint32_t offset, uint32_t size, uint32_t stride,
                                         bool beyond_stride) {
  return size && (beyond_stride || uint64_t(offset) + size <= stride);
}

// Instance rate keeps the existing zero record constant for all vertex indices.
// All native draws have instanceCount=1 and firstInstance=0.
constexpr uint32_t NativeDefaultVertexStride(uint32_t alignment, uint32_t maximum_stride) {
  if (!alignment) {
    return 0;
  }
  const uint64_t stride =
      ((uint64_t(kNativeDefaultVertexRecordSize) + alignment - 1) / alignment) * alignment;
  return stride <= maximum_stride ? uint32_t(stride) : 0;
}

struct NativeViewportSelection {
  VkViewport viewport{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
  bool empty = true;
};

// VkRenderingInfo VUIDs 07815/07816 apply even without attachments. Keep the
// logical and physical fallback extent identical so this cap does not scale
// the guest-authored viewport transform.
constexpr VkExtent2D BoundNativeAttachmentlessExtent(uint32_t width, uint32_t height,
                                                    uint32_t maximum_width,
                                                    uint32_t maximum_height) {
  return {std::min(width, maximum_width), std::min(height, maximum_height)};
}

template <typename DeviceLimits>
inline NativeViewportSelection SelectNativeViewport(
    const std::array<float, 6>& requested, float scale_x, float scale_y,
    uint32_t target_width, uint32_t target_height, const DeviceLimits& limits) {
  NativeViewportSelection result;
  for (float value : requested) {
    if (!std::isfinite(value)) {
      return result;
    }
  }
  if (!std::isfinite(scale_x) || !std::isfinite(scale_y) || scale_x <= 0.0f ||
      scale_y <= 0.0f || requested[2] <= 0.0f || requested[3] <= 0.0f) {
    return result;
  }
  VkViewport viewport{};
  // Clipping the viewport itself changes the NDC-to-framebuffer transform.
  // Keep the guest transform intact; the guest window scissor clips coverage.
  viewport.x = requested[0] * scale_x;
  viewport.y = requested[1] * scale_y;
  viewport.width = requested[2] * scale_x;
  viewport.height = requested[3] * scale_y;
  viewport.minDepth = std::clamp(requested[4], 0.0f, 1.0f);
  viewport.maxDepth = std::clamp(requested[5], 0.0f, 1.0f);
  if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
      !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
      viewport.width <= 0.0f || viewport.height <= 0.0f ||
      viewport.width > float(limits.maxViewportDimensions[0]) ||
      viewport.height > float(limits.maxViewportDimensions[1]) ||
      viewport.x < limits.viewportBoundsRange[0] ||
      viewport.y < limits.viewportBoundsRange[0] ||
      double(viewport.x) + viewport.width > limits.viewportBoundsRange[1] ||
      double(viewport.y) + viewport.height > limits.viewportBoundsRange[1] ||
      viewport.x >= float(target_width) || viewport.y >= float(target_height) ||
      double(viewport.x) + viewport.width <= 0.0 ||
      double(viewport.y) + viewport.height <= 0.0) {
    return result;
  }
  result.viewport = viewport;
  result.empty = false;
  return result;
}

inline float NativeResolutionDepthBiasScale(float scale_x, float scale_y) {
  return std::max(scale_x, scale_y);
}

struct NativeBlendConstantSelection {
  std::array<float, 4> constants{};
  bool representable = true;
};

constexpr uint32_t NativeColorFormatComponentMask(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R32_SFLOAT: return 0x1u;
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32G32_SFLOAT: return 0x3u;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 0xFu;
    default: return 0;
  }
}

constexpr uint32_t NativeBlendWriteMask(
    uint32_t write_mask, const std::array<VkFormat, kNativeFixedRenderTargetCount>& formats) {
  uint32_t result = 0;
  for (uint32_t target = 0; target < kNativeFixedRenderTargetCount; ++target) {
    result |= (NativeColorWriteMaskForTarget(write_mask, target) &
               NativeColorFormatComponentMask(formats[target])) << (target * 4u);
  }
  return result;
}

inline NativeBlendConstantSelection SelectNativeBlendConstants(
    const std::array<uint32_t, kNativeFixedRenderTargetCount>& controls,
    uint32_t write_mask, const std::array<float, 4>& constants, bool constant_alpha_supported) {
  NativeBlendConstantSelection result{constants, true};
  if (constant_alpha_supported) {
    return result;
  }
  uint32_t alpha_channels = 0;
  uint32_t color_channels = 0;
  for (uint32_t target = 0; target < kNativeFixedRenderTargetCount; ++target) {
    const NativeBlendControlState blend = DecodeNativeBlendControl(controls[target]);
    // MIN/MAX ignore factors. Alpha-channel factors read the unchanged alpha.
    if (blend.color_operation == 2 || blend.color_operation == 3) {
      continue;
    }
    const uint32_t channels = NativeColorWriteMaskForTarget(write_mask, target) & 0x7u;
    for (uint32_t factor : {blend.source_color, blend.destination_color}) {
      if (factor == 14 || factor == 15) {
        alpha_channels |= channels;
      } else if (factor == 12 || factor == 13) {
        color_channels |= channels;
      }
    }
  }
  for (uint32_t channel = 0; channel < 3; ++channel) {
    if (!(alpha_channels & (1u << channel))) {
      continue;
    }
    if ((color_channels & (1u << channel)) && constants[channel] != constants[3]) {
      result.representable = false;
      return result;
    }
    result.constants[channel] = constants[3];
  }
  return result;
}

}  // namespace rex::graphics::gta4_native
