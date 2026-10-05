// Offline translation of the comparison renderer's exact host utility
// programs. These shaders do not use the guest draw/constant-bank ABI.
#include "spirv_msl.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include "../shaders/vulkan_spirv/fullscreen_cw_vs.h"
#include "depth_handoff_ps.h"
#include "scene_depth_handoff_ps.h"
#include "packed_depth_alias_ps.h"
#include "resolve_convert_ps.h"
#include "resolve_convert_msaa_ps.h"
#include "resolve_convert_hdr_ps.h"
#include "resolve_convert_hdr_msaa_ps.h"
#include "resolve_depth_msaa_ps.h"
#include "hdr_present_ps.h"
#include "split_postfx_ps.h"
#include "sun_shafts_ps.h"
#include "smaa/smaa_shaders.inc"
#include "smaa/smaa_present_ps.inc"
#include "smaa/smaa_hardware_ps.inc"
using Json=nlohmann::json;
namespace {
Json Export(const char* name,std::span<const uint32_t> code,const std::filesystem::path& root) {
  spirv_cross::CompilerMSL compiler(std::vector<uint32_t>(code.begin(),code.end()));
  const auto stage=compiler.get_execution_model();
  if(stage!=spv::ExecutionModelVertex&&stage!=spv::ExecutionModelFragment)
    throw std::runtime_error("Unexpected host shader stage");
  auto options=compiler.get_msl_options();options.platform=spirv_cross::CompilerMSL::Options::iOS;
  options.msl_version=spirv_cross::CompilerMSL::Options::make_msl_version(2,4);options.argument_buffers=false;
  compiler.set_msl_options(options);
  // Match the stock game conversion: Metal's positive viewport Y points
  // downward, while the comparison frontend uses Vulkan's negative viewport.
  auto common=compiler.get_common_options();common.vertex.flip_vert_y=true;compiler.set_common_options(common);
  const auto entries=compiler.get_entry_points_and_stages();
  if(entries.size()!=1)throw std::runtime_error("Ambiguous host shader entry point");
  compiler.rename_entry_point(entries.front().name,"theft4_host_shader",stage);
  auto resources=compiler.get_shader_resources();
  if(!resources.storage_buffers.empty()||!resources.storage_images.empty()||!resources.subpass_inputs.empty()||
     !resources.separate_images.empty()||!resources.separate_samplers.empty()||!resources.uniform_buffers.empty())
    throw std::runtime_error("Host shader interface requires an explicit lowering");
  Json textures=Json::array(),constants=Json::array();uint32_t index=0;
  for(const auto& image:resources.sampled_images) {
    const auto& type=compiler.get_type(image.type_id);
    if(!type.array.empty())throw std::runtime_error("Host shader descriptor arrays are unsupported");
    spirv_cross::MSLResourceBinding binding{};binding.stage=stage;
    binding.desc_set=compiler.get_decoration(image.id,spv::DecorationDescriptorSet);
    binding.binding=compiler.get_decoration(image.id,spv::DecorationBinding);
    binding.msl_texture=index;binding.msl_sampler=index;compiler.add_msl_resource_binding(binding);
    textures.push_back({{"set",binding.desc_set},{"binding",binding.binding},{"texture_index",index},
      {"sampler_index",index},{"name",image.name},{"dimension",uint32_t(type.image.dim)},
      {"multisampled",type.image.ms},{"arrayed",type.image.arrayed},{"depth",type.image.depth}});++index;
  }
  if(index>16||resources.push_constant_buffers.size()>1)throw std::runtime_error("Host shader interface budget exceeded");
  for(const auto& push:resources.push_constant_buffers) {
    spirv_cross::MSLResourceBinding binding{};binding.stage=stage;
    binding.desc_set=spirv_cross::kPushConstDescSet;binding.binding=spirv_cross::kPushConstBinding;
    binding.msl_buffer=0;compiler.add_msl_resource_binding(binding);
    constants.push_back({{"buffer_index",0},{"bytes",compiler.get_declared_struct_size(compiler.get_type(push.base_type_id))}});
  }
  auto source=compiler.compile();
  const bool resolve_specialization=std::string(name).find("gta4_native_resolve_convert_")==0;
  const bool present_specialization=std::string(name)=="gta4_native_hdr_present_ps"||
      std::string(name)=="smaa_present_ps"||std::string(name)=="smaa_hardware_present_ps";
  if(resolve_specialization) {
    const std::array<const char*,8> fields{"source_guest_sample_type","requested_guest_sample_type",
      "destination_guest_sample_type","sample_select","mode","physical_source_sample_type","physical_destination_sample_type","flags"};
    std::string declarations;
    for(size_t i=0;i<fields.size();++i) {
      const auto symbol="theft4_resolve_"+std::to_string(i);
      declarations+="constant uint "+symbol+" [[function_constant("+std::to_string(i+1)+")]];\n";
      const auto original="resolve_constants."+std::string(fields[i]);
      const auto replacement="(is_function_constant_defined("+symbol+") ? "+symbol+" : "+original+")";
      size_t at=0;while((at=source.find(original,at))!=std::string::npos){source.replace(at,original.size(),replacement);at+=replacement.size();}
    }
    const auto insertion=source.find("using namespace metal;");if(insertion==std::string::npos)throw std::runtime_error("Metal resolve namespace missing");
    source.insert(insertion+std::string("using namespace metal;").size(),"\n"+declarations);
  }
  if(present_specialization) {
    std::string declarations;
    const std::array<const char*,2> fields{"output_mode","hdr_mode"};
    for(size_t i=0;i<fields.size();++i) {
      const auto symbol="theft4_present_"+std::to_string(i);
      declarations+="constant uint "+symbol+" [[function_constant("+std::to_string(i+1)+")]];\n";
      const auto original="present_constants."+std::string(fields[i]);
      const auto replacement="(is_function_constant_defined("+symbol+") ? "+symbol+" : "+original+")";
      size_t at=0;while((at=source.find(original,at))!=std::string::npos){source.replace(at,original.size(),replacement);at+=replacement.size();}
    }
    const auto insertion=source.find("using namespace metal;");if(insertion==std::string::npos)throw std::runtime_error("Metal presentation namespace missing");
    source.insert(insertion+std::string("using namespace metal;").size(),"\n"+declarations);
  }
  std::ofstream out(root/(std::string(name)+".metal"));out<<source;
  if(!out)throw std::runtime_error("Could not save translated host shader");
  uint64_t hash=14695981039346656037ull;
  for(auto word:code)for(size_t b=0;b<4;++b){hash^=uint8_t(word>>(b*8));hash*=1099511628211ull;}
  return {{"resolve_specialization",resolve_specialization},{"present_specialization",present_specialization},{"name",name},{"stage",stage==spv::ExecutionModelVertex ? "vertex" : "fragment"},
    {"entry","theft4_host_shader"},{"spirv_words",code.size()},{"spirv_fnv1a64",hash},
    {"textures",textures},{"constants",constants}};
}
}
int main(int argc,char** argv) {
  if(argc!=2){std::cerr<<"Usage: metal_host_shader_export output\n";return 2;}
  std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
  struct Program {const char* name;std::span<const uint32_t> code;};
#define PROGRAM(x) Program{#x,x}
  const Program programs[]{PROGRAM(fullscreen_cw_vs),PROGRAM(gta4_native_depth_handoff_ps),
    PROGRAM(gta4_native_scene_depth_handoff_ps),PROGRAM(gta4_native_packed_depth_alias_ps),
    PROGRAM(gta4_native_resolve_convert_ps),PROGRAM(gta4_native_resolve_convert_msaa_ps),
    PROGRAM(gta4_native_resolve_convert_hdr_ps),PROGRAM(gta4_native_resolve_convert_hdr_msaa_ps),
    PROGRAM(gta4_native_resolve_depth_msaa_ps),PROGRAM(gta4_native_hdr_present_ps),
    PROGRAM(gta4_native_split_postfx_ps),PROGRAM(gta4_native_sun_shafts_ps),
    PROGRAM(smaa_edge_low_ps),PROGRAM(smaa_weight_low_ps),PROGRAM(smaa_edge_medium_ps),PROGRAM(smaa_weight_medium_ps),
    PROGRAM(smaa_edge_high_ps),PROGRAM(smaa_weight_high_ps),PROGRAM(smaa_edge_ultra_ps),PROGRAM(smaa_weight_ultra_ps),
    PROGRAM(smaa_neighborhood_ps),PROGRAM(smaa_present_ps),PROGRAM(smaa_hardware_present_ps),PROGRAM(smaa_hardware_neighborhood_ps)};
#undef PROGRAM
  Json report={{"schema",1},{"purpose","Host utility ABI; separate from the guest shader catalog"},
    {"programs",Json::array()},{"rejected",Json::array()}};
  std::ofstream manifest(root/"manifest.tsv");
  for(const auto& p:programs)try {
    auto entry=Export(p.name,p.code,root);manifest<<p.name<<'\t'<<entry["stage"].get<std::string>()<<'\n';
    report["programs"].push_back(std::move(entry));
  }catch(const std::exception& error){report["rejected"].push_back({{"name",p.name},{"error",error.what()}});}
  std::ofstream(root/"HOST_SHADER_MANIFEST.json")<<report.dump(2)<<'\n';
  std::cout<<"Host Metal shaders accepted="<<report["programs"].size()<<" rejected="<<report["rejected"].size()<<'\n';
  return report["rejected"].empty() ? 0 : 1;
}
