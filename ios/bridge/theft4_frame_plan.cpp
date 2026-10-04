#include "theft4_frame_plan.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <map>
#include <cstring>

namespace theft4::render {
std::vector<uint8_t> DeadAttachmentStores(const FramePlan& f) {
  struct Pending {size_t command;uint8_t slot;};
  std::map<SurfaceKey,std::map<SurfaceView,Pending>> pending;
  std::vector<uint8_t> masks(f.commands.size(),0);
  const auto overwritten_loads=RedundantAttachmentLoads(f);
  const auto read=[&](const SampledSurfaceView& view){
    auto found=pending.find(view.surface);if(found==pending.end())return;
    std::erase_if(found->second,[&](const auto& entry){return SampledViewContains(view,entry.first);});
  };
  const auto overwrite=[&](SurfaceView view){
    auto found=pending.find(view.surface);if(found==pending.end())return;
    auto prior=found->second.find(view);if(prior==found->second.end())return;
    masks[prior->second.command]|=uint8_t(1u<<prior->second.slot);found->second.erase(prior);
  };
  for(size_t n=0;n<f.commands.size();++n){
    if(const auto* copy=std::get_if<ImageCopy>(&f.commands[n])){
      read(copy->source);
      if(copy->combined_depth_stencil){auto source=copy->source;source.aspect=Aspect::Stencil;read(source);}
      const auto* target=FindSurface(f,copy->destination.surface);
      const bool full=target&&copy->destination_origin==std::array<uint32_t,2>{0,0}&&
        copy->extent==std::array<uint32_t,2>{std::max(1u,target->width>>copy->destination.level),std::max(1u,target->height>>copy->destination.level)};
      if(full)overwrite(copy->destination);else read(copy->destination);
      if(copy->combined_depth_stencil){auto destination=copy->destination;destination.aspect=Aspect::Stencil;if(full)overwrite(destination);else read(destination);}
      continue;
    }
    const auto& pass=std::get<Pass>(f.commands[n]);
    for(const auto& c:pass.commands){
      if(const auto* draw=std::get_if<FrameDraw>(&c)){for(const auto& view:draw->produced)if(view)read(*view);}
      else if(const auto* host=GetHostDraw(c)){for(const auto& input:host->fetches)if(input.produced)read(*input.produced);}
    }
    const auto attachment=[&](const std::optional<Attachment>& a,uint8_t slot){
      if(!a)return;
      if(a->load==Load::Load&&!(overwritten_loads[n]&(1u<<slot)))read(a->view);else overwrite(a->view);
      // Keep resolves, final stores and partially updated targets intact.
      if(a->store==Store::Store&&!a->resolve)pending[a->view.surface][a->view]={n,slot};
      else if(auto found=pending.find(a->view.surface);found!=pending.end())found->second.erase(a->view);
    };
    for(uint8_t slot=0;slot<4;++slot)attachment(pass.colors[slot],slot);
    attachment(pass.depth,4);attachment(pass.stencil,5);
  }
  return masks;
}
namespace {
bool FullHostColorOverwrite(const FramePlan& f,const Pass& pass,const HostDraw& host) {
  if(!pass.colors[0]||pass.depth||pass.stencil||host.pipeline.depth_test||host.pipeline.stencil_test||
     host.pipeline.blends[0].enabled||host.pipeline.blends[0].write_mask!=15)return false;
  for(size_t i=1;i<4;++i)if(pass.colors[i])return false;
  const auto* target=FindSurface(f,pass.colors[0]->view.surface);if(!target)return false;
  if(!target->samples||target->samples>8||host.pipeline.samples!=target->samples||
     (host.pipeline.sample_mask&((1u<<target->samples)-1))!=((1u<<target->samples)-1))return false;
  const auto w=std::max(1u,target->width>>pass.colors[0]->view.level),h=std::max(1u,target->height>>pass.colors[0]->view.level);
  if(host.scissor!=std::array<uint32_t,4>{0,0,w,h})return false;
  // These shaders contain no discard and always write the complete color.
  switch(host.program) {
    case HostProgram::Resolve:case HostProgram::ResolveMSAA:
    case HostProgram::Present:case HostProgram::SplitPostFx:case HostProgram::SunShafts:
    case HostProgram::SmaaNeighborhood:case HostProgram::SmaaPresent:
    case HostProgram::SmaaHardwarePresent:case HostProgram::SmaaHardwareNeighborhood:return true;
    default:return false;
  }
}
}
std::vector<uint8_t> RedundantAttachmentLoads(const FramePlan& f) {
  std::vector<uint8_t> masks(f.commands.size(),0);
  for(size_t i=0;i<f.commands.size();++i)if(const auto* pass=std::get_if<Pass>(&f.commands[i]))
    if(!pass->commands.empty())if(const auto* host=GetHostDraw(pass->commands.front()))
      if(FullHostColorOverwrite(f,*pass,*host)&&pass->colors[0]->load!=Load::Discard)masks[i]=1;
  return masks;
}
std::optional<ImageCopy> IdentityResolveCopy(const FramePlan& f,const Pass& pass) {
  if(pass.commands.size()!=1)return {};
  const auto* host=GetHostDraw(pass.commands[0]);
  if(!host||host->program!=HostProgram::Resolve||!FullHostColorOverwrite(f,pass,*host)||pass.colors[0]->resolve||
     pass.colors[0]->store!=Store::Store||!host->fetches[0].produced)return {};
  const auto& input=*host->fetches[0].produced;const auto& output=pass.colors[0]->view;
  const auto* source=FindSurface(f,input.surface);const auto* target=FindSurface(f,output.surface);
  const std::array identity{Swizzle::Red,Swizzle::Green,Swizzle::Blue,Swizzle::Alpha};
  if(!source||!target||source->samples!=1||target->samples!=1||input.surface==output.surface||
     input.aspect!=Aspect::Color||input.kind!=ImageKind::Texture2D||input.levels!=1||input.slices!=1||input.swizzle!=identity||
     source->format!=target->format||(input.format!=Format::Invalid&&input.format!=source->format)||
     (source->format!=Format::RGBA8Unorm&&source->format!=Format::BGRA8Unorm)||!host->constants.source)return {};
  const auto& bank=host->constants;const auto& data=bank.source->value;
  if(bank.offset>data.size()||bank.length<64||data.size()-bank.offset<64)return {};
  std::array<uint32_t,16> c{};std::memcpy(c.data(),data.data()+bank.offset,64);
  // Only the direct physical path with neutral exponent. No guest layout,
  // scale filtering, Float16 sanitization or out-of-range texel behavior.
  if(c[0]||c[1]||c[2]||c[3]||c[8]!=1||c[9]||c[10]||c[11]!=2)return {};
  const std::array<uint32_t,2> extent{std::max(1u,target->width>>output.level),std::max(1u,target->height>>output.level)};
  if(extent!=std::array<uint32_t,2>{std::max(1u,source->width>>input.level),std::max(1u,source->height>>input.level)}||
     extent!=std::array<uint32_t,2>{c[12],c[13]}||extent!=std::array<uint32_t,2>{c[14],c[15]})return {};
  return ImageCopy{{input.surface,input.level,input.slice,Aspect::Color},output,{},{},extent,false};
}
Pass& AppendPass(FramePlan& frame,Pass next) {
  auto* previous=frame.commands.empty()?nullptr:std::get_if<Pass>(&frame.commands.back());
  const auto compatible=[](const std::optional<Attachment>& a,const std::optional<Attachment>& b) {
    if(bool(a)!=bool(b))return false;
    return !a||(a->view==b->view&&!a->resolve&&!b->resolve&&
        a->store==Store::Store&&b->store==Store::Store&&b->load==Load::Load);
  };
  bool merge=previous&&previous->attachmentless_extent==next.attachmentless_extent&&compatible(previous->depth,next.depth)&&compatible(previous->stencil,next.stencil);
  if(merge)for(size_t slot=0;slot<next.colors.size();++slot)
    merge&=compatible(previous->colors[slot],next.colors[slot]);
  if(merge) {
    previous->commands.insert(previous->commands.end(),std::make_move_iterator(next.commands.begin()),
                              std::make_move_iterator(next.commands.end()));
    return *previous;
  }
  frame.commands.push_back(std::move(next));return std::get<Pass>(frame.commands.back());
}
const Surface* FindSurface(const FramePlan& f,SurfaceKey key) {
  for(const auto& s:f.surfaces)if(s&&s->key==key)return s.get();
  return nullptr;
}
uint32_t SurfaceSlices(const Surface& s) {
  return s.kind==ImageKind::TextureCube||s.kind==ImageKind::TextureCubeArray ? s.layers*6 : s.layers;
}
bool SupportsAspect(Format f,Aspect a) {
  switch(a) {
    case Aspect::Color:return f>Format::Invalid&&f<Format::Depth32Float;
    case Aspect::Depth:return f==Format::Depth32Float||f==Format::Depth32FloatStencil8;
    case Aspect::Stencil:return f==Format::Stencil8||f==Format::Depth32FloatStencil8;
    default:return false;
  }
}
bool SampledViewContains(const SampledSurfaceView& sampled,const SurfaceView& view) {
  return sampled.surface==view.surface&&view.level>=sampled.level&&
    view.level-sampled.level<sampled.levels&&view.slice>=sampled.slice&&
    view.slice-sampled.slice<sampled.slices;
}
bool SampledViewDefined(const SampledSurfaceView& view,const SurfaceContents& contents) {
  // Bounds also constrain iteration when this helper is used independently
  // of frame admission. Mutable allocation declarations allow at most these.
  if(!view.levels||view.levels>15||!view.slices||view.slices>2048||
     view.level>UINT32_MAX-view.levels||view.slice>UINT32_MAX-view.slices)return false;
  for(uint32_t level=0;level<view.levels;++level)
    for(uint32_t slice=0;slice<view.slices;++slice)
      if(!contents.contains({view.surface,view.level+level,view.slice+slice,view.aspect}))return false;
  return true;
}
static bool ValidateSampledViewOf(const Surface* s,const SampledSurfaceView& view,std::string& error,bool allow_multisampled) {
  const auto reject=[&](const char* reason){error=reason;return false;};
  if(!s||s->kind==ImageKind::Texture3D||s->kind>=ImageKind::Count||(!allow_multisampled&&s->samples!=1)||!view.levels||!view.slices||view.level>=s->levels||
     view.levels>s->levels-view.level||view.slice>=SurfaceSlices(*s)||
     view.slices>SurfaceSlices(*s)-view.slice||!SupportsAspect(s->format,view.aspect))
    return reject("Sampled GPU view has an invalid allocation or range");
  if(s->samples>1&&(s->kind!=ImageKind::Texture2D||view.kind!=ImageKind::Texture2D||
     view.level||view.levels!=1||view.slice||view.slices!=1))
    return reject("Multisample sampling requires one complete 2D allocation");
  for(auto channel:view.swizzle)if(channel>Swizzle::Alpha)return reject("Invalid sampled GPU channel swizzle");
  const auto format=view.format==Format::Invalid ? s->format : view.format;
  const auto rgba=[](Format f){return f==Format::RGBA8Unorm||f==Format::RGBA8Srgb;};
  const auto bgra=[](Format f){return f==Format::BGRA8Unorm||f==Format::BGRA8Srgb;};
  if(!SupportsAspect(format,view.aspect)||
     (format!=s->format&&!(rgba(format)&&rgba(s->format))&&!(bgra(format)&&bgra(s->format))))
    return reject("Sampled GPU format reinterpretation needs explicit lowering");
  const std::array identity{Swizzle::Red,Swizzle::Green,Swizzle::Blue,Swizzle::Alpha};
  if(view.aspect!=Aspect::Color&&view.swizzle!=identity)return reject("Depth/stencil sampling requires its native channel contract");
  const bool array=s->kind==ImageKind::Texture2DArray||s->kind==ImageKind::TextureCube||s->kind==ImageKind::TextureCubeArray;
  switch(view.kind) {
    case ImageKind::Texture2D:
      if(view.slices!=1)return reject("A sampled 2D view must select one layer");
      break;
    case ImageKind::Texture2DArray:
      if(!array)return reject("Sampled array view requires layered storage");
      break;
    case ImageKind::TextureCube:case ImageKind::TextureCubeArray:
      if(!array||s->width!=s->height||view.slice%6||view.slices%6||
         (view.kind==ImageKind::TextureCube&&view.slices!=6))
        return reject("Sampled cube view requires aligned complete square faces");
      break;
    default:return reject("Sampled GPU view type needs explicit lowering");
  }
  error.clear();return true;
}
bool ValidateSampledView(const FramePlan& frame,const SampledSurfaceView& view,std::string& error,bool allow_multisampled) {
  return ValidateSampledViewOf(FindSurface(frame,view.surface),view,error,allow_multisampled);
}
namespace {
bool Reject(std::string& error,const char* message){error=message;return false;}
bool View(const Surface* s,const SurfaceView& v) {
  return s&&v.level<s->levels&&v.slice<SurfaceSlices(*s)&&SupportsAspect(s->format,v.aspect);
}
uint32_t Width(const Surface& s,const SurfaceView& v){return std::max(1u,s.width>>v.level);}
uint32_t Height(const Surface& s,const SurfaceView& v){return std::max(1u,s.height>>v.level);}
bool SameStorage(const SurfaceView& a,const SurfaceView& b) {
  return a.surface==b.surface&&a.level==b.level&&a.slice==b.slice;
}
}
bool ValidateFrame(const FramePlan& f,const SurfaceContents& initial,
                   SurfaceContents& final,std::string& error,IndexRangeCache* indices,
                   DrawVertexRanges* validated_draws) {
  DrawVertexRanges draw_ranges;
  if(!f.sequence||f.surfaces.size()>4096||f.commands.empty()||f.commands.size()>4096)
    return Reject(error,"Invalid ordered frame size or sequence");
  std::map<SurfaceKey,const Surface*> declarations;
  uint64_t subresources=0;
  for(const auto& s:f.surfaces) {
    if(!s||!s->key.id||!s->key.generation||!s->width||!s->height||s->width>16384||s->height>16384||
       !s->levels||s->levels>std::bit_width(std::max(s->width,s->height))||!s->layers||s->layers>2048||
       !s->samples||!std::has_single_bit(s->samples)||s->samples>8||
       (s->samples>1&&(s->kind!=ImageKind::Texture2D||s->levels!=1))||
       s->kind==ImageKind::Texture3D||s->kind>=ImageKind::Count||
       ((s->kind==ImageKind::Texture2D||s->kind==ImageKind::TextureCube)&&s->layers!=1)||
       ((s->kind==ImageKind::TextureCube||s->kind==ImageKind::TextureCubeArray)&&s->width!=s->height)||
       SurfaceSlices(*s)>2048||(!SupportsAspect(s->format,Aspect::Color)&&
       !SupportsAspect(s->format,Aspect::Depth)&&!SupportsAspect(s->format,Aspect::Stencil))||
       !declarations.emplace(s->key,s.get()).second)
      return Reject(error,"Invalid or duplicate frame surface generation");
    subresources+=uint64_t(SurfaceSlices(*s))*s->levels;
  }
  if(subresources>32768)return Reject(error,"Ordered frame subresource budget exceeded");
  const auto find=[&](SurfaceKey key)->const Surface* {
    const auto i=declarations.find(key);return i==declarations.end()?nullptr:i->second;
  };
  const auto view_valid=[&](const SurfaceView& v){return View(find(v.surface),v);};
  const auto sampled_valid=[&](const SampledSurfaceView& v,std::string& e,bool ms=false) {
    return ValidateSampledViewOf(find(v.surface),v,e,ms);
  };
  SurfaceContents contents;
  DrawResourceValidationCache resources;
  for(const auto& v:initial)if(find(v.surface)) {
    if(!view_valid(v))return Reject(error,"Initial frame content has an invalid view");
    contents.insert(v);
  }
  size_t draw_count=0;
  for(const auto& command:f.commands) {
    if(const auto* copy=std::get_if<ImageCopy>(&command)) {
      if(!view_valid(copy->source)||!view_valid(copy->destination)||
         !contents.contains(copy->source)||SameStorage(copy->source,copy->destination)||
         copy->source.aspect!=copy->destination.aspect)
        return Reject(error,"Image copy has an undefined source or aliased destination");
      const auto* src=find(copy->source.surface);
      const auto* dst=find(copy->destination.surface);
      const bool combined=src->format==Format::Depth32FloatStencil8;
      if(src->format!=dst->format||src->samples!=1||dst->samples!=1||
         combined!=copy->combined_depth_stencil||
         (combined&&(copy->source.aspect!=Aspect::Depth||
           !contents.contains({copy->source.surface,copy->source.level,copy->source.slice,Aspect::Stencil}))))
        return Reject(error,"Image copy requires exact unscaled single-aspect storage");
      const auto region=[&](const Surface& s,const SurfaceView& v,const std::array<uint32_t,2>& origin) {
        return copy->extent[0]&&copy->extent[1]&&origin[0]<=Width(s,v)&&origin[1]<=Height(s,v)&&
          copy->extent[0]<=Width(s,v)-origin[0]&&copy->extent[1]<=Height(s,v)-origin[1];
      };
      if(!region(*src,copy->source,copy->source_origin)||!region(*dst,copy->destination,copy->destination_origin))
        return Reject(error,"Image copy region is outside its subresource");
      if(copy->destination_origin==std::array<uint32_t,2>{0,0}&&
         copy->extent==std::array<uint32_t,2>{Width(*dst,copy->destination),Height(*dst,copy->destination)}) {
        contents.insert(copy->destination);
        if(combined)contents.insert({copy->destination.surface,copy->destination.level,copy->destination.slice,Aspect::Stencil});
      }
      continue;
    }
    const auto& pass=std::get<Pass>(command);
    std::vector<const Attachment*> attachments;
    for(const auto& a:pass.colors)if(a)attachments.push_back(&*a);
    if(pass.depth)attachments.push_back(&*pass.depth);
    if(pass.stencil)attachments.push_back(&*pass.stencil);
    uint32_t width=0,height=0,samples=0;
    if(attachments.empty()) {
      width=pass.attachmentless_extent[0];height=pass.attachmentless_extent[1];samples=1;
      if(!width||!height||width>16384||height>16384)
        return Reject(error,"Invalid attachmentless pass extent");
    } else if(pass.attachmentless_extent!=std::array<uint32_t,2>{})
      return Reject(error,"Attached pass has an attachmentless extent");
    std::set<SurfaceView> occupied;
    std::vector<SurfaceView> writes;
    const auto role=[&](const std::optional<Attachment>& a,Aspect aspect) {
      return !a||a->view.aspect==aspect;
    };
    for(const auto& a:pass.colors)if(!role(a,Aspect::Color))return Reject(error,"Invalid color attachment aspect");
    if(!role(pass.depth,Aspect::Depth)||!role(pass.stencil,Aspect::Stencil))
      return Reject(error,"Invalid depth/stencil attachment aspect");
    for(const auto* a:attachments) {
      if(!view_valid(a->view)||a->load>=Load::Count||a->store>=Store::Count||
         !occupied.insert(a->view).second||!std::isfinite(a->clear_depth)||
         a->clear_depth<0||a->clear_depth>1||a->clear_stencil>255)
        return Reject(error,"Invalid ordered frame attachment");
      for(double c:a->clear_color)if(!std::isfinite(c))return Reject(error,"Nonfinite frame clear color");
      const auto* s=find(a->view.surface);
      const auto w=Width(*s,a->view),h=Height(*s,a->view);
      if(!width){width=w;height=h;samples=s->samples;}
      if(width!=w||height!=h||samples!=s->samples)return Reject(error,"Ordered pass attachment dimensions differ");
      for(const auto& previous:writes)if(SameStorage(previous,a->view)&&previous.aspect==a->view.aspect)
        return Reject(error,"Ordered pass writes an aliased attachment twice");
      writes.push_back(a->view);
      if(a->load==Load::Load&&!contents.contains(a->view))return Reject(error,"Ordered pass loads undefined content");
      if(a->load==Load::Clear)contents.insert(a->view);
      if(a->load==Load::Discard)contents.erase(a->view);
      const bool resolve=a->store==Store::Resolve||a->store==Store::StoreAndResolve;
      if(resolve!=bool(a->resolve))return Reject(error,"Ordered pass resolve/store actions disagree");
      if(a->resolve) {
        if(a->filter>=ResolveFilter::Count||
           (a->view.aspect==Aspect::Color&&a->filter!=ResolveFilter::Average)||
           (a->view.aspect==Aspect::Depth&&a->filter==ResolveFilter::Average)||
           (a->view.aspect==Aspect::Stencil&&a->filter!=ResolveFilter::Sample0))
          return Reject(error,"Unsupported ordered attachment resolve filter");
        if(!view_valid(*a->resolve)||a->resolve->aspect!=a->view.aspect||s->samples==1||
           SameStorage(a->view,*a->resolve)||occupied.contains(*a->resolve))
          return Reject(error,"Invalid ordered pass resolve destination");
        const auto* dst=find(a->resolve->surface);
        if(dst->samples!=1||dst->format!=s->format||Width(*dst,*a->resolve)!=w||Height(*dst,*a->resolve)!=h)
          return Reject(error,"Ordered pass resolve format or extent differs");
        occupied.insert(*a->resolve);writes.push_back(*a->resolve);
      }
    }
    if(pass.depth&&pass.stencil) {
      const auto* depth=find(pass.depth->view.surface);
      const auto* stencil=find(pass.stencil->view.surface);
      if((depth->format==Format::Depth32FloatStencil8||stencil->format==Format::Depth32FloatStencil8)&&
         !SameStorage(pass.depth->view,pass.stencil->view))
        return Reject(error,"Combined depth/stencil views must share an allocation");
    }
    draw_count+=pass.commands.size();if(draw_count>20000)return Reject(error,"Ordered frame draw budget exceeded");
    for(const auto& command:pass.commands) {
      if(const auto* clear=std::get_if<RectClear>(&command)) {
        const auto& rect=clear->rectangle;
        if((clear->colors&~15u)||(!clear->colors&&!clear->depth&&!clear->stencil)||
           !rect[2]||!rect[3]||rect[0]>width||rect[1]>height||
           rect[2]>width-rect[0]||rect[3]>height-rect[1]||
           !std::isfinite(clear->depth_value)||clear->depth_value<0||clear->depth_value>1||
           clear->stencil_value>255||
           (clear->depth&&!pass.depth)||(clear->stencil&&!pass.stencil))
          return Reject(error,"Invalid ordered rectangular clear");
        for(float value:clear->color)if(!std::isfinite(value))return Reject(error,"Nonfinite rectangular clear color");
        const bool full=rect==std::array<uint32_t,4>{0,0,width,height};
        for(size_t slot=0;slot<4;++slot)if(clear->colors&(1u<<slot)) {
          if(!pass.colors[slot])return Reject(error,"Rectangular clear selects an absent color attachment");
          if(full)contents.insert(pass.colors[slot]->view);
        }
        if(full&&clear->depth)contents.insert(pass.depth->view);
        if(full&&clear->stencil)contents.insert(pass.stencil->view);
        continue;
      }
      if(const auto* host=GetHostDraw(command)) {
        const auto& p=host->pipeline;const auto& rect=host->scissor;
        if(host->program>=HostProgram::Count||p.vertex!=Shader{}||p.fragment!=Shader{}||!p.attributes.empty()||
           p.negative_one_to_one||p.samples!=samples||host->stencil_front_reference>255||host->stencil_back_reference>255||
           !rect[2]||!rect[3]||rect[0]>width||rect[1]>height||rect[2]>width-rect[0]||rect[3]>height-rect[1])
          return Reject(error,"Invalid ordered host utility draw");
        if(!ValidateFixedPipeline(p,error))return false;
        if((p.sample_mask&((1u<<samples)-1))!=((1u<<samples)-1))
          return Reject(error,"Host utility sample-mask lowering is required");
        for(const auto& stream:p.streams)if(stream!=Stream{})return Reject(error,"Host utility has an unexpected vertex stream");
        for(float value:host->blend_color)if(!std::isfinite(value))return Reject(error,"Nonfinite host blend color");
        for(size_t i=0;i<4;++i) {
          const auto format=pass.colors[i] ? find(pass.colors[i]->view.surface)->format : Format::Invalid;
          if(p.colors[i]!=format)return Reject(error,"Host utility color formats differ from its ordered pass");
        }
        const auto depth=pass.depth ? find(pass.depth->view.surface)->format : Format::Invalid;
        const auto stencil=pass.stencil ? find(pass.stencil->view.surface)->format : Format::Invalid;
        if(p.depth!=depth||p.stencil!=stencil)return Reject(error,"Host utility depth/stencil differs from its ordered pass");
        const auto& info=kHostPrograms[size_t(host->program)];const auto& constants=host->constants;
        if(info.constants&&(!constants.source||!constants.source->generation||constants.offset%16||
           constants.source->value.size()>64*1024*1024||constants.offset>constants.source->value.size()||
           constants.length>constants.source->value.size()-constants.offset||constants.length<info.constants))
          return Reject(error,"Host utility constants do not match its ABI");
        if(!info.constants&&constants.source)return Reject(error,"Host utility has unexpected constants");
        for(size_t slot=0;slot<host->fetches.size();++slot) {
          const auto& input=host->fetches[slot];
          if(!(info.textures&(1u<<slot))) {
            if(input.produced||input.image||input.sampler)return Reject(error,"Host utility has an unexpected texture input");
            continue;
          }
          if(!input.sampler||!ValidateSampler(*input.sampler,error)||bool(input.produced)==bool(input.image))
            return Reject(error,"Host utility requires one image owner and a valid sampler per input");
          const bool multisampled=info.multisampled&(1u<<slot);
          if(input.image) {
            if(multisampled||input.image->kind!=ImageKind::Texture2D||!ValidateImage(*input.image,error))
              return Reject(error,"Host static texture type differs from its ABI");
          }else {
            const auto& view=*input.produced;
            if(!sampled_valid(view,error,multisampled)||view.kind!=ImageKind::Texture2D||view.levels!=1||view.slices!=1||
               !SampledViewDefined(view,contents)||(find(view.surface)->samples>1)!=multisampled)
              return Reject(error,"Host utility samples undefined or incorrectly sampled GPU content");
            for(const auto& attachment:writes)if(SampledViewContains(view,attachment))
              return Reject(error,"Host utility samples its active attachment");
          }
        }
        // This exact host program always writes color zero, with a generated
        // fullscreen triangle and no discard path. Proven complete coverage
        // permits DontCare instead of a redundant drawable clear/load.
        if(host->program==HostProgram::Present&&p.samples==1&&pass.colors[0]&&
           !p.depth_test&&!p.stencil_test&&!p.blends[0].enabled&&p.blends[0].write_mask==15&&
           rect==std::array<uint32_t,4>{0,0,width,height})contents.insert(pass.colors[0]->view);
        continue;
      }
      if(std::holds_alternative<HostCommand>(command))
        return Reject(error,"Ordered pass has a missing host utility draw");
      const auto& item=std::get<FrameDraw>(command);
      if(!item.capture)return Reject(error,"Ordered pass has a missing draw");
      uint64_t maximum_vertex=0;
      if(!Validate(*item.capture,error,nullptr,indices,validated_draws?&maximum_vertex:nullptr,&resources))return false;
      if(validated_draws)draw_ranges.push_back({item.capture.get(),maximum_vertex});
      const auto& c=*item.capture;const auto& p=c.draw.pipeline;
      if(c.width!=width||c.height!=height||p.samples!=samples)return Reject(error,"Draw extent differs from its ordered pass");
      for(size_t i=0;i<4;++i) {
        const auto format=pass.colors[i] ? find(pass.colors[i]->view.surface)->format : Format::Invalid;
        if(p.colors[i]!=format)return Reject(error,"Draw color formats differ from its ordered pass");
      }
      const auto depth=pass.depth ? find(pass.depth->view.surface)->format : Format::Invalid;
      const auto stencil=pass.stencil ? find(pass.stencil->view.surface)->format : Format::Invalid;
      if(p.depth!=depth||p.stencil!=stencil)return Reject(error,"Draw depth/stencil differs from its ordered pass");
      for(size_t slot=0;slot<item.produced.size();++slot)if(item.produced[slot]) {
        const auto& view=*item.produced[slot];
        if(!sampled_valid(view,error))return false;
        if(view.aspect==Aspect::Stencil||!SampledViewDefined(view,contents)||
           c.draw.fetches[slot].image||!c.draw.fetches[slot].sampler)
          return Reject(error,"Draw samples unavailable GPU-produced content");
        for(const auto& attachment:writes)if(SampledViewContains(view,attachment))
          return Reject(error,"Ordered pass samples its active render attachment");
      }
    }
    for(const auto* a:attachments) {
      // A draw can cover only a few pixels. Store preserves validity; it never
      // upgrades a discarded, uncleared target into fully defined content.
      const bool defined=contents.contains(a->view);
      if(a->resolve) {
        if(defined)contents.insert(*a->resolve);else contents.erase(*a->resolve);
      }
      if(a->store==Store::Discard||a->store==Store::Resolve)contents.erase(a->view);
    }
  }
  if(f.output&&(!view_valid(*f.output)||f.output->aspect!=Aspect::Color||
               find(f.output->surface)->samples!=1||!contents.contains(*f.output)))
    return Reject(error,"Frame output is unavailable or discarded");
  final=std::move(contents);if(validated_draws)*validated_draws=std::move(draw_ranges);
  error.clear();return true;
}
}
