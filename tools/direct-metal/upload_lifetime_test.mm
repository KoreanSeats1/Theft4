#include "theft4_metal_resources.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
using namespace theft4::metal;
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  @autoreleasepool {
    Renderer renderer(2);assert(renderer.Ready());ResourceCache cache(renderer);
    std::string error;std::vector<std::shared_ptr<const uint64_t>> static_owners;
    std::vector<std::shared_ptr<const uint64_t>> transient_owners;
    std::vector<BufferView> accepted_geometry;
    cache.BeginUploadBatch();
    for(size_t draw=0;draw<512;++draw) {
      auto transient=std::make_shared<const uint64_t>(draw*2+1);
      transient_owners.push_back(transient);
      auto constants=cache.UniformBuffer({transient,*transient,{}},std::vector<uint8_t>(12288,0x5a),error);
      assert(constants.buffer);
      auto geometry=std::make_shared<const uint64_t>(draw*2+2);
      static_owners.push_back(geometry);
      auto vertices=cache.UploadBuffer({geometry,*geometry,{}},std::vector<uint8_t>(144,uint8_t(draw)),error);
      assert(vertices.buffer);accepted_geometry.push_back(vertices);
    }
    const auto initial=cache.Stats();
    transient_owners.clear();cache.BeginUploadBatch();cache.SweepRetired(false);
    const auto retained=cache.Stats();
    for(size_t draw=0;draw<accepted_geometry.size();++draw) {
      const auto& v=accepted_geometry[draw];
      const auto* bytes=static_cast<const uint8_t*>(v.buffer.contents)+v.offset;
      for(size_t i=0;i<v.length;++i)assert(bytes[i]==uint8_t(draw));
      auto reused=cache.UploadBuffer({static_owners[draw],*static_owners[draw],{}},
          std::vector<uint8_t>(144,uint8_t(draw)),error);
      assert(reused.buffer==v.buffer&&reused.offset==v.offset);
    }
    nlohmann::json report{{"draws",512},{"geometry_bytes",512*144},
      {"initial_resident_bytes",initial.resident_buffer_bytes},
      {"retained_after_constants_expire_bytes",retained.resident_buffer_bytes},
      {"geometry_views_unchanged",true},{"geometry_hits_without_reupload",true}};
    static_owners.clear();accepted_geometry.clear();cache.SweepRetired(false);
    assert(cache.Stats().resident_buffer_bytes==0&&cache.BufferCount()==0);
    // Retired constant entries are scattered across many hash buckets. Buffer
    // views simulate independently retained GPU leases: dropping cache/source
    // ownership must never overwrite the bytes that those views still see.
    ResourceCache bounded(renderer);auto living=std::make_shared<const uint64_t>(90000);
    auto vertex=bounded.UploadBuffer({living,*living,{}},std::vector<uint8_t>(144,0xa7),error);
    std::vector<std::shared_ptr<const uint64_t>> sources;
    std::vector<BufferView> submitted_constants;
    for(size_t i=0;i<8192;++i) {
      auto source=std::make_shared<const uint64_t>(i+1);sources.push_back(source);
      submitted_constants.push_back(bounded.UniformBuffer({source,*source,{}},std::vector<uint8_t>(256,uint8_t(i)),error));
      assert(submitted_constants.back().buffer);
    }
    sources.clear();bounded.BeginUploadBatch();size_t calls=0;
    while(bounded.Stats().resident_buffer_bytes>256*1024&&calls<64){bounded.SweepRetired(true);++calls;}
    assert(bounded.Stats().resident_buffer_bytes==256*1024&&bounded.BufferCount()==1);
    assert(static_cast<const uint8_t*>(vertex.buffer.contents)[vertex.offset]==0xa7);
    for(size_t i=0;i<submitted_constants.size();++i) {
      const auto& v=submitted_constants[i];const auto* bytes=static_cast<const uint8_t*>(v.buffer.contents)+v.offset;
      for(size_t j=0;j<v.length;++j)assert(bytes[j]==uint8_t(i));
    }
    report["bounded_retirement_calls"]=calls;report["retired_constant_entries"]=8192;
    report["retained_gpu_views_unchanged"]=true;report["live_geometry_retained"]=true;
    std::ofstream(argv[1])<<report.dump(2)<<'\n';
  }
}
