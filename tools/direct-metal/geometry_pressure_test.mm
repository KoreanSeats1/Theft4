// Actual production upload implementation; supports a separately compiled
// build134 implementation for A/B. No game assets and no FPS extrapolation.
#include "theft4_metal_resources.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
using namespace theft4::metal;
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  @autoreleasepool {
    Renderer renderer(2);assert(renderer.Ready());std::string error;nlohmann::json report;
    // Repeated oversized first-use bursts with no surviving source owners.
    // Cache eviction/Clear drops only CPU ownership; ARC/GPU references remain.
    std::vector<double> times;
    ResourceCache cache(renderer,8*1024*1024);
    const std::array<size_t,5> sizes{256*1024+3,512*1024,1024*1024+17,2*1024*1024,3*1024*1024+5};
    uint64_t checksum=0,generation=0;
    std::array<std::vector<uint8_t>,5> source;
    for(size_t i=0;i<sizes.size();++i)source[i].resize(sizes[i],uint8_t(i+11));
    for(size_t burst=0;burst<80;++burst) {
      const auto began=std::chrono::steady_clock::now();
      @autoreleasepool {
        cache.BeginUploadBatch();
        for(size_t i=0;i<sizes.size();++i) {
          auto owner=std::make_shared<const uint64_t>(++generation);
          auto view=cache.UploadBuffer({owner,generation,{}},source[i],error);
          assert(view.buffer&&view.length==sizes[i]);
          assert(!std::memcmp(static_cast<const uint8_t*>(view.buffer.contents)+view.offset,source[i].data(),sizes[i]));
          assert(cache.Stats().resident_buffer_bytes<=8*1024*1024);
          checksum+=static_cast<const uint8_t*>(view.buffer.contents)[view.offset];
        }
        cache.Clear();
      }
      times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count());
    }
    std::sort(times.begin()+8,times.end());const auto stats=cache.Stats();
    report["large_burst"]={{"warm_median_cpu_ms",times[8+(times.size()-8)/2]},
      {"warm_p95_cpu_ms",times[8+(times.size()-8)*95/100]},
      {"vm_allocations",stats.page_memory_allocations},{"vm_reuses",stats.page_memory_reuses},
      {"free_backing_bytes",stats.free_page_bytes},{"checksum",checksum},{"uploaded_bytes",stats.uploaded_bytes}};
    assert(stats.free_page_bytes<=16*1024*1024);
#ifndef THEFT4_UPLOAD_BASELINE
    assert(stats.page_memory_reuses>=300&&stats.page_memory_allocations<=10);
#endif
    // Saturate the hash table with live owners, then retire an isolated geometry
    // page whose keys are dispersed across buckets. Count sweep calls until it
    // goes away. Existing hot pages must remain hits with their original bytes.
    ResourceCache retire(renderer,16*1024*1024);std::vector<std::shared_ptr<const uint64_t>> living;
    std::vector<BufferView> old;
    for(size_t i=0;i<32768;++i) {
      auto owner=std::make_shared<const uint64_t>(++generation);living.push_back(owner);
      auto view=retire.UploadBuffer({owner,generation,{}},std::vector<uint8_t>(1,0x57),error);assert(view.buffer);
      if(i<16)old.push_back(view);
    }
    std::vector<std::shared_ptr<const uint64_t>> dead;
    for(size_t i=0;i<256;++i) {
      auto owner=std::make_shared<const uint64_t>(++generation);dead.push_back(owner);
      auto view=retire.UploadBuffer({owner,generation,{}},std::vector<uint8_t>(1024,0x93),error);assert(view.buffer);
    }
    const auto peak=retire.Stats().resident_buffer_bytes;dead.clear();retire.BeginUploadBatch();size_t calls=0;
    while(retire.Stats().resident_buffer_bytes==peak&&calls<128){retire.SweepRetired(true);++calls;}
    assert(retire.Stats().resident_buffer_bytes==peak-256*1024);
    for(size_t i=0;i<old.size();++i) {
      auto view=retire.UploadBuffer({living[i],*living[i],{}},std::vector<uint8_t>(1,0x57),error);
      assert(view.buffer==old[i].buffer&&view.offset==old[i].offset);
    }
    report["geometry_retirement"]={{"sweep_calls",calls},{"live_sources",living.size()},
      {"dead_geometry_page_released",true},{"hot_geometry_preserved",true}};
    const auto sweep_begin=std::chrono::steady_clock::now();
    for(size_t n=0;n<1000;++n)retire.SweepRetired(true);
    report["geometry_retirement"]["live_sweep_mean_cpu_ms"]=
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-sweep_begin).count()/1000;
#ifndef THEFT4_UPLOAD_BASELINE
    assert(calls<=2);
#endif
    // Pending GPU reads and an escaped ARC buffer keep large recycled backing
    // immutable even when CPU caches/sources retire and new bursts overflow.
    id<MTLCommandQueue> queue=[renderer.Device() newCommandQueue];assert(queue);
    std::vector<id<MTLCommandBuffer>> commands;std::vector<id<MTLBuffer>> outputs;
    id<MTLBuffer> escaped=nil;void* escaped_memory=nullptr;
    {
      ResourceCache temporary(renderer,2*1024*1024);
      for(size_t i=0;i<40;++i) {@autoreleasepool {
        const size_t size=512*1024+17;
        auto owner=std::make_shared<const uint64_t>(++generation);
        auto view=temporary.UploadBuffer({owner,generation,{}},std::vector<uint8_t>(size,uint8_t(i+1)),error);assert(view.buffer);
        if(!i){escaped=view.buffer;escaped_memory=view.buffer.contents;}
        else assert(view.buffer.contents!=escaped_memory);
        auto output=[renderer.Device() newBufferWithLength:size options:MTLResourceStorageModeShared];assert(output);
        auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
        [blit copyFromBuffer:view.buffer sourceOffset:view.offset toBuffer:output destinationOffset:0 size:size];
        [blit endEncoding];[command commit];commands.push_back(command);outputs.push_back(output);
        temporary.Clear();
      }}
    }
    for(size_t i=0;i<commands.size();++i) {
      [commands[i] waitUntilCompleted];assert(commands[i].status==MTLCommandBufferStatusCompleted);
      for(size_t b=0;b<512*1024+17;++b)assert(static_cast<const uint8_t*>(outputs[i].contents)[b]==uint8_t(i+1));
    }
    for(size_t b=0;b<512*1024+17;++b)assert(static_cast<const uint8_t*>(escaped.contents)[b]==1);
    report["pending_large_gpu_reads"]=40;report["escaped_large_buffer_immutable"]=true;
    report["scope"]="Apple host Metal cold-load/retirement fixture; not gameFPS";
    std::ofstream(argv[1])<<report.dump(2)<<'\n';
  }
}
