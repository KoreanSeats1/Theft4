// Allocation and differential admission checks. A private preserved baseline
// can be linked with THEFT4_FRAME_BASELINE for comparison; no game files needed.
#include "theft4_frame_plan.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <random>
static bool count_allocations=false;
static size_t allocations=0;
void* operator new(size_t n) {
  if(auto p=std::malloc(n?n:1)){if(count_allocations)++allocations;return p;}
  throw std::bad_alloc();
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t alignment) {
  void* p=nullptr;
  if(!posix_memalign(&p,std::max(size_t(alignment),sizeof(void*)),n?n:1)) {
    if(count_allocations)++allocations;return p;
  }
  throw std::bad_alloc();
}
void operator delete(void* p,std::align_val_t) noexcept{std::free(p);}
using namespace theft4::render;
#ifdef THEFT4_FRAME_BASELINE
namespace theft4::render {
bool ValidateFrameBaseline(const FramePlan&,const SurfaceContents&,SurfaceContents&,std::string&,
                           IndexRangeCache*,DrawVertexRanges*);
std::vector<uint8_t> DeadAttachmentStoresBaseline(const FramePlan&);
std::vector<uint8_t> RedundantAttachmentLoadsBaseline(const FramePlan&);
}
#endif
static FramePlan Fixture(size_t passes,bool resolve) {
  FramePlan frame;frame.sequence=1;
  for(size_t i=0;i<12;++i) {
    auto s=std::make_shared<Surface>();s->key={i+1,1};s->width=s->height=64;
    s->samples=resolve&&i<6?4:1;
    s->format=i%6<4?Format::RGBA8Unorm:i%6==4?Format::Depth32Float:Format::Stencil8;
    frame.surfaces.push_back(s);
  }
  Pass pass;
  for(size_t i=0;i<6;++i) {
    Attachment a;a.view={{i+1,1},0,0,i<4?Aspect::Color:i==4?Aspect::Depth:Aspect::Stencil};
    a.load=Load::Clear;a.store=Store::Store;
    if(resolve){a.resolve=SurfaceView{{i+7,1},0,0,a.view.aspect};a.store=Store::StoreAndResolve;
      if(i>=4)a.filter=ResolveFilter::Sample0;}
    if(i<4)pass.colors[i]=a;else if(i==4)pass.depth=a;else pass.stencil=a;
  }
  for(size_t i=0;i<passes;++i) {
    if(i) {
      for(auto& a:pass.colors)a->load=Load::Load;
      pass.depth->load=pass.stencil->load=Load::Load;
    }
    frame.commands.push_back(pass);
  }
  frame.output=SurfaceView{{resolve?7u:1u,1},0,0,Aspect::Color};return frame;
}
static auto Measure(const FramePlan& f,bool baseline,size_t repeats) {
  SurfaceContents final;std::string error;size_t total=0;
  const auto start=std::chrono::steady_clock::now();
  for(size_t i=0;i<repeats;++i) {
    allocations=0;count_allocations=true;
#ifdef THEFT4_FRAME_BASELINE
    const bool okay=baseline?ValidateFrameBaseline(f,{},final,error,nullptr,nullptr):ValidateFrame(f,{},final,error);
#else
    const bool okay=ValidateFrame(f,{},final,error);
#endif
    count_allocations=false;assert(okay);total+=allocations;
  }
  return std::pair{total/repeats,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/repeats};
}
int main() {
  const auto one=Fixture(1,true),many=Fixture(240,true);
  const auto a=Measure(one,false,10),b=Measure(many,false,80);
  assert(a.first==b.first); // No per-pass admission heap allocations.
  SurfaceContents final;std::string error;
  assert(ValidateFrame(many,{},final,error)&&final.size()==12);
  auto bad=one;std::get<Pass>(bad.commands[0]).colors[3]->resolve=std::get<Pass>(bad.commands[0]).colors[0]->resolve;
  assert(!ValidateFrame(bad,{},final,error));
  FrameAttachmentAnalysis analysis;
  AnalyzeFrameAttachments(many,analysis);
  allocations=0;count_allocations=true;AnalyzeFrameAttachments(many,analysis);count_allocations=false;
  assert(allocations==0); // Warm analysis lists and arena perform no heap work.
  auto drawing=Fixture(240,false);auto capture=std::make_shared<Capture>();
  capture->width=capture->height=64;capture->draw.pipeline.vertex.hash=1;
  capture->draw.pipeline.fragment.hash=2;capture->draw.vertex_count=3;
  capture->draw.viewport={0,0,64,64,0,1};capture->draw.scissor={0,0,64,64};
  capture->draw.pipeline.colors.fill(Format::RGBA8Unorm);
  capture->draw.pipeline.depth=Format::Depth32Float;capture->draw.pipeline.stencil=Format::Stencil8;
  for(size_t bank=0;bank<3;++bank) {
    auto bytes=std::make_shared<Bytes>();bytes->generation=bank+1;
    bytes->value.resize(bank==0?4096:bank==1?3584:1056);
    capture->draw.constants[bank]={bytes,0,bytes->value.size()};
  }
  for(auto& c:drawing.commands)for(size_t i=0;i<20;++i)
    std::get<Pass>(c).commands.push_back(FrameDraw{capture,{}});
  FrameValidationScratch scratch;assert(ValidateFrame(drawing,{},final,error,nullptr,scratch));
  assert(scratch.draw_ranges.size()==4800);const auto capacity=scratch.draw_ranges.capacity();
  allocations=0;count_allocations=true;
  assert(ValidateFrame(drawing,{},final,error,nullptr,scratch));count_allocations=false;
  const auto scratch_allocations=allocations;
  const auto no_draw=Measure(Fixture(240,false),false,1);
  assert(scratch_allocations==no_draw.first&&scratch.draw_ranges.capacity()==capacity);
  for(const auto& r:scratch.draw_ranges)assert(r.capture==capture.get()&&r.maximum_vertex==2);
  auto rejected_plan=drawing;std::get<Pass>(rejected_plan.commands.back()).attachmentless_extent={1,1};
  const auto preserved=final;assert(!ValidateFrame(rejected_plan,{},final,error,nullptr,scratch));
  assert(final==preserved&&scratch.draw_ranges.empty()&&scratch.draw_ranges.capacity()==capacity);
  assert(ValidateFrame(drawing,{},final,error,nullptr,scratch));scratch.Clear();
  assert(scratch.draw_ranges.empty()&&scratch.draw_ranges.capacity()==capacity);
  AnalyzeFrameAttachments(drawing,analysis);
  allocations=0;count_allocations=true;AnalyzeFrameAttachments(drawing,analysis);count_allocations=false;
  const auto analysis_allocations=allocations;
  // Map-node churn must fit the reusable arena independently of pass count.
  assert(analysis_allocations==0);
  const auto one_store=Fixture(1,false);AnalyzeFrameAttachments(one_store,analysis);
  allocations=0;count_allocations=true;AnalyzeFrameAttachments(one_store,analysis);count_allocations=false;
  assert(allocations==analysis_allocations);
  // Arena overflow is temporary and retains complete analysis results.
  FramePlan large;large.sequence=1;
  for(size_t i=0;i<2048;++i) {
    auto s=std::make_shared<Surface>();s->key={i+1,1};s->width=s->height=64;s->format=Format::RGBA8Unorm;
    large.surfaces.push_back(s);Pass p;Attachment at;at.view={s->key};at.load=Load::Clear;at.store=Store::Store;
    p.colors[0]=at;large.commands.push_back(p);
  }
  AnalyzeFrameAttachments(large,analysis);
  assert(analysis.dead_stores==DeadAttachmentStores(large)&&analysis.redundant_loads==RedundantAttachmentLoads(large));
  AnalyzeFrameAttachments(drawing,analysis);
  assert(analysis.dead_stores==DeadAttachmentStores(drawing));
  // The last slot must reject a collision with either an earlier attachment
  // or its resolve, at the maximum list capacity.
  bad=one;std::get<Pass>(bad.commands[0]).stencil->resolve->surface={6,1};
  assert(!ValidateFrame(bad,{},final,error));
#ifdef THEFT4_FRAME_BASELINE
  const auto measure_analysis=[&](bool baseline) {
    size_t total=0;const auto began=std::chrono::steady_clock::now();
    for(size_t i=0;i<80;++i) {
      allocations=0;count_allocations=true;
      if(baseline) {
        const auto stores=DeadAttachmentStoresBaseline(drawing);
        const auto loads=RedundantAttachmentLoadsBaseline(drawing);
        assert(stores==analysis.dead_stores&&loads==analysis.redundant_loads);
      } else AnalyzeFrameAttachments(drawing,analysis);
      count_allocations=false;total+=allocations;
    }
    return std::pair{total/80,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()/80};
  };
  AnalyzeFrameAttachments(drawing,analysis);
  const auto old_analysis=measure_analysis(true),new_analysis=measure_analysis(false);
  DrawVertexRanges baseline_ranges;allocations=0;count_allocations=true;
  assert(ValidateFrameBaseline(drawing,{},final,error,nullptr,&baseline_ranges));count_allocations=false;
  const auto baseline_range_allocations=allocations;
  std::mt19937_64 rng(136);size_t accepted=0,rejected=0;
  for(size_t trial=0;trial<60000;++trial) {
    auto f=Fixture(1+rng()%4,rng()%2);SurfaceContents initial;
    for(size_t i=0;i<f.surfaces.size();++i)if(rng()%3==0)
      initial.insert({f.surfaces[i]->key,0,0,i%6<4?Aspect::Color:i%6==4?Aspect::Depth:Aspect::Stencil});
    auto& pass=std::get<Pass>(f.commands[rng()%f.commands.size()]);
    std::array<Attachment*,6> list{&*pass.colors[0],&*pass.colors[1],&*pass.colors[2],&*pass.colors[3],&*pass.depth,&*pass.stencil};
    auto& at=*list[rng()%6];
    switch(rng()%18) {
      case 0:break;
      case 1:at.view=list[rng()%6]->view;break;
      case 2:at.resolve=list[rng()%6]->view;break;
      case 3:at.view.level=rng()%3;break;
      case 4:at.view.slice=rng()%3;break;
      case 5:at.view.surface.id=rng()%15;break;
      case 6:at.load=Load(rng()%5);break;
      case 7:at.store=Store(rng()%6);break;
      case 8:at.filter=ResolveFilter(rng()%6);break;
      case 9:at.clear_depth=std::numeric_limits<double>::quiet_NaN();break;
      case 10:at.clear_stencil=256;break;
      case 11:at.clear_color[rng()%4]=std::numeric_limits<double>::infinity();break;
      case 12:at.view.aspect=Aspect(rng()%5);break;
      case 13:at.resolve.reset();break;
      case 14:pass.attachmentless_extent={64,64};break;
      case 15:pass.colors[rng()%4].reset();break;
      case 16:pass.commands.push_back(RectClear{uint32_t(rng()%32),false,false,{0,0,uint32_t(rng()%66),64}});break;
      case 17:if(at.resolve)at.resolve->surface.id=rng()%15;break;
    }
    const SurfaceContents sentinel{{{999,1},0,0,Aspect::Color}};
    auto before=sentinel,after=sentinel;std::string old_error,new_error;
    DrawVertexRanges old_ranges{{nullptr,999,false}},new_ranges=old_ranges;
    const bool old_okay=ValidateFrameBaseline(f,initial,before,old_error,nullptr,&old_ranges);
    const bool new_okay=ValidateFrame(f,initial,after,new_error,nullptr,&new_ranges);
    assert(old_okay==new_okay&&old_error==new_error&&before==after);
    assert(old_ranges.size()==new_ranges.size());
    for(size_t i=0;i<old_ranges.size();++i)assert(old_ranges[i].capture==new_ranges[i].capture&&
      old_ranges[i].maximum_vertex==new_ranges[i].maximum_vertex&&old_ranges[i].index_has_restart==new_ranges[i].index_has_restart);
    if(!old_okay){assert(after==sentinel&&new_ranges[0].maximum_vertex==999);++rejected;}else ++accepted;
    AnalyzeFrameAttachments(f,analysis);
    assert(analysis.dead_stores==DeadAttachmentStoresBaseline(f)&&
           analysis.redundant_loads==RedundantAttachmentLoadsBaseline(f));
    auto reusable_final=sentinel;std::string reusable_error;
    const bool reusable_okay=ValidateFrame(f,initial,reusable_final,reusable_error,nullptr,scratch);
    assert(reusable_okay==old_okay&&reusable_error==old_error&&reusable_final==before);
    if(!reusable_okay)assert(scratch.draw_ranges.empty());scratch.Clear();
  }
  const auto old=Measure(many,true,80);
  std::cout<<"{\"differential_cases\":60000,\"accepted\":"<<accepted<<",\"rejected\":"<<rejected
    <<",\"baseline_allocations\":"<<old.first<<",\"baseline_ms\":"<<old.second
    <<",\"baseline_draw_admission_allocations\":"<<baseline_range_allocations
    <<",\"baseline_analysis_allocations\":"<<old_analysis.first<<",\"baseline_analysis_ms\":"<<old_analysis.second
    <<",\"candidate_analysis_allocations\":"<<new_analysis.first<<",\"candidate_analysis_ms\":"<<new_analysis.second<<",";
#else
  std::cout<<"{";
#endif
  std::cout<<"\"passes\":240,\"candidate_allocations\":"<<b.first<<",\"candidate_ms\":"<<b.second
    <<",\"one_pass_allocations\":"<<a.first<<",\"pass_heap_allocations\":0"
    <<",\"draws\":4800,\"reused_admission_allocations\":"<<scratch_allocations
    <<",\"warm_attachment_analysis_allocations\":"<<analysis_allocations<<"}\n";
}
