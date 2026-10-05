// CPU upload preparation fixture. No gameplay FPS claim and no GPU waits.
#include "theft4_metal_resources.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
using namespace theft4::metal;
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  @autoreleasepool {
    Renderer renderer(2);if(!renderer.Ready())return 1;
    nlohmann::json report;
    const auto run=[&](bool recycle,bool compact) {
      ResourceCache cache(renderer,8*1024*1024,recycle);std::string error;
      std::vector<double> times;uint64_t generation=0,checksum=0;
      const std::array<size_t,3> sizes=compact?std::array<size_t,3>{512,256,1056}:std::array<size_t,3>{4096,3584,1056};
      std::array<std::vector<uint8_t>,3> banks;
      for(size_t bank=0;bank<3;++bank)banks[bank].resize(sizes[bank],uint8_t(41+bank));
      for(size_t frame=0;frame<36;++frame) {
        const auto began=std::chrono::steady_clock::now();
        @autoreleasepool {
          cache.BeginUploadBatch();
          for(size_t draw=0;draw<2048;++draw)for(size_t bank=0;bank<3;++bank) {
            auto owner=std::make_shared<const uint64_t>(++generation);
            auto view=cache.UniformBuffer({owner,generation,{}},banks[bank],error);
            if(!view.buffer||view.length!=sizes[bank]||std::memcmp(static_cast<uint8_t*>(view.buffer.contents)+view.offset,banks[bank].data(),sizes[bank]))throw std::runtime_error("upload mismatch");
            checksum+=static_cast<uint8_t*>(view.buffer.contents)[view.offset];
          }
          cache.Clear();
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
        if(frame>=8)times.push_back(ms);
      }
      std::sort(times.begin(),times.end());const auto stats=cache.Stats();
      return nlohmann::json{{"median_cpu_ms",times[times.size()/2]},{"uploaded_bytes",stats.uploaded_bytes},
        {"vm_allocations",stats.page_memory_allocations},{"vm_reuses",stats.page_memory_reuses},
        {"metal_buffer_creates",stats.buffer_creates},{"free_page_bytes",stats.free_page_bytes},{"checksum",checksum}};
    };
    // Alternate order to reduce thermal/order bias; report both trials.
    for(size_t trial=0;trial<4;++trial)for(size_t order=0;order<3;++order) {
      const size_t mode=(trial+order)%3;
      const char* name=mode==0?"full_fresh":mode==1?"full_recycled":"compact_recycled";
      report[name].push_back(run(mode!=0,mode==2));
    }
    report["draws_per_frame"]=2048;report["frames_per_trial"]=36;
    report["scope"]="CPU upload fixture, same shader-visible prefix bytes; not game FPS";
    std::ofstream(argv[1])<<report.dump(2)<<'\n';
  }
}
