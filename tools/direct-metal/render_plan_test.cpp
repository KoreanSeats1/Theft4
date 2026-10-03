#include "theft4_draw_capture.h"
#include "theft4_render_plan_source.h"
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>
using namespace theft4::render;
namespace {
void Require(bool okay, const std::string& reason) { if (!okay) throw std::runtime_error(reason); }
using Recipe = rex::graphics::gta4_native::NativePipelineRecipe;
uint32_t driver_calls = 0;
VKAPI_ATTR VkResult VKAPI_CALL InspectDeferredDriver(
    VkDevice, VkPipelineCache, uint32_t count, const VkGraphicsPipelineCreateInfo* info,
    const VkAllocationCallbacks*, VkPipeline* pipeline) {
  ++driver_calls;
  Require(count == 1 && info->stageCount == 2 && info->layout == reinterpret_cast<VkPipelineLayout>(3),
          "Deferred driver lost its realized layout");
  Require(info->pStages[0].module == reinterpret_cast<VkShaderModule>(1) &&
          info->pStages[1].module == reinterpret_cast<VkShaderModule>(2), "Deferred driver lost modules");
  Require(info->pVertexInputState->pVertexBindingDescriptions[0].stride == 48 &&
          info->pVertexInputState->pVertexAttributeDescriptions[0].location == 3,
          "Prepared vertex layout borrowed expired source storage");
  Require(*static_cast<const uint32_t*>(info->pStages[1].pSpecializationInfo->pData) == 37 &&
          *info->pMultisampleState->pSampleMask == 5, "Deferred state lost alpha specialization or sample mask");
  Require(info->pDepthStencilState->front.reference == 0 &&
          info->pColorBlendState->pAttachments[0].srcColorBlendFactor == VK_BLEND_FACTOR_SRC_ALPHA,
          "Prepared state lost stencil canonicalization or blend factors");
  *pipeline = reinterpret_cast<VkPipeline>(4); return VK_SUCCESS;
}
Recipe CpuPipelineFixture() {
  uint32_t value = 37;
  VkSpecializationMapEntry entry{0, 0, sizeof(value)};
  VkSpecializationInfo specialization{1, &entry, sizeof(value), &value};
  std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
  for (size_t i = 0; i < 2; ++i) {
    stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
    stages[i].pName = "shaderMain";
  }
  stages[1].pSpecializationInfo = &specialization;
  VkVertexInputBindingDescription binding{0, 48, VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription attribute{3, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
  VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex.vertexBindingDescriptionCount = vertex.vertexAttributeDescriptionCount = 1;
  vertex.pVertexBindingDescriptions = &binding; vertex.pVertexAttributeDescriptions = &attribute;
  VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = viewport.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.lineWidth = 1;
  VkSampleMask mask = 5;
  VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  samples.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT; samples.pSampleMask = &mask;
  VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth.front.reference = 99;
  VkPipelineColorBlendAttachmentState attachment{};
  attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; attachment.colorWriteMask = 15;
  VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1; blend.pAttachments = &attachment;
  const auto& states = Recipe::DynamicStates();
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = states.size(); dynamic.pDynamicStates = states.data();
  VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
  VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &format;
  VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pNext = &rendering; info.stageCount = 2; info.pStages = stages.data();
  info.pVertexInputState = &vertex; info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewport; info.pRasterizationState = &raster;
  info.pMultisampleState = &samples; info.pDepthStencilState = &depth;
  info.pColorBlendState = &blend; info.pDynamicState = &dynamic;
  auto result = Recipe::Capture(info, value);
  Require(result.has_value(), "CPU pipeline capture incorrectly requires a graphics device or handles");
  result->data.shaders[0].title_hash = 0xDC923A87EEA87A86ull;
  result->data.shaders[1].title_hash = 0x949ED69300FB92B7ull;
  result->data.shaders[1].variant = 1;
  return *result;
}
Capture Fixture() {
  Capture c; c.frame = 17; c.command = 93; c.width = c.height = 64;
  auto& d = c.draw; d.pipeline.vertex.hash = 0xDC923A87EEA87A86ull;
  d.pipeline.fragment.hash = 0x949ED69300FB92B7ull;
  d.pipeline.colors[0] = Format::RGBA8Unorm;
  d.pipeline.attributes = {{0, 0, 0, VertexFormat::Float4}, {1, 16, 0, VertexFormat::Float4}};
  d.pipeline.streams[0] = {16, false}; d.pipeline.streams[16] = {16, true};
  auto constants = std::make_shared<Bytes>(); constants->generation = 9;
  constants->conversion = {UINT64_MAX, 2, 3, 4}; constants->value.resize(4096 + 3584 + 1056);
  d.constants = {Buffer{constants, 0, 4096}, Buffer{constants, 4096, 3584}, Buffer{constants, 7680, 1056}};
  auto vertex = std::make_shared<Bytes>(); vertex->generation = 11; vertex->value.resize(16 * 4);
  d.vertices[0] = {vertex, 0, 64}; d.vertices[16] = {vertex, 0, 32}; d.instances = 2;
  auto index = std::make_shared<Bytes>(); index->generation = 12; index->value.resize(16);
  const uint16_t indices[]{1, 2, 3}; std::memcpy(index->value.data() + 8, indices, 6);
  d.indices = {index, 8, 6}; d.index_count = 3; d.index_bytes = 2; d.base_vertex = -1;
  d.viewport = {0, 0, 64, 64, 0, 1}; d.scissor = {0, 0, 64, 64};
  auto image = std::make_shared<Image>(); image->format = Format::RGBA8Unorm;
  image->width = image->height = image->levels = 1; image->source = vertex;
  image->mips = {{0, 0, 1, 1, 1, 4, 4, 0, 4}};
  d.fetches[25] = {image, std::make_shared<Sampler>()};
  return c;
}
}
int main(int argc, char** argv) {
  try {
    Require(argc == 2, "render_plan_test needs a private output directory");
    const std::filesystem::path root(argv[1]); std::filesystem::create_directories(root);
    auto c = Fixture(); std::string error; Require(Validate(c, error), error);
    {
      auto prepared = CpuPipelineFixture();
      Require(prepared.Valid() && !prepared.layout && !prepared.modules[0] && !prepared.modules[1],
              "CPU pipeline unexpectedly contains a driver realization");
      VkPipeline handle = VK_NULL_HANDLE;
      Require(prepared.Create(InspectDeferredDriver, VK_NULL_HANDLE, VK_NULL_HANDLE, &handle) ==
              VK_ERROR_INITIALIZATION_FAILED && driver_calls == 0 && !handle,
              "Unrealized CPU pipeline reached the driver");
      Draw neutral;
      Require(source::Pipeline(prepared.data, neutral, error), error);
      Require(neutral.pipeline.fragment.variant == 1 && neutral.pipeline.fragment.specialization == 37 &&
              neutral.pipeline.streams[0].stride == 48 && neutral.pipeline.samples == 4 &&
              neutral.pipeline.sample_mask == 5 && neutral.pipeline.colors[0] == Format::BGRA8Unorm,
              "CPU pipeline cannot feed the neutral Metal contract without driver handles");
      prepared.modules = {reinterpret_cast<VkShaderModule>(1), reinterpret_cast<VkShaderModule>(2)};
      prepared.layout = reinterpret_cast<VkPipelineLayout>(3);
      Require(prepared.Create(InspectDeferredDriver, VK_NULL_HANDLE, VK_NULL_HANDLE, &handle) == VK_SUCCESS &&
              driver_calls == 1 && handle == reinterpret_cast<VkPipeline>(4),
              "Deferred driver realization did not preserve prepared CPU state");
    }
    const auto file = (root / "roundtrip.t4draw").string(); Require(WriteCapture(file, c, error), error);
    Capture loaded; Require(ReadCapture(file, loaded, error), error);
    Require(loaded.draw.pipeline == c.draw.pipeline && loaded.draw.base_vertex == -1 &&
            loaded.draw.pipeline.vertex.hash == 0xDC923A87EEA87A86ull, "Lost high shader hash or signed base vertex");
    Require(loaded.draw.constants[0].source == loaded.draw.constants[2].source &&
            loaded.draw.vertices[0].source == loaded.draw.fetches[25].image->source &&
            loaded.draw.constants[0].source->conversion[0] == UINT64_MAX,
            "Lost immutable resource aliasing or conversion identity");
    auto bad = c; bad.draw.base_vertex = -2; Require(!Validate(bad, error), "Negative effective index accepted");
    bad = c; bad.draw.vertices[0].length = 31; Require(!Validate(bad, error), "Actual index exceeds vertex view");
    bad = c; bad.draw.vertices[16].length = 16; Require(!Validate(bad, error), "Second instance reads short stream");
    bad = c; bad.draw.pipeline.depth = Format::RGBA8Unorm; Require(!Validate(bad, error), "Color in depth role accepted");
    bad = c; bad.draw.fetches[25].sampler = std::make_shared<Sampler>(Sampler{.max_lod_bits = std::bit_cast<uint32_t>(-1.f)});
    Require(!Validate(bad, error), "Invalid sampler clamp accepted");
    std::ifstream input(file, std::ios::binary); std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    auto json = nlohmann::json::from_cbor(bytes); json["draw"]["index_count"] = uint64_t(UINT32_MAX) + 1;
    bytes = nlohmann::json::to_cbor(json); const auto corrupt = root / "corrupt.t4draw";
    { std::ofstream output(corrupt, std::ios::binary); output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
    Require(!ReadCapture(corrupt.string(), loaded, error) && loaded.frame == c.frame,
            "Overflow capture accepted or failed read changed existing packet");
    rex::graphics::gta4_native::NativePipelineRecipe::Snapshot recipe{};
    recipe.stage_count = 2; recipe.shaders[0].title_hash = c.draw.pipeline.vertex.hash;
    recipe.shaders[1].title_hash = c.draw.pipeline.fragment.hash;
    recipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; recipe.rasterization.lineWidth = 1;
    recipe.samples = VK_SAMPLE_COUNT_1_BIT; recipe.sample_mask = ~0u;
    recipe.color_count = 1; recipe.color_formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
    recipe.color_attachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_A_BIT;
    recipe.depth_format = recipe.stencil_format = VK_FORMAT_D32_SFLOAT;
    Draw mapped; Require(source::Pipeline(recipe, mapped, error), error);
    Require(mapped.pipeline.colors[0] == Format::BGRA8Unorm && mapped.pipeline.blends[0].write_mask == 9 &&
            mapped.pipeline.depth == Format::Depth32Float && mapped.pipeline.stencil == Format::Invalid,
            "Frontend attachment or channel semantics lost");
    recipe.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    Require(!source::Pipeline(recipe, mapped, error), "Unexpanded fan accepted");
    Require(source::TextureBlock(Format::ASTC4x4).bytes == 16 && source::TextureBlock(Format::BC1Unorm).bytes == 8,
            "Compressed upload footprint differs");
    {
      VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
      s.minFilter=VK_FILTER_LINEAR;s.magFilter=VK_FILTER_NEAREST;s.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
      s.addressModeU=VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
      s.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;s.addressModeW=VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      s.anisotropyEnable=VK_TRUE;s.maxAnisotropy=8;s.minLod=2;s.maxLod=5;
      s.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
      Sampler decoded;Require(source::DecodeSampler(s,decoded,error),error);
      Require(decoded.min_linear&&!decoded.mag_linear&&decoded.mip_linear&&decoded.anisotropy==8&&
              decoded.address==std::array{Address::MirrorClampEdge,Address::ClampBorder,Address::MirrorRepeat}&&
              decoded.min_lod_bits==std::bit_cast<uint32_t>(2.f)&&decoded.max_lod_bits==std::bit_cast<uint32_t>(5.f)&&
              decoded.opaque_white_border,"CPU sampler policy changed during neutral lowering");
      const auto unchanged=decoded;
      const auto rejects=[&](VkSamplerCreateInfo bad) {
        Require(!source::DecodeSampler(bad,decoded,error)&&decoded==unchanged,
                "Unsupported sampler changed the preceding neutral state");
      };
      auto bad=s;bad.compareEnable=VK_TRUE;rejects(bad);
      bad=s;bad.unnormalizedCoordinates=VK_TRUE;rejects(bad);
      bad=s;bad.mipLodBias=0.5f;rejects(bad);
      bad=s;bad.maxAnisotropy=1.5f;rejects(bad);
      bad=s;bad.maxAnisotropy=32;rejects(bad);
      bad=s;bad.maxLod=1;rejects(bad);
      bad=s;bad.minFilter=VK_FILTER_CUBIC_EXT;rejects(bad);
      bad=s;bad.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;rejects(bad);
      bad=s;bad.addressModeU=VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;rejects(bad);
      bad=s;bad.pNext=&s;rejects(bad);
      bad=s;bad.flags=1;rejects(bad);
      s.anisotropyEnable=VK_FALSE;s.maxAnisotropy=0;
      s.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
      Require(source::DecodeSampler(s,decoded,error)&&decoded.anisotropy==1&&!decoded.opaque_white_border,
              "Disabled anisotropy incorrectly requires a populated driver limit");
    }
    {
      DrawCaptureRecorder recorder((root / "writer").string());
      Require(recorder.Submit(c, true), "Bounded writer rejected valid packet");
      Require(!recorder.Submit(c, true), "Repeated pipeline family recorded twice");
      recorder.Flush(); Capture written;
      Require(ReadCapture((root / "writer/draw-0.t4draw").string(), written, error), error);
    }
    {
      DrawCaptureRecorder recorder((root / "rejected-only").string());
      recorder.Reject("draw needs preceding GPU-produced image"); recorder.Flush();
      std::ifstream input(root / "rejected-only/CAPTURE_REPORT.json"); nlohmann::json report; input >> report;
      Require(report.at("written") == 0 && report.at("rejections").at("draw needs preceding GPU-produced image") == 1,
              "Rejected-only game capture lost its diagnostic report");
    }
    std::cout << "Draw contract: alias-preserving roundtrip, index and instance bounds, corrupted input, frontend mapping, bounded asynchronous writer passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
