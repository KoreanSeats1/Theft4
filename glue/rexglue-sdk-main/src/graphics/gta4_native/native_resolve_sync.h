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

}  // namespace rex::graphics::gta4_native
