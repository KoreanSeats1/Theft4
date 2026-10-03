#pragma once
// Comparison-frontend boundary only. Metal runtime code never includes this
// header: it consumes theft4_render_plan.h after effective state is lowered.
#include "theft4_render_plan.h"
#include "native_pipeline_recipe.h"

namespace theft4::render::source {
Format PixelFormat(VkFormat format);
bool Pipeline(const rex::graphics::gta4_native::NativePipelineRecipe::Snapshot& source,
              render::Draw& draw,std::string& error);
bool DecodeSampler(const VkSamplerCreateInfo&,render::Sampler&,std::string& error);
struct Block {uint32_t width=1,height=1,bytes=0;};
Block TextureBlock(Format format);
}
