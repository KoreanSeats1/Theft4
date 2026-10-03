#pragma once
#include <array>
#include <cstdint>
namespace theft4::render {
enum class HostProgram : uint32_t {
  DepthHandoff,SceneDepthHandoff,PackedDepthAlias,Resolve,ResolveMSAA,
  ResolveHDR,ResolveHDRMSAA,ResolveDepthMSAA,Present,SplitPostFx,SunShafts,
  SmaaEdgeLow,SmaaWeightLow,SmaaEdgeMedium,SmaaWeightMedium,SmaaEdgeHigh,SmaaWeightHigh,
  SmaaEdgeUltra,SmaaWeightUltra,SmaaNeighborhood,SmaaPresent,SmaaHardwarePresent,
  SmaaHardwareNeighborhood,Count
};
struct HostProgramInfo {const char* name;uint32_t constants,textures,multisampled;};
// Exact host ABI reflected from the shipped utility programs. The runtime
// validates the offline manifest against this table before enabling commands.
inline constexpr std::array<HostProgramInfo,uint32_t(HostProgram::Count)> kHostPrograms{{
  {"gta4_native_depth_handoff_ps",0,1,0},{"gta4_native_scene_depth_handoff_ps",4,1,0},
  {"gta4_native_packed_depth_alias_ps",64,3,0},{"gta4_native_resolve_convert_ps",64,1,0},
  {"gta4_native_resolve_convert_msaa_ps",64,1,1},{"gta4_native_resolve_convert_hdr_ps",64,1,0},
  {"gta4_native_resolve_convert_hdr_msaa_ps",64,1,1},{"gta4_native_resolve_depth_msaa_ps",64,1,1},
  {"gta4_native_hdr_present_ps",44,1,0},{"gta4_native_split_postfx_ps",80,15,0},
  {"gta4_native_sun_shafts_ps",80,7,0},{"smaa_edge_low_ps",16,1,0},{"smaa_weight_low_ps",16,7,0},
  {"smaa_edge_medium_ps",16,1,0},{"smaa_weight_medium_ps",16,7,0},{"smaa_edge_high_ps",16,1,0},
  {"smaa_weight_high_ps",16,7,0},{"smaa_edge_ultra_ps",16,1,0},{"smaa_weight_ultra_ps",16,7,0},
  {"smaa_neighborhood_ps",16,3,0},{"smaa_present_ps",44,3,0},
  {"smaa_hardware_present_ps",44,7,0},{"smaa_hardware_neighborhood_ps",16,7,0}
}};
}
