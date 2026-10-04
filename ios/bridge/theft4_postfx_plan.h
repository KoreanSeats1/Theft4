#pragma once
#include "theft4_frame_plan.h"
#include <functional>
#include <string_view>
namespace theft4::render {
using SurfaceOwner=std::shared_ptr<const Surface>;
using UtilityAllocator=std::function<SurfaceOwner(std::string_view,uint32_t,uint32_t,Format)>;
struct SplitConstants {
  std::array<int32_t,2> source{},destination{};
  uint32_t pass=0,depth_source=0;
  std::array<uint32_t,2> reserved{};
  std::array<float,4> projection{},distance{},blur{};
};
struct SunConstants {
  std::array<int32_t,2> source{},destination{};
  uint32_t pass=0,samples=24;
  float density=0,decay=0;
  std::array<float,4> screen{},color_and_sky_start{},sky_end{};
};
struct PresentConstants {
  int32_t source_width=0,source_height=0,destination_width=0,destination_height=0;
  float headroom=1;
  uint32_t output_mode=0,hdr_mode=0;
  float paper_white=203,peak=400,shoulder_start=0,shoulder_power=2.5;
};
static_assert(sizeof(SplitConstants)==80&&sizeof(SunConstants)==80&&sizeof(PresentConstants)==44);
struct SmaaLookups {std::shared_ptr<const Image> area,search;};
SmaaLookups MakeSmaaLookups(uint64_t& generation);
// These build the actual ordered utility passes used by the title producer.
// The caller supplies lifetime-owned, reusable targets and a speculative content
// journal. A later rejected frame must not commit either journal or allocation.
bool AppendSplitPostFx(FramePlan&,SurfaceContents&,const SurfaceOwner& scene,
    const HostFetch& depth,const HostFetch& mask,SplitConstants,const UtilityAllocator&,
    uint64_t& generation,std::string& error);
bool AppendSunShafts(FramePlan&,SurfaceContents&,const SurfaceOwner& scene,
    const HostFetch& depth,SunConstants,const UtilityAllocator&,uint64_t& generation,std::string& error);
bool AppendPresentation(FramePlan&,SurfaceContents&,HostFetch source,const SurfaceOwner& output,
    PresentConstants,int smaa_quality,const SmaaLookups&,const UtilityAllocator&,
    uint64_t& generation,std::string& error);
}
