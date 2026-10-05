// CPU-only preparation timing using private saved game draws. No gameplay FPS claim.
#include "theft4_metal_plan.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
int main(int argc,char** argv) {
  if(argc!=4)return 2;
  @autoreleasepool {
    using namespace theft4;
    metal::Renderer renderer;metal::PlanAdapter adapter(renderer);std::string error;
    if(!renderer.Ready()||!adapter.Open(argv[1],error)){std::cerr<<error;return 1;}
    std::vector<std::filesystem::path> paths;
    for(const auto& file:std::filesystem::directory_iterator(argv[2]))
      if(file.path().extension()==".t4draw")paths.push_back(file.path());
    std::sort(paths.begin(),paths.end());
    std::vector<render::Capture> captures;
    for(const auto& path:paths) {
      render::Capture capture;metal::Draw draw;
      if(!render::ReadCapture(path.string(),capture,error)||!adapter.Prepare(capture,draw,error)) {
        std::cerr<<path.filename()<<": "<<error<<'\n';return 1;
      }
      captures.push_back(std::move(capture));
    }
    if(captures.empty())return 1;
    std::vector<double> samples;size_t bindings=0;
    for(size_t batch=0;batch<28;++batch) {@autoreleasepool {
      const auto begin=std::chrono::steady_clock::now();adapter.BeginUploadBatch();
      for(size_t draw=0;draw<2048;++draw) {
        metal::Draw result;
        if(!adapter.Prepare(captures[(draw*13+batch)%captures.size()],result,error)){std::cerr<<error;return 1;}
        bindings+=result.textures.size()+result.samplers.size();
      }
      adapter.EndUploadBatch();
      const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
      if(batch>=4)samples.push_back(ms);
    }}
    auto sorted=samples;std::sort(sorted.begin(),sorted.end());
    nlohmann::json report={{"scope","Saved game draw CPU admission/preparation only; no GPU execution or gameplay FPS"},
      {"gpu",renderer.Device().name.UTF8String},{"draws_per_batch",2048},{"saved_draws",captures.size()},
      {"median_ms",sorted[sorted.size()/2]},{"samples_ms",samples},{"bindings_checksum",bindings}};
    std::ofstream(argv[3])<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';
  }
}
