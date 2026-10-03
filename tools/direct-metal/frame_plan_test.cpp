#include "theft4_frame_plan.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace theft4::render;
namespace {
auto SurfaceFor(uint64_t id,uint32_t samples=1) {
  auto s=std::make_shared<Surface>();s->key={id,1};s->format=Format::RGBA8Unorm;
  s->width=s->height=4;s->samples=samples;return s;
}
Attachment Color(uint64_t id,Load load=Load::Clear,Store store=Store::Store) {
  Attachment a;a.view={{id,1},0,0,Aspect::Color};a.load=load;a.store=store;return a;
}
auto DrawFor() {
  auto c=std::make_shared<Capture>();c->width=c->height=4;
  auto& d=c->draw;d.pipeline.vertex.hash=1;d.pipeline.fragment.hash=2;
  d.pipeline.colors[0]=Format::RGBA8Unorm;d.vertex_count=3;
  d.viewport={0,0,4,4,0,1};d.scissor={0,0,4,4};
  for(size_t i=0;i<3;++i){auto b=std::make_shared<Bytes>();b->generation=1;b->value.resize(i==0?4096:i==1?3584:1056);d.constants[i]={b,0,b->value.size()};}
  d.fetches[0].sampler=std::make_shared<Sampler>();return c;
}
FramePlan Plan() {
  FramePlan f;f.sequence=1;f.surfaces={SurfaceFor(1,4),SurfaceFor(2),SurfaceFor(3)};
  Pass first;first.colors[0]=Color(1,Load::Clear,Store::StoreAndResolve);
  first.colors[0]->resolve=SurfaceView{{2,1},0,0,Aspect::Color};
  Pass second;second.colors[0]=Color(3);FrameDraw d;d.capture=DrawFor();d.produced[0]=SurfaceView{{2,1},0,0,Aspect::Color};second.commands.push_back(d);
  f.commands={first,second};f.output=SurfaceView{{3,1},0,0,Aspect::Color};return f;
}
}
int main() {
  std::string error;SurfaceContents result;
  auto good=Plan();assert(ValidateFrame(good,{},result,error));assert(result.size()==3);
  const auto reject=[&](FramePlan f,const SurfaceContents& initial={}) {
    SurfaceContents untouched{{{999,1},0,0,Aspect::Color}};auto before=untouched;
    assert(!ValidateFrame(f,initial,untouched,error));assert(!error.empty());assert(untouched==before);
  };
  auto bad=Plan();std::get<Pass>(bad.commands[0]).colors[0]->load=Load::Load;reject(bad);
  bad=Plan();std::get<Pass>(bad.commands[0]).colors[0]->store=Store::Discard;reject(bad);
  bad=Plan();std::get<Pass>(bad.commands[0]).colors[0]->resolve.reset();reject(bad);
  bad=Plan();std::get<Pass>(bad.commands[0]).colors[0]->resolve->surface={1,1};reject(bad);
  bad=Plan();auto mismatch=SurfaceFor(2);mismatch->width=8;bad.surfaces[1]=mismatch;reject(bad);
  bad=Plan();std::get<Pass>(bad.commands[0]).colors[0]->filter=ResolveFilter::Min;reject(bad);
  bad=Plan();std::get<FrameDraw>(std::get<Pass>(bad.commands[1]).commands[0]).produced[0]=SurfaceView{{3,1},0,0,Aspect::Color};reject(bad);
  bad=Plan();std::get<FrameDraw>(std::get<Pass>(bad.commands[1]).commands[0]).produced[0]=SurfaceView{{2,2},0,0,Aspect::Color};reject(bad);
  bad=Plan();std::get<FrameDraw>(std::get<Pass>(bad.commands[1]).commands[0]).produced[0]->aspect=Aspect::Stencil;reject(bad);
  bad=Plan();std::get<Pass>(bad.commands[1]).colors[0]->store=Store::Discard;reject(bad);
  bad=Plan();std::get<Pass>(bad.commands[1]).colors[0]->load=Load::Discard;reject(bad);
  bad=Plan();std::get<Pass>(bad.commands[1]).colors[1]=std::get<Pass>(bad.commands[1]).colors[0];reject(bad);
  bad=Plan();auto draw=DrawFor();draw->draw.pipeline.samples=4;std::get<FrameDraw>(std::get<Pass>(bad.commands[1]).commands[0]).capture=draw;reject(bad);
  bad=Plan();draw=DrawFor();draw->draw.fetches[0].sampler.reset();std::get<FrameDraw>(std::get<Pass>(bad.commands[1]).commands[0]).capture=draw;reject(bad);
  bad=Plan();draw=DrawFor();draw->draw.pipeline.colors[0]=Format::BGRA8Unorm;std::get<FrameDraw>(std::get<Pass>(bad.commands[1]).commands[0]).capture=draw;reject(bad);
  bad=Plan();std::get<Pass>(bad.commands[0]).colors[0]->clear_color[0]=std::numeric_limits<double>::quiet_NaN();reject(bad);
  bad=Plan();bad.surfaces.push_back(bad.surfaces[0]);reject(bad);
  bad=Plan();std::get<FrameDraw>(std::get<Pass>(bad.commands[1]).commands[0]).capture.reset();reject(bad);
  auto next=Plan();next.commands={next.commands.back()};std::get<Pass>(next.commands[0]).colors[0]->load=Load::Load;
  assert(ValidateFrame(next,result,result,error));reject(next);
  auto discarded=Plan();std::get<Pass>(discarded.commands[0]).colors[0]->store=Store::Resolve;
  assert(ValidateFrame(discarded,{},result,error));assert(!result.contains({{1,1},0,0,Aspect::Color}));
  FramePlan mip;mip.sequence=2;auto array=SurfaceFor(5);array->kind=ImageKind::Texture2DArray;array->layers=2;array->levels=3;
  mip.surfaces={array};Pass p;p.colors[0]=Color(5);p.colors[0]->view.level=1;p.colors[0]->view.slice=1;
  mip.commands={p};mip.output=p.colors[0]->view;assert(ValidateFrame(mip,{},result,error));
  bad=mip;bad.output->slice=2;reject(bad);bad=mip;bad.output->level=3;reject(bad);
  auto copied=Plan();auto dst=SurfaceFor(4);copied.surfaces.push_back(dst);
  ImageCopy copy{{{3,1},0,0,Aspect::Color},{{4,1},0,0,Aspect::Color},{0,0},{0,0},{4,4}};
  copied.commands.push_back(copy);copied.output=copy.destination;assert(ValidateFrame(copied,{},result,error));
  bad=copied;std::get<ImageCopy>(bad.commands.back()).extent[0]=5;reject(bad);
  bad=copied;std::get<ImageCopy>(bad.commands.back()).source.surface={99,1};reject(bad);
  bad=copied;std::get<ImageCopy>(bad.commands.back()).destination=copy.source;reject(bad);
  bad=copied;std::get<ImageCopy>(bad.commands.back()).extent={2,2};reject(bad); // Partial copy never defines an entire new target.
  auto cleared=Plan();auto& pass=std::get<Pass>(cleared.commands[1]);
  pass.commands.clear();pass.colors[0]->load=Load::Discard;
  RectClear clear;clear.colors=1;clear.rectangle={0,0,4,4};pass.commands.push_back(clear);
  assert(ValidateFrame(cleared,{},result,error));
  bad=cleared;std::get<RectClear>(std::get<Pass>(bad.commands[1]).commands[0]).rectangle={1,1,2,2};reject(bad);
  bad=cleared;std::get<RectClear>(std::get<Pass>(bad.commands[1]).commands[0]).colors=2;reject(bad);
  bad=cleared;std::get<RectClear>(std::get<Pass>(bad.commands[1]).commands[0]).depth=true;reject(bad);
  bad=cleared;std::get<RectClear>(std::get<Pass>(bad.commands[1]).commands[0]).rectangle[0]=UINT32_MAX;reject(bad);
  auto utilities=Plan();auto& hostPass=std::get<Pass>(utilities.commands[1]);hostPass.commands.clear();
  HostDraw host;host.pipeline.colors[0]=Format::RGBA8Unorm;host.scissor={0,0,4,4};
  auto constants=std::make_shared<Bytes>();constants->generation=1;constants->value.resize(44);
  host.constants={constants,0,44};host.fetches[0].produced=SurfaceView{{2,1},0,0,Aspect::Color};
  host.fetches[0].sampler=std::make_shared<Sampler>();hostPass.commands.push_back(host);
  assert(ValidateFrame(utilities,{},result,error));
  const auto hostDraw=[](FramePlan& f)->HostDraw& {return std::get<HostDraw>(std::get<Pass>(f.commands[1]).commands[0]);};
  auto fullscreen=utilities;std::get<Pass>(fullscreen.commands[1]).colors[0]->load=Load::Discard;
  assert(ValidateFrame(fullscreen,{},result,error));
  bad=fullscreen;hostDraw(bad).scissor={1,1,2,2};reject(bad);
  bad=fullscreen;hostDraw(bad).pipeline.blends[0].write_mask=7;reject(bad);
  bad=fullscreen;hostDraw(bad).pipeline.blends[0].enabled=true;reject(bad);
  bad=utilities;hostDraw(bad).constants.length=43;reject(bad);
  bad=utilities;hostDraw(bad).constants.offset=UINT64_MAX;reject(bad);
  bad=utilities;hostDraw(bad).program=HostProgram::Count;reject(bad);
  bad=utilities;hostDraw(bad).fetches[0].produced->surface={3,1};reject(bad);
  bad=utilities;hostDraw(bad).fetches[1]=hostDraw(bad).fetches[0];reject(bad);
  bad=utilities;hostDraw(bad).pipeline.vertex.hash=1;reject(bad);
  bad=utilities;hostDraw(bad).pipeline.sample_mask=0;reject(bad);
  bad=utilities;hostDraw(bad).pipeline.depth_write=true;reject(bad);
  bad=utilities;auto invalidSampler=std::make_shared<Sampler>();invalidSampler->anisotropy=0;
  hostDraw(bad).fetches[0].sampler=invalidSampler;reject(bad);
  // The resolve program samples an MSAA owner directly, not its resolved image.
  host.program=HostProgram::ResolveMSAA;constants=std::make_shared<Bytes>();constants->generation=1;constants->value.resize(64);
  host.constants={constants,0,64};host.fetches[0].produced->surface={1,1};hostPass.commands={host};
  assert(ValidateFrame(utilities,{},result,error));
  bad=utilities;hostDraw(bad).fetches[0].produced->surface={2,1};reject(bad);
  std::cout<<"Ordered frame dependencies, content validity, resolve actions, generations and transactional rejection passed\n";
}
