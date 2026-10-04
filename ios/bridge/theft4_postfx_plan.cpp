#include "theft4_postfx_plan.h"
#include "../../LibertyRecompLib/postfx/smaa/AreaTex.h"
#include "../../LibertyRecompLib/postfx/smaa/SearchTex.h"
#include "../../glue/rexglue-sdk-main/src/graphics/gta4_native/native_postfx_plan.h"
#include <algorithm>
#include <cstring>
namespace theft4::render {
namespace {
bool Fail(std::string& error,const char* message){error=message;return false;}
void Retain(FramePlan& f,const SurfaceOwner& s) {
  if(std::none_of(f.surfaces.begin(),f.surfaces.end(),[&](const auto& old){return old->key==s->key;}))f.surfaces.push_back(s);
}
std::shared_ptr<const Sampler> SamplerFor(bool linear) {
  auto sampler=std::make_shared<Sampler>();sampler->min_linear=sampler->mag_linear=linear;
  sampler->address.fill(Address::ClampEdge);return sampler;
}
HostFetch Input(const SurfaceOwner& surface,bool linear=true) {
  HostFetch f;f.produced=SurfaceView{surface->key,0,0,Aspect::Color};f.sampler=SamplerFor(linear);return f;
}
HostFetch Linear(HostFetch f){f.sampler=SamplerFor(true);return f;}
HostFetch SnapshotAlias(HostFetch f,const SurfaceOwner& scene,const SurfaceOwner& snapshot) {
  if(f.produced&&f.produced->surface==scene->key)f.produced->surface=snapshot->key;
  return Linear(std::move(f));
}
template<class T> Buffer Constants(const T& value,uint64_t& generation) {
  auto bytes=std::make_shared<Bytes>();bytes->generation=++generation;
  const auto* data=reinterpret_cast<const uint8_t*>(&value);bytes->value.assign(data,data+sizeof(T));
  return {bytes,0,sizeof(T)};
}
bool Target(const SurfaceOwner& surface,uint32_t width,uint32_t height,Format format,std::string& error) {
  return surface&&surface->width==width&&surface->height==height&&surface->format==format&&surface->samples==1&&
      surface->levels==1&&surface->layers==1&&surface->kind==ImageKind::Texture2D?
      true:Fail(error,"Utility allocator returned incompatible storage");
}
void PassFor(FramePlan& plan,SurfaceContents& contents,const SurfaceOwner& output,HostDraw draw,bool discard=false) {
  Retain(plan,output);Attachment a;a.view={output->key,0,0,Aspect::Color};
  a.load=discard?Load::Discard:Load::Clear;a.store=Store::Store;
  draw.pipeline.colors[0]=output->format;draw.scissor={0,0,output->width,output->height};
  Pass pass;pass.colors[0]=a;pass.commands.push_back(std::move(draw));AppendPass(plan,std::move(pass));
  // Clear is a real full write. Present's full generated triangle is separately
  // proven by ValidateFrame, including when the destination is a drawable.
  contents.insert(a.view);
}
}
SmaaLookups MakeSmaaLookups(uint64_t& generation) {
  const auto image=[&](Format format,uint32_t width,uint32_t height,uint64_t row,const auto& data) {
    auto bytes=std::make_shared<Bytes>();bytes->generation=++generation;bytes->value.assign(std::begin(data),std::end(data));
    auto result=std::make_shared<Image>();result->source=bytes;result->format=format;result->width=width;result->height=height;
    result->mips.push_back({0,0,width,height,1,row,row*height,0,sizeof(data)});return result;
  };
  return {image(Format::RG8Unorm,AREATEX_WIDTH,AREATEX_HEIGHT,AREATEX_PITCH,areaTexBytes),
          image(Format::R8Unorm,SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,SEARCHTEX_PITCH,searchTexBytes)};
}
bool AppendSplitPostFx(FramePlan& f,SurfaceContents& contents,const SurfaceOwner& scene,
    const HostFetch& depth,const HostFetch& mask,SplitConstants constants,const UtilityAllocator& allocate,
    uint64_t& generation,std::string& error) {
  if(!scene||scene->samples!=1||scene->kind!=ImageKind::Texture2D||!depth.sampler||!mask.sampler)
    return Fail(error,"Split post-FX input shape unavailable");
  Retain(f,scene);const uint32_t w=scene->width,h=scene->height;
  auto snapshot=allocate("scene-snapshot",w,h,scene->format);
  if(!Target(snapshot,w,h,scene->format,error))return false;
  Retain(f,snapshot);f.commands.push_back(ImageCopy{{scene->key,0,0,Aspect::Color},
      {snapshot->key,0,0,Aspect::Color},{0,0},{0,0},{w,h}});contents.insert({snapshot->key,0,0,Aspect::Color});
  const auto depth_input=SnapshotAlias(depth,scene,snapshot),mask_input=SnapshotAlias(mask,scene,snapshot);
  const bool dof=!rex::graphics::gta4_native::NativeDofCanBeElided(constants.projection,constants.distance,constants.blur);
  SurfaceOwner full=scene,half_ping,half_pong;
  if(dof) {
    full=allocate("split-full",w,h,scene->format);
    half_ping=allocate("split-half-ping",(w+1)/2,(h+1)/2,scene->format);
    half_pong=allocate("split-half-pong",(w+1)/2,(h+1)/2,scene->format);
    if(!Target(full,w,h,scene->format,error)||!Target(half_ping,(w+1)/2,(h+1)/2,scene->format,error)||
       !Target(half_pong,(w+1)/2,(h+1)/2,scene->format,error))return false;
  }
  const auto pass=[&](uint32_t index,SurfaceOwner input,SurfaceOwner blur,SurfaceOwner output) {
    constants.pass=index;constants.source={int32_t(input->width),int32_t(input->height)};
    constants.destination={int32_t(output->width),int32_t(output->height)};
    HostDraw draw;draw.program=HostProgram::SplitPostFx;draw.constants=Constants(constants,generation);
    draw.fetches={Input(input),Input(blur),depth_input,mask_input};PassFor(f,contents,output,std::move(draw));
  };
  pass(0,snapshot,snapshot,full);
  if(dof){pass(1,full,full,half_ping);pass(2,half_ping,half_ping,half_pong);pass(3,full,half_pong,scene);}
  error.clear();return true;
}
bool AppendSunShafts(FramePlan& f,SurfaceContents& contents,const SurfaceOwner& scene,
    const HostFetch& depth,SunConstants constants,const UtilityAllocator& allocate,uint64_t& generation,std::string& error) {
  if(!scene||scene->samples!=1||scene->kind!=ImageKind::Texture2D)return Fail(error,"Sun-shaft scene shape unavailable");
  const auto w=scene->width,h=scene->height;
  auto pre=allocate("sun-pre",(w+1)/2,(h+1)/2,scene->format);
  auto ping=allocate("sun-ping",(w+1)/2,(h+1)/2,scene->format),pong=allocate("sun-pong",(w+1)/2,(h+1)/2,scene->format);
  auto output=allocate("sun-output",w,h,scene->format);
  for(const auto& target:{pre,ping,pong})if(!Target(target,(w+1)/2,(h+1)/2,scene->format,error))return false;
  if(!Target(output,w,h,scene->format,error))return false;
  // The existing sun chain samples the scene and writes separate intermediates,
  // then copies the completed output back to the same title-owned scene texture.
  const auto pass=[&](uint32_t index,SurfaceOwner input,SurfaceOwner target,uint32_t sw,uint32_t sh) {
    constants.pass=index;constants.source={int32_t(sw),int32_t(sh)};
    constants.destination={int32_t(target->width),int32_t(target->height)};
    HostDraw draw;draw.program=HostProgram::SunShafts;draw.constants=Constants(constants,generation);
    draw.fetches[0]=Input(scene);draw.fetches[1]=Input(input);draw.fetches[2]=Linear(depth);
    PassFor(f,contents,target,std::move(draw));
  };
  Retain(f,scene);pass(0,scene,pre,w,h);pass(1,pre,ping,pre->width,pre->height);
  pass(2,ping,pong,ping->width,ping->height);pass(3,pong,output,pong->width,pong->height);
  f.commands.push_back(ImageCopy{{output->key,0,0,Aspect::Color},{scene->key,0,0,Aspect::Color},{0,0},{0,0},{w,h}});
  contents.insert({scene->key,0,0,Aspect::Color});error.clear();return true;
}
bool AppendPresentation(FramePlan& f,SurfaceContents& contents,HostFetch source,const SurfaceOwner& output,
    PresentConstants constants,int quality,const SmaaLookups& lookup,const UtilityAllocator& allocate,
    uint64_t& generation,std::string& error) {
  if(!output||output->samples!=1||!source.sampler||constants.source_width<=0||constants.source_height<=0||
     constants.destination_width!=int32_t(output->width)||constants.destination_height!=int32_t(output->height)||quality>3)
    return Fail(error,"Presentation shape or SMAA quality invalid");
  source=Linear(source);
  if(quality>=0) {
    if(!lookup.area||!lookup.search)return Fail(error,"SMAA lookup ownership missing");
    const auto w=uint32_t(constants.source_width),h=uint32_t(constants.source_height);
    auto edges=allocate("smaa-edges",w,h,Format::RG8Unorm),weights=allocate("smaa-weights",w,h,Format::RGBA8Unorm);
    auto blended=allocate("smaa-output",w,h,Format::RGBA16Float);
    if(!Target(edges,w,h,Format::RG8Unorm,error)||!Target(weights,w,h,Format::RGBA8Unorm,error)||
       !Target(blended,w,h,Format::RGBA16Float,error))return false;
    const std::array<float,4> dimensions{1.0f/w,1.0f/h,float(w),float(h)};auto bank=Constants(dimensions,generation);
    HostDraw edge;edge.program=HostProgram(uint32_t(HostProgram::SmaaEdgeLow)+quality*2);edge.constants=bank;edge.fetches[0]=source;
    PassFor(f,contents,edges,std::move(edge));
    HostDraw weight;weight.program=HostProgram(uint32_t(HostProgram::SmaaWeightLow)+quality*2);weight.constants=bank;
    weight.fetches[0]=Input(edges);weight.fetches[1].image=lookup.area;weight.fetches[1].sampler=SamplerFor(true);
    weight.fetches[2].image=lookup.search;weight.fetches[2].sampler=SamplerFor(false);PassFor(f,contents,weights,std::move(weight));
    HostDraw neighborhood;neighborhood.program=HostProgram::SmaaNeighborhood;neighborhood.constants=bank;
    neighborhood.fetches[0]=source;neighborhood.fetches[1]=Input(weights);PassFor(f,contents,blended,std::move(neighborhood));
    source=Input(blended);
  }
  HostDraw present;present.program=HostProgram::Present;present.constants=Constants(constants,generation);
  present.fetches[0]=source;PassFor(f,contents,output,std::move(present),true);
  f.output=SurfaceView{output->key,0,0,Aspect::Color};error.clear();return true;
}
}
