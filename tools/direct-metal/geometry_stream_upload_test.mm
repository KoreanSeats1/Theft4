// Exercises production upload ownership on a real Metal device. Timing is a
// synthetic streaming fixture, never an estimate of gameplay FPS.
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
    Renderer renderer(2);assert(renderer.Ready());std::string error;
    nlohmann::json report;
    const auto run=[&](bool compact,bool small) {
      ResourceCache cache(renderer,128*1024*1024,true,compact);
      std::vector<std::shared_ptr<const uint64_t>> owners;
      std::vector<BufferView> escaped;
      std::vector<double> times;uint64_t generation=0,checksum=0;
      const std::array<size_t,6> sizes{144,4096,24576,65537,96*1024,128*1024};
      for(size_t frame=0;frame<120;++frame) {
        const auto began=std::chrono::steady_clock::now();
        cache.BeginUploadBatch();
        for(size_t mesh=0;mesh<(small?2:6);++mesh) {
          auto owner=std::make_shared<const uint64_t>(++generation);owners.push_back(owner);
          std::vector<uint8_t> bytes(small?144:sizes[mesh],uint8_t(generation));
          auto view=cache.UploadBuffer({owner,generation,{}},bytes,error);assert(view.buffer);
          assert(view.offset%256==0&&view.length==bytes.size());
          assert(!std::memcmp(static_cast<const uint8_t*>(view.buffer.contents)+view.offset,bytes.data(),bytes.size()));
          checksum+=static_cast<const uint8_t*>(view.buffer.contents)[view.offset];escaped.push_back(view);
        }
        times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count());
      }
      const auto stats=cache.Stats();
      for(size_t i=0;i<escaped.size();++i) {
        const auto& view=escaped[i];
        for(size_t j=0;j<view.length;++j)assert(static_cast<const uint8_t*>(view.buffer.contents)[view.offset+j]==uint8_t(i+1));
      }
      owners.clear();cache.SweepRetired(false);assert(cache.Stats().resident_buffer_bytes==0);
      // A cleared/retired tail cannot retain a dangling allocation iterator.
      cache.Clear();auto owner=std::make_shared<const uint64_t>(++generation);
      assert(cache.UploadBuffer({owner,generation,{}},std::vector<uint8_t>(256,0xc7),error).buffer);
      cache.Clear();assert(cache.Stats().resident_buffer_bytes==0);
      std::sort(times.begin(),times.end());
      return nlohmann::json{{"median_cpu_ms",times[times.size()/2]},{"p95_cpu_ms",times[times.size()*95/100]},
          {"new_metal_buffers",stats.buffer_creates},{"resident_bytes",stats.resident_buffer_bytes},
          {"uploaded_bytes",stats.uploaded_bytes},{"checksum",checksum}};
    };
    for(size_t trial=0;trial<4;++trial)for(bool small:{false,true})for(size_t order=0;order<2;++order) {
      const bool compact=(trial+order)%2;
      report[small?"small_stream":"mixed_stream"][compact?"compact":"baseline"].push_back(run(compact,small));
    }
    // Submit earlier ranges, then append new ranges to that SAME Metal page
    // while command buffers may still be reading it. GPU copies must retain
    // exact bytes across frame boundaries, LRU eviction, Clear and destruction.
    id<MTLCommandQueue> queue=[renderer.Device() newCommandQueue];assert(queue);
    std::vector<id<MTLCommandBuffer>> commands;
    std::vector<id<MTLBuffer>> results;
    std::vector<std::shared_ptr<const uint64_t>> owners;
    BufferView old;
    {
      ResourceCache cache(renderer,256*1024,true,true);
      for(size_t frame=0;frame<300;++frame) {
        cache.BeginUploadBatch();auto owner=std::make_shared<const uint64_t>(frame+1);owners.push_back(owner);
        std::vector<uint8_t> bytes(1024,uint8_t(frame));
        auto view=cache.UploadBuffer({owner,*owner,{}},bytes,error);assert(view.buffer);
        if(frame==0)old=view;
        if(frame==1)assert(view.buffer==old.buffer&&view.offset==1024);
        auto output=[renderer.Device() newBufferWithLength:bytes.size() options:MTLResourceStorageModeShared];assert(output);
        auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
        [blit copyFromBuffer:view.buffer sourceOffset:view.offset toBuffer:output destinationOffset:0 size:view.length];
        [blit endEncoding];[command commit];commands.push_back(command);results.push_back(output);
        assert(cache.Stats().resident_buffer_bytes<=256*1024);
      }
      assert(old.buffer&&static_cast<const uint8_t*>(old.buffer.contents)[old.offset]==0);
      auto retry=cache.UploadBuffer({owners[0],1,{}},std::vector<uint8_t>(1024,0),error);
      assert(retry.buffer&&retry.buffer!=old.buffer); // evicted source reuploads safely
      for(size_t size:{size_t(64*1024),size_t(64*1024+1),size_t(256*1024),size_t(256*1024+1)}) {
        auto owner=std::make_shared<const uint64_t>(1000+size);owners.push_back(owner);
        auto view=cache.UploadBuffer({owner,*owner,{}},std::vector<uint8_t>(size,0xe3),error);
        assert(view.buffer&&view.length==size);
        for(size_t byte=0;byte<size;++byte)assert(static_cast<const uint8_t*>(view.buffer.contents)[view.offset+byte]==0xe3);
      }
      cache.Clear();assert(cache.BufferCount()==0&&cache.Stats().resident_buffer_bytes==0);
    }
    for(size_t i=0;i<commands.size();++i) {
      [commands[i] waitUntilCompleted];assert(commands[i].status==MTLCommandBufferStatusCompleted);
      for(size_t byte=0;byte<1024;++byte)assert(static_cast<const uint8_t*>(results[i].contents)[byte]==uint8_t(i));
    }
    report["gpu_cross_frame_reads"]=300;
    report["gpu_bytes_identical_after_eviction_and_cache_destruction"]=true;
    report["scope"]="Host Metal synthetic upload fixture; not iPad frame-time evidence";
    std::ofstream(argv[1])<<report.dump(2)<<'\n';
  }
}
