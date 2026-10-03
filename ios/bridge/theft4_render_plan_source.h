#pragma once
// CPU frontend metadata boundary. No driver calls or graphics handles are needed.
// Metal runtime consumes theft4_render_plan.h after effective state is lowered.
#include "theft4_render_plan.h"
#include "native_pipeline_recipe.h"

namespace theft4::render::source {
Format PixelFormat(VkFormat format);
bool Pipeline(const rex::graphics::gta4_native::NativePipelineRecipe::Snapshot& source,
              render::Draw& draw,std::string& error);
bool DecodeSampler(const VkSamplerCreateInfo&,render::Sampler&,std::string& error);
struct SampledImageDescription {
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageViewType kind = VK_IMAGE_VIEW_TYPE_2D;
  VkComponentMapping components{};
  uint32_t width=0, height=0, depth=1, layers=1, levels=1;
};
struct TextureUploadMip {
  uint32_t level=0, width=0, height=0, depth=1;
  uint32_t base_array_layer=0, layer_count=1, buffer_row_length=0, buffer_image_height=0;
  uint64_t payload_offset=0, payload_size=0;
};
// Lower every upload plane transactionally. This computes immutable layout
// only; the caller attaches its shared byte owner after successful validation.
bool DecodeImageUpload(const SampledImageDescription&,std::span<const TextureUploadMip>,
                       uint64_t payload_bytes,render::Image&,std::string& error);
struct Block {uint32_t width=1,height=1,bytes=0;};
Block TextureBlock(Format format);
}
