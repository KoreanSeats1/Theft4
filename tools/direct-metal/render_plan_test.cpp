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
      DrawCaptureRecorder recorder((root / "writer").string());
      Require(recorder.Submit(c, true), "Bounded writer rejected valid packet");
      Require(!recorder.Submit(c, true), "Repeated pipeline family recorded twice");
      recorder.Flush(); Capture written;
      Require(ReadCapture((root / "writer/draw-0.t4draw").string(), written, error), error);
    }
    std::cout << "Draw contract: alias-preserving roundtrip, index and instance bounds, corrupted input, frontend mapping, bounded asynchronous writer passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
