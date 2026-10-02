#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <rex/ui/vulkan/api.h>

#include "postfx_resource_pool.h"
#include "present_constants.h"
#include "native_gpu_timing.h"

namespace rex::ui::vulkan {
class VulkanDevice;
}

namespace rex::graphics::gta4_native {

enum class SmaaQuality : uint32_t {
  kLow,
  kMedium,
  kHigh,
  kUltra,
  kCount,
};

SmaaQuality ParseSmaaQuality(std::string_view quality);

class SmaaPipeline {
 public:
  static constexpr uint32_t kDescriptorSetCount = 3;
  static constexpr uint32_t kCombinedImageSamplerDescriptorCount = 6;

  struct Output {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat format = VK_FORMAT_UNDEFINED;
    PostFxExtent extent{};
  };

  // Public only so the translation-unit helpers can create and destroy the
  // tightly owned Vulkan allocations; callers interact through Output.
  struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    PostFxExtent extent{};
    VkDeviceSize allocation_size = 0;
    uint32_t memory_type = UINT32_MAX;
  };

  struct MemoryUsage {
    VkDeviceSize extent_bytes = 0;
    VkDeviceSize lookup_bytes = 0;
    VkDeviceSize staging_bytes = 0;
    uint32_t extent_images = 0;
    uint32_t lookup_images = 0;
    uint32_t staging_buffers = 0;
  };

  struct Presentation {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    PostFxExtent extent{};
    NativePresentConstants constants{};
  };
  // The borrowed destination must already be in COLOR_ATTACHMENT_OPTIMAL.
  // A false return records no commands, so the caller may use the old chain.
  bool Record(VkCommandBuffer command_buffer, const ui::vulkan::VulkanDevice* device,
              VkDescriptorPool frame_descriptor_pool, VkPipelineCache pipeline_cache,
              VkImage source_image, VkImageView source_view, VkImageLayout& source_layout,
              PostFxExtent extent, SmaaQuality quality, Output& output,
              const NativeGpuTimingSink* timing = nullptr,
              const Presentation* presentation = nullptr);
  bool RequiresExtentResourceRecreation(PostFxExtent extent) const;
  void Destroy(const ui::vulkan::VulkanDevice* device);
  MemoryUsage QueryMemoryUsage() const;

 private:
  struct StagingBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize allocation_size = 0;
    uint32_t memory_type = UINT32_MAX;
  };

  bool EnsureStaticResources(const ui::vulkan::VulkanDevice* device,
                             VkPipelineCache pipeline_cache);
  bool EnsurePresentationResources(const ui::vulkan::VulkanDevice* device,
                                   VkPipelineCache cache, VkFormat format);
  bool EnsureExtentResources(const ui::vulkan::VulkanDevice* device, PostFxExtent extent,
                             bool needs_output);
  bool RecordLookupUpload(VkCommandBuffer command_buffer,
                          const ui::vulkan::VulkanDevice* device);
  void DestroyExtentResources(const ui::vulkan::VulkanDevice* device);

  Image edges_;
  Image weights_;
  Image output_;
  Image area_;
  Image search_;
  StagingBuffer area_staging_;
  StagingBuffer search_staging_;
  bool lookup_upload_recorded_ = false;

  VkSampler linear_sampler_ = VK_NULL_HANDLE;
  VkSampler point_sampler_ = VK_NULL_HANDLE;
  std::array<VkDescriptorSetLayout, kDescriptorSetCount> descriptor_set_layouts_{};
  std::array<VkPipelineLayout, kDescriptorSetCount> pipeline_layouts_{};
  std::array<VkPipeline, size_t(SmaaQuality::kCount)> edge_pipelines_{};
  std::array<VkPipeline, size_t(SmaaQuality::kCount)> weight_pipelines_{};
  VkPipeline neighborhood_pipeline_ = VK_NULL_HANDLE;
  VkPipeline presentation_pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout presentation_layout_ = VK_NULL_HANDLE;
  VkFormat presentation_format_ = VK_FORMAT_UNDEFINED;
};

}  // namespace rex::graphics::gta4_native
