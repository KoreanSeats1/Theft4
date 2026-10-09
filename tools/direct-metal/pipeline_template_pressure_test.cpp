#include "native_incremental_owner_cache.h"
#include "theft4_render_plan.h"
#include <vulkan/vulkan_core.h>
#include <rex/graphics/gta4_native/title_commands.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#define XXH_INLINE_ALL
#include <xxhash.h>
using namespace rex::graphics::gta4_native;
struct Gta4NativeGraphicsSystem {
#include "pipeline-template-key.inc"
};
#include "pipeline-template-hash.inc"
using Key=Gta4NativeGraphicsSystem::NativePipelineKey;
using Hash=Gta4NativeGraphicsSystem::NativePipelineKeyHash;
struct Entry{theft4::render::Pipeline pipeline;std::shared_ptr<const uint64_t> recipe;};
struct Count{size_t operator()(const Entry&)const{return 1;}};
using Cache=NativeIncrementalOwnerCache<Key,Entry,Count,Hash>;
static Key MakeKey(uint64_t id){Key k;k.vertex_shader_hash=id;k.pixel_shader_hash=17;k.vertex_strides[0]=16;k.color_formats[0]=VK_FORMAT_R8G8B8A8_UNORM;return k;}
static Entry Make(uint64_t id){Entry e;e.pipeline.vertex.hash=id;e.pipeline.colors[0]=theft4::render::Format::RGBA8Unorm;
 e.pipeline.attributes={{0,0,0,theft4::render::VertexFormat::Float4}};e.recipe=std::make_shared<const uint64_t>(id);return e;}
int main(int argc,char** argv){
  assert(argc==2);Cache cache;std::unordered_map<Key,Entry,Hash> old;
  uint64_t old_misses=0,new_misses=0,evictions=0,checksum=0;
  const auto visit=[&](uint64_t id){auto key=MakeKey(id);auto prior=old.find(key);
    if(prior==old.end()){++old_misses;if(old.size()>=4096)old.clear();old.emplace(key,Make(id));}
    auto it=cache.find(key);if(it==cache.end()){
      ++new_misses;if(cache.size()>=4096){assert(cache.EvictOldest());++evictions;}
      cache.Store(key,Make(id));it=cache.find(key);
    }else cache.Touch(it);
    assert(it->second.pipeline==Make(id).pipeline&&*it->second.recipe==id);
    checksum+=*it->second.recipe;assert(cache.size()<=4096&&cache.bytes()==cache.size());
  };
  for(size_t i=1024;i<4096;++i)visit(i);for(size_t i=0;i<1024;++i)visit(i);
  const auto pinned=cache.find(MakeKey(0))->second;
  for(size_t turn=0;turn<25;++turn){for(size_t i=0;i<175;++i)visit(4096+turn*175+i);for(size_t i=0;i<1024;++i)visit(i);}
  assert(new_misses==4096+25*175&&old_misses>new_misses&&evictions==25*175);
  // Per-field equality still separates fixed-function variants of one shader.
  auto depth=MakeKey(1);depth.depth_write_enable=1;cache.Store(depth,Make(991));assert(*cache.find(depth)->second.recipe==991);
  assert(*cache.find(MakeKey(1))->second.recipe==1);cache.clear();assert(*pinned.recipe==0&&pinned.pipeline==Make(0).pipeline);
  nlohmann::json report{{"ceiling",4096},{"turns",25},{"new_variants_per_turn",175},{"warm_set",1024},
   {"baseline_template_builds",old_misses},{"incremental_template_builds",new_misses},{"cold_evictions",evictions},
   {"pipeline_descriptor_parity",true},{"escaped_recipe_lifetime",true},{"checksum",checksum},
   {"scope","Synthetic pressure workload using production key/hash/cache; not proof of device saturation or FPS"}};
  // Compare only warm lookup/recency work, with actual key/hash and no
  // descriptor construction, draw submission, source allocation or GPU timing.
  Cache warm_cache;std::unordered_map<Key,Entry,Hash> warm_old;
  std::vector<Key> keys;for(size_t i=0;i<4096;++i){auto key=MakeKey(i);keys.push_back(key);warm_cache.Store(key,Make(i));warm_old.emplace(key,Make(i));}
  std::vector<double> baseline_ns,candidate_ns;uint64_t warm_checksum=0;
  for(size_t trial=0;trial<31;++trial){
    for(size_t step=0;step<2;++step){const bool incremental=((step+trial)&1);const auto start=std::chrono::steady_clock::now();
      for(size_t i=0;i<16384;++i){const auto& key=keys[(i*137)%keys.size()];
        if(incremental){auto it=warm_cache.find(key);assert(it!=warm_cache.end());warm_cache.Touch(it);warm_checksum+=*it->second.recipe;}
        else {auto it=warm_old.find(key);assert(it!=warm_old.end());warm_checksum+=*it->second.recipe;}}
      const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/16384;
      (incremental?candidate_ns:baseline_ns).push_back(ns);
    }
  }
  std::sort(baseline_ns.begin(),baseline_ns.end());std::sort(candidate_ns.begin(),candidate_ns.end());
  report["warm_lookup"]={{"baseline_median_ns",baseline_ns[15]},{"candidate_median_ns",candidate_ns[15]},
    {"checksum",warm_checksum},{"scope","Actual-key lookup/recency microbenchmark only; not game frame savings"}};
  std::ofstream(argv[1])<<report.dump(2)<<'\n';
}
