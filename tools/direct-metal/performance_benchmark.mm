// Synthetic CPU admission and driver submission benchmark, not gameplay FPS.
#include "theft4_metal_frame.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#ifndef THEFT4_PERF_OPTIMIZED
#define THEFT4_PERF_OPTIMIZED 0
#endif
using namespace theft4;
using Clock=std::chrono::steady_clock;
void Require(bool okay,const std::string& error){if(!okay)throw std::runtime_error(error);}
render::Buffer Bytes(std::span<const uint8_t> data,uint64_t generation) {
  auto bytes=std::make_shared<render::Bytes>();bytes->generation=generation;bytes->value.assign(data.begin(),data.end());
  return {bytes,0,data.size()};
}
std::shared_ptr<render::Capture> Draw(uint64_t generation) {
  auto c=std::make_shared<render::Capture>();c->width=c->height=8;auto& d=c->draw;
  d.pipeline.vertex.hash=0x048E49996734F6B5ull;d.pipeline.fragment.hash=0x949ED69300FB92B7ull;
  d.pipeline.colors[0]=render::Format::RGBA8Unorm;
  d.pipeline.attributes={{0,0,0,render::VertexFormat::Float4},{17,0,16,render::VertexFormat::Float4},
                         {13,0,32,render::VertexFormat::Float4}};d.pipeline.streams[0]={48,false};
  d.vertex_count=3;d.viewport={0,0,8,8,0,1};d.scissor={0,0,8,8};
  const std::array<float,36> vertices{-1,-1,0.5,1,1,0,0,1,0,0,0,0,
    3,-1,0.5,1,1,0,0,1,0,0,0,0,-1,3,0.5,1,1,0,0,1,0,0,0,0};
  d.vertices[0]=Bytes({reinterpret_cast<const uint8_t*>(vertices.data()),sizeof(vertices)},generation);
  for(size_t bank=0;bank<3;++bank) {
    std::vector<uint8_t> data(bank==0?4096:bank==1?3584:1056);
    if(bank==2)for(size_t target=0;target<4;++target) {
      const std::array<float,12> p{1,1,1,1,0,0,0,0,1,1,1,1};std::memcpy(data.data()+0x360+target*48,p.data(),48);
    }
    d.constants[bank]=Bytes(data,generation+bank+1);
  }
  return c;
}
std::shared_ptr<render::FramePlan> Plan(std::shared_ptr<render::Surface> surface,uint64_t sequence) {
  auto plan=std::make_shared<render::FramePlan>();plan->sequence=sequence;plan->surfaces={surface};
  render::Pass pass;render::Attachment a;a.view={surface->key,0,0,render::Aspect::Color};
  a.load=render::Load::Clear;a.store=render::Store::Store;pass.colors[0]=a;
  plan->commands={pass};plan->output=a.view;return plan;
}
int main(int argc,char** argv) {
  if(argc!=3)return 2;
  try {@autoreleasepool {
    std::string error;metal::Renderer renderer{1};metal::FrameAdapter adapter{renderer};
    Require(renderer.Ready(),"Metal unavailable");Require(adapter.Open(argv[1],error),error);
    auto surface=std::make_shared<render::Surface>();surface->key={1,1};surface->width=surface->height=8;surface->format=render::Format::RGBA8Unorm;
    auto prototype=Draw(100);auto admission=Plan(surface,1);
    // 600 draws repeatedly reference one immutable 65,532-index generation.
    std::vector<uint16_t> indices(65532);for(size_t i=0;i<indices.size();++i)indices[i]=uint16_t(i%3);
    prototype->draw.indices=Bytes({reinterpret_cast<const uint8_t*>(indices.data()),indices.size()*2},900);
    prototype->draw.index_count=uint32_t(indices.size());prototype->draw.vertex_count=0;
    auto& pass=std::get<render::Pass>(admission->commands[0]);
    for(size_t i=0;i<600;++i)pass.commands.push_back(render::FrameDraw{std::make_shared<render::Capture>(*prototype),{}});
    for(size_t i=0;i<127;++i) {
      auto unused=std::make_shared<render::Surface>(*surface);unused->key={i+2,1};admission->surfaces.push_back(unused);
    }
#if THEFT4_PERF_OPTIMIZED
    render::IndexRangeCache ranges;
#endif
    std::vector<double> validation_ms,submit_ms;render::SurfaceContents final;
    for(size_t frame=0;frame<12;++frame) {
      const auto began=Clock::now();
#if THEFT4_PERF_OPTIMIZED
      Require(render::ValidateFrame(*admission,{},final,error,&ranges),error);
#else
      Require(render::ValidateFrame(*admission,{},final,error),error);
#endif
      validation_ms.push_back(std::chrono::duration<double,std::milli>(Clock::now()-began).count());
    }
    prototype=Draw(200);
    uint64_t buffers_created=0,uploaded=0;
    for(size_t frame=0;frame<22;++frame) {@autoreleasepool {
      auto plan=Plan(surface,frame+2);auto& draws=std::get<render::Pass>(plan->commands[0]).commands;
      for(size_t i=0;i<1000;++i) {
        auto c=std::make_shared<render::Capture>(*prototype);
        // Per-object constants change, while vertices and other banks stay static.
        std::vector<uint8_t> data(4096);const uint32_t value=uint32_t(i);
        std::memcpy(data.data(),&value,4);c->draw.constants[0]=Bytes(data,1000+frame*1000+i);
        draws.push_back(render::FrameDraw{c,{}});
      }
      const auto before=adapter.ImmutableStats();const auto began=Clock::now();
      auto receipt=adapter.Submit(plan,error);Require(bool(receipt),error);
      const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-began).count();
      Require(receipt.Wait(error),error);
      if(frame>=2) {
        submit_ms.push_back(elapsed);const auto after=adapter.ImmutableStats();
        buffers_created+=after.buffer_creates-before.buffer_creates;uploaded+=after.uploaded_bytes-before.uploaded_bytes;
      }
      if(frame==21) {
        auto pixels=renderer.ReadRGBA8(adapter.Output(*plan,error),error);Require(pixels.size()==8*8*4,error);
        for(size_t i=0;i<pixels.size();i+=4)Require(pixels[i]==255&&pixels[i+1]==0&&pixels[i+2]==0&&pixels[i+3]==255,"Benchmark render parity failed");
      }
    }}
    const auto median=[](auto v){std::sort(v.begin(),v.end());return v[v.size()/2];};
    nlohmann::json report{{"optimized",bool(THEFT4_PERF_OPTIMIZED)},{"gpu",renderer.Device().name.UTF8String},
      {"synthetic_validation_draws",600},{"indices_per_draw",65532},{"validation_median_ms",median(validation_ms)},
      {"synthetic_submission_draws",1000},{"submission_median_ms",median(submit_ms)},
      {"submission_samples_ms",submit_ms},{"validation_samples_ms",validation_ms},
      {"measured_frames",submit_ms.size()},{"buffer_allocations",buffers_created},{"uploaded_bytes",uploaded},{"pixel_parity",true}};
#if THEFT4_PERF_OPTIMIZED
    report["index_scanned"]=ranges.ScannedIndices();report["index_cache_hits"]=ranges.Hits();
#endif
    std::ofstream out(argv[2]);out<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';
  }}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
