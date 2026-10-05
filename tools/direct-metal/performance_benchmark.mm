// Synthetic CPU admission and driver submission benchmark, not gameplay FPS.
#include "theft4_metal_frame.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
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
// Exercise the actual upload adapter with a scattered geometry working set,
// repeated constant banks and different ranges of each immutable source.
// Excludes source creation, GPU execution and shader loading from CPU timing.
nlohmann::json UploadViewBenchmark(metal::Renderer& renderer) {
  metal::PlanAdapter adapter{renderer};std::string error;
  std::array<std::vector<render::Buffer>,2> buffers;
  for(size_t kind=0;kind<2;++kind)for(size_t i=0;i<(kind?128:2048);++i) {
    std::array<uint8_t,4096> data{};data.fill(uint8_t(i));
    buffers[kind].push_back(Bytes(data,1+i));
  }
  std::vector<double> samples;uint64_t checksum=0;
  const auto before=adapter.ResourceStats();
  for(size_t frame=0;frame<28;++frame) {@autoreleasepool {
    const auto start=Clock::now();adapter.BeginUploadBatch();
    for(size_t draw=0;draw<4096;++draw) {
      for(size_t slot=0;slot<2;++slot) {
        auto range=buffers[0][(draw*13+slot*1024)%buffers[0].size()];
        range.offset=(draw&3)*256;range.length=128;
        const auto view=adapter.BufferFor(range,error);Require(bool(view.buffer),error);
        checksum+=view.offset+view.length;
      }
      for(size_t bank=0;bank<3;++bank) {
        auto range=buffers[1][(draw/16+bank*43)%buffers[1].size()];
        range.offset=bank*256;range.length=128;
        const auto view=adapter.ConstantFor(range,error);Require(bool(view.buffer),error);
        checksum+=view.offset+view.length;
      }
    }
    adapter.EndUploadBatch();
    if(frame>=4)samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());
  }}
  // Read every source back through its returned range; aliasing collisions
  // must never substitute another source's bytes.
  adapter.BeginUploadBatch();
  for(size_t kind=0;kind<2;++kind)for(size_t i=0;i<buffers[kind].size();++i) {
    auto range=buffers[kind][i];range.offset=256;range.length=128;
    const auto view=kind?adapter.ConstantFor(range,error):adapter.BufferFor(range,error);
    Require(view.buffer&&view.length==128,error);
    const auto* data=static_cast<const uint8_t*>(view.buffer.contents)+view.offset;
    for(size_t j=0;j<128;++j)Require(data[j]==uint8_t(i),"Upload view substituted another source");
  }
  adapter.EndUploadBatch();auto sorted=samples;std::sort(sorted.begin(),sorted.end());
  const auto after=adapter.ResourceStats();
  return {{"scope","Synthetic upload-adapter CPU work; not game FPS"},
    {"gpu",renderer.Device().name.UTF8String},{"draws_per_batch",4096},{"lookups_per_batch",20480},
    {"geometry_sources",2048},{"constant_sources",128},{"measured_batches",samples.size()},
    {"median_cpu_ms",sorted[sorted.size()/2]},{"samples_ms",samples},{"checksum",checksum},
    {"memo_hits",after.prepared_view_hits-before.prepared_view_hits},
    {"memo_misses",after.prepared_view_misses-before.prepared_view_misses},
    {"uploaded_bytes",after.uploaded_bytes-before.uploaded_bytes},{"range_byte_parity",true}};
}
int main(int argc,char** argv) {
  if(argc<3||argc>7)return 2;
  bool streaming=false,profile_gpu=false,heavy=false,pooled=true;
  for(int i=3;i<argc;++i){streaming|=std::string(argv[i])=="--streaming";profile_gpu|=std::string(argv[i])=="--profile";
    heavy|=std::string(argv[i])=="--heavy";if(std::string(argv[i])=="--immutable-uploads")pooled=false;}
  try {@autoreleasepool {
    std::string error;metal::Renderer renderer{1};metal::FrameAdapter adapter{renderer,128,384*1024*1024,pooled};
    Require(renderer.Ready(),"Metal unavailable");
    if(argc==4&&std::string_view(argv[3])=="--upload-views") {
      const auto report=UploadViewBenchmark(renderer);std::ofstream output(argv[2]);
      Require(bool(output),"Benchmark report could not be opened");
      output<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';return 0;
    }
    Require(adapter.Open(argv[1],error),error);
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
    uint64_t buffers_created=0,uploaded=0;std::vector<metal::GpuPassTiming> pass_timings;
    std::deque<std::shared_ptr<render::FramePlan>> retained_generations;
    const auto frames=streaming?52u:heavy?32u:22u;
    const size_t draws_per_frame=heavy?4500:1000,pass_count=heavy?180:streaming?200:1;
    const auto allocated_before=renderer.Device().currentAllocatedSize;
    for(size_t frame=0;frame<frames;++frame) {@autoreleasepool {
      auto plan=Plan(surface,frame+2);auto& draws=std::get<render::Pass>(plan->commands[0]).commands;
      for(size_t i=0;i<draws_per_frame;++i) {
        auto c=std::make_shared<render::Capture>(*prototype);
        // Per-object constants change, while vertices and other banks stay static.
        std::vector<uint8_t> data(4096);const uint32_t value=uint32_t(i);
        std::memcpy(data.data(),&value,4);c->draw.constants[0]=Bytes(data,1000+frame*1000+i);
        if(heavy) {
          // The real immediate-mode shader reads this bank. Change a dormant
          // prefix word while preserving all shader-visible color/scale words.
          auto shared=prototype->draw.constants[2].source->value;
          std::memcpy(shared.data(),&value,4);c->draw.constants[2]=Bytes(shared,200000+frame*draws_per_frame+i);
        }
        if(streaming) {
          const auto& source=prototype->draw.vertices[0].source->value;
          c->draw.vertices[0]=Bytes(source,100000+frame*1000+i);
        }
        draws.push_back(render::FrameDraw{c,{}});
      }
      if(streaming||heavy) {
        // The live game publishes hundreds of passes and retains CPU source
        // generations beyond one frame. Exercise both behaviors, not only one
        // static triangle and one pass. LOAD preserves the previous pass.
        auto original=std::move(draws);auto base=std::get<render::Pass>(plan->commands[0]);
        plan->commands.clear();
        for(size_t group=0;group<pass_count;++group) {
          auto split=base;if(group)split.colors[0]->load=render::Load::Load;
          for(size_t item=0;item<draws_per_frame/pass_count;++item)
            split.commands.push_back(std::move(original[group*(draws_per_frame/pass_count)+item]));
          plan->commands.push_back(std::move(split));
        }
      }
      const auto before=adapter.ImmutableStats();const auto began=Clock::now();
      auto receipt=adapter.Submit(plan,error,nullptr,profile_gpu&&frame==frames-1);Require(bool(receipt),error);
      const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-began).count();
      Require(receipt.Wait(error),error);
      if(profile_gpu&&frame==frames-1) {
        pass_timings=receipt.GpuPassTimings();
        if([renderer.Device() supportsFamily:MTLGPUFamilyApple1]&&[renderer.Device() supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) {
          Require(pass_timings.size()==pass_count,"GPU stage profile missing pass samples");
          for(const auto& t:pass_timings)Require(std::isfinite(t.vertex_ms)&&std::isfinite(t.fragment_ms)&&t.vertex_ms>=0&&t.fragment_ms>=0,"GPU stage timestamp conversion invalid");
        }
      }
      if(frame>=2) {
        submit_ms.push_back(elapsed);const auto after=adapter.ImmutableStats();
        buffers_created+=after.buffer_creates-before.buffer_creates;uploaded+=after.uploaded_bytes-before.uploaded_bytes;
      }
      if(frame==frames-1) {
        auto pixels=renderer.ReadRGBA8(adapter.Output(*plan,error),error);Require(pixels.size()==8*8*4,error);
        for(size_t i=0;i<pixels.size();i+=4)Require(pixels[i]==255&&pixels[i+1]==0&&pixels[i+2]==0&&pixels[i+3]==255,"Benchmark render parity failed");
      }
      if(streaming||heavy){retained_generations.push_back(plan);if(retained_generations.size()>(heavy?8:40))retained_generations.pop_front();}
    }}
    const auto median=[](auto v){std::sort(v.begin(),v.end());return v[v.size()/2];};
    nlohmann::json report{{"optimized",bool(THEFT4_PERF_OPTIMIZED)},{"gpu",renderer.Device().name.UTF8String},
      {"synthetic_validation_draws",600},{"indices_per_draw",65532},{"validation_median_ms",median(validation_ms)},
      {"synthetic_submission_draws",draws_per_frame},{"submission_median_ms",median(submit_ms)},
      {"submission_samples_ms",submit_ms},{"validation_samples_ms",validation_ms},
      {"measured_frames",submit_ms.size()},{"buffer_allocations",buffers_created},{"uploaded_bytes",uploaded},{"pixel_parity",true}};
    report["streaming_geometry"]=streaming;report["passes_per_frame"]=pass_count;report["reusable_frame_uploads"]=pooled;
    if(profile_gpu) {
      report["gpu_profile_samples"]=nlohmann::json::array();
      for(const auto& t:pass_timings)report["gpu_profile_samples"].push_back({{"pass",t.pass},{"vertex_ms",t.vertex_ms},{"fragment_ms",t.fragment_ms}});
    }
    report["gpu_allocated_growth_bytes"]=renderer.Device().currentAllocatedSize-allocated_before;
#ifdef THEFT4_PACKED_UPLOAD_STATS
    const auto stats=adapter.ImmutableStats();
    report["buffer_cache_peak_bytes"]=stats.peak_buffer_bytes;report["buffer_cache_resident_bytes"]=stats.resident_buffer_bytes;
    report["buffer_evictions"]=stats.buffer_evictions;
    report["frame_buffer_reuses"]=stats.frame_upload_buffer_reuses;report["frame_buffer_bytes"]=stats.frame_upload_resident_bytes;
#endif
#if THEFT4_PERF_OPTIMIZED
    report["index_scanned"]=ranges.ScannedIndices();report["index_cache_hits"]=ranges.Hits();
#endif
    std::ofstream out(argv[2]);out<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';
  }}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
