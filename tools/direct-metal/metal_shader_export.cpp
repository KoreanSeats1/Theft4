// Offline conversion only. Game draws use the generated Metal libraries and
// direct buffer/texture slots; SPIRV-Cross and Vulkan are not runtime dependencies.
#include "shader_cache.h"
#include "smolv.h"
#include "spirv_msl.hpp"
#include "native_masked_constants.h"
#include <zstd.h>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

std::vector<uint32_t> ApplyMetalShaderColorContract(std::vector<uint32_t> input);
std::vector<uint32_t> LowerMetalDescriptorSlots(std::vector<uint32_t> input,
    const std::map<uint32_t,uint32_t>& resources, uint32_t mask);
namespace {
std::string Replace(std::string text, const std::string& from, const std::string& to) {
  size_t p = 0;
  while ((p = text.find(from, p)) != std::string::npos) {
    text.replace(p, from.size(), to); p += to.size();
  }
  return text;
}
std::string Hash(uint64_t hash) {
  std::ostringstream s; s << std::hex << std::setw(16) << std::setfill('0') << hash;
  return s.str();
}
struct Binding { uint32_t kind, slot, index; };
struct Export {
  std::string source, inputs;
  std::vector<Binding> bindings;
  bool vertex = false;
  uint32_t fp_flags = 0;
  std::array<uint32_t,3> constant_bytes{4096,3584,1056};
};
Export Convert(std::vector<uint32_t> code, uint32_t used_mask, bool negative_one_to_one = false) {
  // Descriptor and color-output lowering leave the VS/PS bank reads intact.
  // The generated color epilogue reads only the complete shared bank.
  const auto constant_usage=rex::graphics::gta4_native::ReflectNativeConstantUsage(code);
  spirv_cross::CompilerMSL probe(code);
  const auto stage = probe.get_execution_model();
  if (stage != spv::ExecutionModelVertex && stage != spv::ExecutionModelFragment)
    throw std::runtime_error("unsupported shader stage");
  if (negative_one_to_one && stage != spv::ExecutionModelVertex)
    throw std::runtime_error("clip conversion requires a vertex shader");
  auto original_resources = probe.get_shader_resources();
  std::map<uint32_t,uint32_t> descriptor_resources;
  for (const auto& resource : original_resources.separate_images)
    descriptor_resources[resource.id] = probe.get_decoration(resource.id, spv::DecorationDescriptorSet);
  for (const auto& resource : original_resources.separate_samplers)
    descriptor_resources[resource.id] = probe.get_decoration(resource.id, spv::DecorationDescriptorSet);
  code = LowerMetalDescriptorSlots(std::move(code), descriptor_resources, used_mask);
  if (stage == spv::ExecutionModelFragment) code = ApplyMetalShaderColorContract(std::move(code));
  spirv_cross::CompilerMSL compiler(std::move(code));
  auto resources = compiler.get_shader_resources();
  if (resources.push_constant_buffers.size() != 1)
    throw std::runtime_error("expected one guest constant block");
  const auto& push = resources.push_constant_buffers.front();
  const auto& bank_type = compiler.get_type(push.base_type_id);
  if (bank_type.member_types.size() != 3)
    throw std::runtime_error("unexpected guest constant bank schema");
  for (uint32_t i = 0; i < 3; ++i) {
    const auto& member = compiler.get_type(bank_type.member_types[i]);
    if (member.width != 64 || member.vecsize != 1 ||
        compiler.type_struct_member_offset(bank_type, i) != i * 8)
      throw std::runtime_error("unexpected guest constant bank layout");
  }
  compiler.set_name(push.base_type_id, "Theft4GuestBanks");
  compiler.set_name(push.id, "guestBanks");
  compiler.set_member_name(push.base_type_id, 0, "bankVS");
  compiler.set_member_name(push.base_type_id, 1, "bankPS");
  compiler.set_member_name(push.base_type_id, 2, "bankShared");
  compiler.rename_entry_point(compiler.get_entry_points_and_stages().front().name,
                             "theft4_shader", stage);
  auto options = compiler.get_msl_options();
  options.platform = spirv_cross::CompilerMSL::Options::iOS;
  options.msl_version = spirv_cross::CompilerMSL::Options::make_msl_version(2, 4);
  options.argument_buffers = false;
  options.enable_clip_distance_user_varying = false;
  compiler.set_msl_options(options);
  auto common = compiler.get_common_options();
  // DXC's Vulkan shaders invert Y. Metal's viewport uses the original game
  // clip-space convention, so undo that conversion at the vertex entry point.
  common.vertex.flip_vert_y = true;
  // CompilerMSL::emit_fixup converts [-w,w] to [0,w] as (z+w)/2,
  // preserving homogeneous W. CompilerGLSL uses the opposite conversion.
  common.vertex.fixup_clipspace = negative_one_to_one;
  compiler.set_common_options(common);
  const uint32_t count = std::popcount(used_mask);
  if (count > 16) throw std::runtime_error("more than 16 samplers in one stage");
  Export result; result.vertex = stage == spv::ExecutionModelVertex;
  if(constant_usage.known)for(size_t i=0;i<2;++i)
    result.constant_bytes[i]=uint32_t(rex::graphics::gta4_native::NativeMaskedConstantExtent(constant_usage.banks[i]));
  uint32_t texture_index = 0;
  for (auto& image : resources.separate_images) {
    const uint32_t kind = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    if (kind > 3 || !count) throw std::runtime_error("invalid texture descriptor contract");
    spirv_cross::MSLResourceBinding b{};
    b.stage = stage; b.desc_set = kind;
    b.binding = compiler.get_decoration(image.id, spv::DecorationBinding);
    b.count = count; b.msl_texture = texture_index;
    compiler.add_msl_resource_binding(b);
    for (uint32_t slot = 0, rank = 0; slot < 26; ++slot) if (used_mask & (1u << slot))
      result.bindings.push_back({kind, slot, texture_index + rank++});
    texture_index += count;
  }
  if (texture_index > 31) throw std::runtime_error("more than 31 direct textures in one stage");
  for (auto& sampler : resources.separate_samplers) {
    if (compiler.get_decoration(sampler.id, spv::DecorationDescriptorSet) != 4 || !count)
      throw std::runtime_error("invalid sampler descriptor contract");
    spirv_cross::MSLResourceBinding b{};
    b.stage = stage; b.desc_set = 4;
    b.binding = compiler.get_decoration(sampler.id, spv::DecorationBinding);
    b.count = count; b.msl_sampler = 0;
    compiler.add_msl_resource_binding(b);
    for (uint32_t slot = 0, rank = 0; slot < 26; ++slot) if (used_mask & (1u << slot))
      result.bindings.push_back({4, slot, rank++});
  }
  spirv_cross::MSLResourceBinding p{};
  p.stage = stage; p.desc_set = spirv_cross::kPushConstDescSet;
  p.binding = spirv_cross::kPushConstBinding; p.msl_buffer = 7;
  compiler.add_msl_resource_binding(p);
  for (const auto& input : resources.stage_inputs) {
    const auto& type = compiler.get_type(input.type_id);
    result.inputs += std::to_string(compiler.get_decoration(input.id, spv::DecorationLocation))
        + ":" + std::to_string(type.basetype) + ":" + std::to_string(type.vecsize) + ",";
  }
  std::string source = compiler.compile();
  result.fp_flags=compiler.get_fp_fast_math_flags(true);
  const std::string parameter = "constant Theft4GuestBanks& guestBanks [[buffer(7)]]";
  if (source.find(parameter) == std::string::npos)
    throw std::runtime_error("unexpected Metal guest bank parameter");
  source = Replace(source, parameter,
    "const device uchar* vertexBank [[buffer(0)]], const device uchar* pixelBank [[buffer(1)]], "
    "const device uchar* sharedBank [[buffer(2)]]");
  source = Replace(source, "constant Theft4GuestBanks&", "thread const Theft4GuestBanks&");
  auto entry = source.find("theft4_shader(");
  auto body = source.find('{', entry);
  if (entry == std::string::npos || body == std::string::npos)
    throw std::runtime_error("missing Metal entry point");
  // Preserve integer address arithmetic inside the shader, deriving its bases
  // from ordinary Metal buffer parameters. The host never supplies GPU addresses.
  source.insert(body + 1, "\n    const Theft4GuestBanks guestBanks{reinterpret_cast<ulong>(vertexBank), "
      "reinterpret_cast<ulong>(pixelBank), reinterpret_cast<ulong>(sharedBank)};\n");
  result.source = std::move(source);
  return result;
}
}
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) { std::cerr << "Usage: metal_shader_export output [filename-filter]\n"; return 2; }
  const std::filesystem::path root(argv[1]);
  std::filesystem::create_directories(root);
  std::vector<uint8_t> cache(g_spirvCacheDecompressedSize);
  if (ZSTD_decompress(cache.data(), cache.size(), g_compressedSpirvCache,
      g_spirvCacheCompressedSize) != cache.size()) throw std::runtime_error("cache decode");
  std::ofstream manifest(root / "manifest.tsv"), errors(root / "rejections.tsv"), math(root / "shader_math.tsv");
  size_t accepted = 0, rejected = 0;
  for (size_t i = 0; i < g_shaderCacheEntryCount; ++i) {
    const auto& e = g_shaderCacheEntries[i];
    if (argc == 3 && std::string(e.filename).find(argv[2]) == std::string::npos) continue;
    auto convert = [&](uint32_t offset, uint32_t size, const char* suffix) {
      if (!size) return;
      const std::string name = Hash(e.hash) + suffix;
      try {
        if (offset > cache.size() || size > cache.size() - offset) throw std::runtime_error("cache bounds");
        size_t bytes = smolv::GetDecodedBufferSize(cache.data() + offset, size);
        if (!bytes || bytes % 4) throw std::runtime_error("module size");
        std::vector<uint32_t> code(bytes / 4);
        if (!smolv::Decode(cache.data() + offset, size, code.data(), bytes)) throw std::runtime_error("module decode");
        const auto result = Convert(code, e.usedTextureMask);
        const auto write = [&](const Export& output, const std::string& identity) {
          std::ofstream source(root / (identity + ".metal"));
          if (!(source << output.source)) throw std::runtime_error("source write");
          manifest << identity << '\t' << (output.vertex ? "vertex" : "fragment") << '\t'
            << e.usedTextureMask << '\t' << e.specConstantsMask << '\t' << e.filename << '\t'
            << output.inputs << '\t';
          for (auto b : output.bindings) manifest << b.kind << ':' << b.slot << ':' << b.index << ',';
          manifest << '\t' << output.constant_bytes[0] << ':' << output.constant_bytes[1] << ':' << output.constant_bytes[2] << '\n';
          // Match MoltenVK's on-demand compiler policy from the original SPIR-V
          // permissions. Legacy NoContraction operations keep their generated
          // precise helpers independently of the module's permitted math mode.
          const uint32_t relaxed=spv::FPFastMathModeNSZMask|spv::FPFastMathModeAllowRecipMask|
              spv::FPFastMathModeAllowReassocMask|spv::FPFastMathModeAllowContractMask;
          const uint32_t finite=spv::FPFastMathModeNotNaNMask|spv::FPFastMathModeNotInfMask;
          const auto mode=(output.fp_flags&relaxed)!=relaxed ? "safe" :
              (output.fp_flags&finite)==finite ? "fast" : "relaxed";
          math<<identity<<'\t'<<output.fp_flags<<'\t'<<mode<<'\t'<<(std::string(mode)=="fast" ? "fast" : "precise")<<'\n';
          ++accepted;
        };
        write(result, name);
        if (result.vertex) write(Convert(std::move(code), e.usedTextureMask, true), name + "-clip-neg");
      } catch (const std::exception& error) {
        errors << name << '\t' << e.filename << '\t' << error.what() << '\n'; ++rejected;
      }
    };
    convert(e.spirvOffset, e.spirvSize, "");
    convert(e.lateSpirvOffset, e.lateSpirvSize, "-late");
  }
  std::cout << "Metal sources accepted=" << accepted << " rejected=" << rejected << '\n';
  return accepted && !rejected ? 0 : 1;
}
