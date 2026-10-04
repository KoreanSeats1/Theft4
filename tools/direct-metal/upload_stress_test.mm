#include "theft4_metal_resources.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
using namespace theft4::metal;
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  @autoreleasepool {
    Renderer renderer(2);std::string error;
    if(!renderer.Ready())return 1;
#ifdef THEFT4_PACKED_UPLOAD_STRESS
    constexpr size_t budget=4*1024*1024;
    ResourceCache cache(renderer,budget);
#else
    ResourceCache cache(renderer);
#endif
    const uint64_t before=renderer.Device().currentAllocatedSize;
    std::vector<std::shared_ptr<const uint64_t>> owners;
    BufferView retained;uint64_t generation=0;
    for(size_t frame=0;frame<768;++frame) {@autoreleasepool {
      cache.BeginUploadBatch();
      auto owner=std::make_shared<const uint64_t>(++generation);owners.push_back(owner);
      std::vector<uint8_t> constants(4096,uint8_t(frame));
      auto view=cache.UniformBuffer({owner,generation,{}},constants,error);
      if(!view.buffer){std::cerr<<error;return 1;}
      if(frame==0)retained=view;
      for(size_t draw=0;draw<8;++draw) {
        auto vertex=std::make_shared<const uint64_t>(++generation);owners.push_back(vertex);
        std::vector<uint8_t> bytes(144,uint8_t(draw+frame));
#ifdef THEFT4_PACKED_UPLOAD_STRESS
        auto v=cache.UploadBuffer({vertex,generation,{}},bytes,error);
        if(!v.buffer||std::memcmp(static_cast<const uint8_t*>(v.buffer.contents)+v.offset,bytes.data(),bytes.size()))return 1;
        if(cache.Stats().resident_buffer_bytes>budget)return 1;
#else
        if(!cache.Buffer({vertex,generation,{}},bytes,error))return 1;
#endif
      }
    }}
    // CPU generations remain alive just as they do in the title cache. An
    // evicted page retained by an encoded packet must never be overwritten.
    for(size_t i=0;i<retained.length;++i)
      if(static_cast<const uint8_t*>(retained.buffer.contents)[retained.offset+i]!=0)return 1;
    auto reloaded=cache.UniformBuffer({owners[0],1,{}},std::vector<uint8_t>(4096),error);
    if(!reloaded.buffer)return 1;
    const auto s=cache.Stats();
    nlohmann::json report{{"frames",768},{"live_cpu_generations",owners.size()},
      {"source_bytes",768*(4096+8*144)}, {"gpu_allocated_growth_bytes",renderer.Device().currentAllocatedSize-before},
      {"buffer_allocations",s.buffer_creates},{"retained_old_page_unchanged",true}};
#ifdef THEFT4_PACKED_UPLOAD_STRESS
    report["cache_budget_bytes"]=budget;report["cache_resident_bytes"]=s.resident_buffer_bytes;
    report["cache_peak_bytes"]=s.peak_buffer_bytes;report["cache_evictions"]=s.buffer_evictions;
    if(s.resident_buffer_bytes>budget||!s.buffer_evictions||reloaded.buffer==retained.buffer)return 1;
    std::vector<uint8_t> large(256*1024,0x5a);BufferView retained_large;
    for(size_t i=0;i<32;++i) {
      auto owner=std::make_shared<const uint64_t>(++generation);owners.push_back(owner);
      auto view=cache.UploadBuffer({owner,generation,{}},large,error);
      if(!view.buffer||cache.Stats().resident_buffer_bytes>budget)return 1;
      if(i==0)retained_large=view;
    }
    if(static_cast<const uint8_t*>(retained_large.buffer.contents)[retained_large.offset]!=0x5a)return 1;
    report["large_dedicated_uploads_bounded"]=true;
#endif
    owners.clear();cache.SweepRetired();cache.Clear();
    if(cache.BufferCount()||cache.TextureCount())return 1;
    std::ofstream(argv[1])<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';
  }
}
