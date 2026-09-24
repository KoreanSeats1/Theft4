#pragma once

#include <vulkan/vulkan_core.h>

namespace rex::graphics::gta4_native {

// Vulkan's Render Pass Multisample Resolve Operations specifies these scopes
// for all fixed-function attachment resolves, including depth/stencil formats:
// https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-resolve-operations
inline constexpr VkPipelineStageFlags kNativeAttachmentResolveStage =
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
inline constexpr VkAccessFlags kNativeAttachmentResolveRead = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
inline constexpr VkAccessFlags kNativeAttachmentResolveWrite = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

// The producer's ordinary depth/stencil accesses and retained STORE operation
// also participate in its outgoing barrier. They do not replace resolve access.
inline constexpr VkPipelineStageFlags kNativeDepthStencilResolveStages =
    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
    kNativeAttachmentResolveStage;
inline constexpr VkAccessFlags kNativeStoredDepthStencilResolveSourceAccess =
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    kNativeAttachmentResolveRead;

// A source that is already in the required read-only layout has completed the
// write-to-read transition at its prior use. A second read-to-read image
// barrier adds no visibility. Keep the barrier whenever destination storage
// might alias the source, or the layout still needs a transition.
inline bool CanElideNativeResolveSourceReadBarrier(VkImageLayout current_layout,
                                                   VkImageLayout required_layout,
                                                   bool distinct_storage) {
  const bool read_only_layout =
      required_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
      required_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  return distinct_storage && read_only_layout && current_layout == required_layout;
}

}  // namespace rex::graphics::gta4_native
