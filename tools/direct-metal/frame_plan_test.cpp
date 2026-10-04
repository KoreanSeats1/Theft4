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
  // Consecutive title draws retain their order in a single encoder. Admission
  // still sees the actual initial clear; later clears, target changes, copies,
  // discard stores and resolves are boundaries and cannot disappear.
  FramePlan joined;joined.sequence=1;joined.surfaces={SurfaceFor(101),SurfaceFor(102)};
  Pass begin;begin.colors[0]=Color(101);begin.colors[0]->clear_color={0.25,0,0,1};
  AppendPass(joined,begin);
  Pass continuation;continuation.colors[0]=Color(101,Load::Load);
  FrameDraw ordered;ordered.capture=DrawFor();continuation.commands.push_back(ordered);
  AppendPass(joined,continuation);AppendPass(joined,continuation);
  assert(joined.commands.size()==1&&std::get<Pass>(joined.commands[0]).commands.size()==2);
  assert(std::get<Pass>(joined.commands[0]).colors[0]->load==Load::Clear);
  assert(std::get<Pass>(joined.commands[0]).colors[0]->clear_color[0]==0.25);
  assert(ValidateFrame(joined,{},result,error));
  auto boundary=joined;AppendPass(boundary,begin);assert(boundary.commands.size()==2);
  boundary=joined;auto changed=continuation;changed.colors[0]->view.surface={102,1};
  AppendPass(boundary,changed);assert(boundary.commands.size()==2);
  boundary=joined;changed=continuation;changed.colors[0]->store=Store::Discard;
  AppendPass(boundary,changed);assert(boundary.commands.size()==2);
  boundary=joined;std::get<Pass>(boundary.commands.back()).colors[0]->store=Store::Discard;
  AppendPass(boundary,continuation);assert(boundary.commands.size()==2);
  boundary=joined;changed=continuation;changed.depth=Attachment{};
  AppendPass(boundary,changed);assert(boundary.commands.size()==2);
  boundary=joined;std::get<Pass>(boundary.commands.back()).colors[0]->resolve=SurfaceView{{102,1},0,0,Aspect::Color};
  AppendPass(boundary,continuation);assert(boundary.commands.size()==2);
  boundary=joined;boundary.commands.push_back(ImageCopy{});AppendPass(boundary,continuation);
  assert(boundary.commands.size()==3);
  FramePlan attachmentless;attachmentless.sequence=100;
  Pass no_targets;no_targets.attachmentless_extent={4,4};
  auto no_target_draw=DrawFor();no_target_draw->draw.pipeline.colors={};
  no_target_draw->draw.pipeline.fragment={};no_targets.commands={FrameDraw{no_target_draw,{}}};
  AppendPass(attachmentless,no_targets);
  const SurfaceContents retained{{{999,1},0,0,Aspect::Color}};
  assert(ValidateFrame(attachmentless,{},result,error)&&result.empty());
  attachmentless.surfaces={SurfaceFor(999)};
  assert(ValidateFrame(attachmentless,retained,result,error)&&result==retained);
  AppendPass(attachmentless,no_targets);assert(attachmentless.commands.size()==1);
  auto other_extent=no_targets;other_extent.attachmentless_extent={8,4};
  AppendPass(attachmentless,other_extent);assert(attachmentless.commands.size()==2);
  attachmentless.commands.pop_back();
  auto bad_no_targets=attachmentless;
  std::get<Pass>(bad_no_targets.commands[0]).attachmentless_extent={};
  assert(!ValidateFrame(bad_no_targets,retained,result,error));
  bad_no_targets=attachmentless;std::get<Pass>(bad_no_targets.commands[0]).attachmentless_extent={16385,4};
  assert(!ValidateFrame(bad_no_targets,retained,result,error));
  bad_no_targets=attachmentless;auto no_target_mismatch=std::make_shared<Capture>(*no_target_draw);
  no_target_mismatch->draw.pipeline.samples=2;std::get<FrameDraw>(std::get<Pass>(bad_no_targets.commands[0]).commands[0]).capture=no_target_mismatch;
  assert(!ValidateFrame(bad_no_targets,retained,result,error));
  auto bad_attached=Plan();std::get<Pass>(bad_attached.commands[0]).attachmentless_extent={4,4};
  assert(!ValidateFrame(bad_attached,{},result,error));
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
  FramePlan combined;combined.sequence=3;
  auto ds1=SurfaceFor(110),ds2=SurfaceFor(111);ds1->format=ds2->format=Format::Depth32FloatStencil8;
  combined.surfaces={ds1,ds2};Pass initialize;
  initialize.depth=Color(110);initialize.depth->view.aspect=Aspect::Depth;
  initialize.stencil=initialize.depth;initialize.stencil->view.aspect=Aspect::Stencil;
  ImageCopy dsCopy{initialize.depth->view,{ds2->key,0,0,Aspect::Depth},{0,0},{0,0},{4,4},true};
  combined.commands={initialize,dsCopy};assert(ValidateFrame(combined,{},result,error));
  assert(result.contains(dsCopy.destination)&&result.contains({ds2->key,0,0,Aspect::Stencil}));
  bad=combined;std::get<ImageCopy>(bad.commands[1]).combined_depth_stencil=false;reject(bad);
  bad=combined;std::get<Pass>(bad.commands[0]).stencil.reset();reject(bad);
  bad=combined;std::get<ImageCopy>(bad.commands[1]).source.aspect=Aspect::Stencil;reject(bad);
  auto partialCombined=combined;std::get<ImageCopy>(partialCombined.commands[1]).extent={2,2};
  assert(ValidateFrame(partialCombined,{},result,error));
  assert(!result.contains(dsCopy.destination)&&!result.contains({ds2->key,0,0,Aspect::Stencil}));
  bad=copied;std::get<ImageCopy>(bad.commands.back()).combined_depth_stencil=true;reject(bad);
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
  // Complete sampled ranges must preserve every mip/face and reject feedback
  // into any member. Other mips and cube groups of the same backing can differ.
  FramePlan ranges;ranges.sequence=3;
  auto layered=SurfaceFor(20);layered->kind=ImageKind::Texture2DArray;layered->layers=12;layered->levels=3;
  auto output=SurfaceFor(21);ranges.surfaces={layered,output};
  for(uint32_t level=0;level<3;++level)for(uint32_t slice=0;slice<12;++slice) {
    Pass clear;clear.colors[0]=Color(20);clear.colors[0]->view.level=level;clear.colors[0]->view.slice=slice;
    ranges.commands.push_back(clear);
  }
  Pass sample;sample.colors[0]=Color(21);FrameDraw ranged;ranged.capture=DrawFor();
  SampledSurfaceView cube;cube.surface={20,1};cube.level=1;cube.levels=2;cube.slice=6;cube.slices=6;cube.kind=ImageKind::TextureCube;
  ranged.produced[0]=cube;sample.commands.push_back(ranged);ranges.commands.push_back(sample);
  ranges.output=sample.colors[0]->view;assert(ValidateFrame(ranges,{},result,error));
  assert(SampledViewDefined(cube,result));
  assert(SampledViewContains(cube,{{20,1},2,11,Aspect::Color}));
  assert(!SampledViewContains(cube,{{20,1},0,11,Aspect::Color}));
  assert(!SampledViewContains(cube,{{20,1},1,5,Aspect::Color}));
  const auto range=[](FramePlan& f)->SampledSurfaceView& {
    return *std::get<FrameDraw>(std::get<Pass>(f.commands.back()).commands[0]).produced[0];
  };
  bad=ranges;bad.commands.erase(bad.commands.begin()+23);reject(bad); // Missing mip 1, face 11.
  bad=ranges;range(bad).level=UINT32_MAX;reject(bad);
  bad=ranges;range(bad).levels=UINT32_MAX;reject(bad);
  bad=ranges;range(bad).slice=UINT32_MAX;reject(bad);
  bad=ranges;range(bad).slices=UINT32_MAX;reject(bad);
  bad=ranges;range(bad).slice=1;reject(bad);
  bad=ranges;range(bad).slices=5;reject(bad);
  bad=ranges;range(bad).kind=ImageKind::Texture2D;reject(bad);
  bad=ranges;range(bad).kind=ImageKind::Texture3D;reject(bad);
  bad=ranges;range(bad).format=Format::R32Float;reject(bad);
  bad=ranges;range(bad).swizzle[0]=Swizzle(uint32_t(Swizzle::Alpha)+1);reject(bad);
  auto viewed=ranges;range(viewed).format=Format::RGBA8Srgb;
  range(viewed).swizzle={Swizzle::Blue,Swizzle::Green,Swizzle::Red,Swizzle::One};
  assert(ValidateFrame(viewed,{},result,error));
  viewed=ranges;range(viewed).kind=ImageKind::Texture2DArray;
  assert(ValidateFrame(viewed,{},result,error));
  viewed=ranges;range(viewed).kind=ImageKind::TextureCubeArray;range(viewed).slice=0;range(viewed).slices=12;
  assert(ValidateFrame(viewed,{},result,error));
  auto feedback=ranges;auto small=std::make_shared<Surface>(*output);small->width=small->height=2;feedback.surfaces[1]=small;
  auto draw2=std::make_shared<Capture>(*DrawFor());draw2->width=draw2->height=2;
  draw2->draw.viewport={0,0,2,2,0,1};draw2->draw.scissor={0,0,2,2};draw2->draw.pipeline.colors[1]=Format::RGBA8Unorm;
  auto& last=std::get<Pass>(feedback.commands.back());std::get<FrameDraw>(last.commands[0]).capture=draw2;
  last.colors[1]=Color(20,Load::Load);last.colors[1]->view.level=1;last.colors[1]->view.slice=8;
  reject(feedback);assert(error=="Ordered pass samples its active render attachment");
  last.colors[1]->view.slice=0;assert(ValidateFrame(feedback,{},result,error)); // Different cube group.
  auto depthView=cube;depthView.surface={99,1};assert(!ValidateSampledView(ranges,depthView,error));
  std::cout<<"Ordered frame dependencies, content validity, resolve actions, generations and transactional rejection passed\n";
}
