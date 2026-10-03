#include "theft4_frame_plan.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <map>

namespace theft4::render {
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
namespace {
bool Reject(std::string& error,const char* message){error=message;return false;}
bool View(const FramePlan& f,const SurfaceView& v) {
  const auto* s=FindSurface(f,v.surface);
  return s&&v.level<s->levels&&v.slice<SurfaceSlices(*s)&&SupportsAspect(s->format,v.aspect);
}
uint32_t Width(const Surface& s,const SurfaceView& v){return std::max(1u,s.width>>v.level);}
uint32_t Height(const Surface& s,const SurfaceView& v){return std::max(1u,s.height>>v.level);}
bool SameStorage(const SurfaceView& a,const SurfaceView& b) {
  return a.surface==b.surface&&a.level==b.level&&a.slice==b.slice;
}
}
bool ValidateFrame(const FramePlan& f,const SurfaceContents& initial,
                   SurfaceContents& final,std::string& error) {
  if(!f.sequence||f.surfaces.empty()||f.surfaces.size()>4096||f.commands.empty()||f.commands.size()>4096)
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
  SurfaceContents contents;
  for(const auto& v:initial)if(FindSurface(f,v.surface)) {
    if(!View(f,v))return Reject(error,"Initial frame content has an invalid view");
    contents.insert(v);
  }
  size_t draw_count=0;
  for(const auto& command:f.commands) {
    if(const auto* copy=std::get_if<ImageCopy>(&command)) {
      if(!View(f,copy->source)||!View(f,copy->destination)||
         !contents.contains(copy->source)||SameStorage(copy->source,copy->destination)||
         copy->source.aspect!=copy->destination.aspect)
        return Reject(error,"Image copy has an undefined source or aliased destination");
      const auto* src=FindSurface(f,copy->source.surface);
      const auto* dst=FindSurface(f,copy->destination.surface);
      if(src->format!=dst->format||src->samples!=1||dst->samples!=1||
         src->format==Format::Depth32FloatStencil8)
        return Reject(error,"Image copy requires exact unscaled single-aspect storage");
      const auto region=[&](const Surface& s,const SurfaceView& v,const std::array<uint32_t,2>& origin) {
        return copy->extent[0]&&copy->extent[1]&&origin[0]<=Width(s,v)&&origin[1]<=Height(s,v)&&
          copy->extent[0]<=Width(s,v)-origin[0]&&copy->extent[1]<=Height(s,v)-origin[1];
      };
      if(!region(*src,copy->source,copy->source_origin)||!region(*dst,copy->destination,copy->destination_origin))
        return Reject(error,"Image copy region is outside its subresource");
      if(copy->destination_origin==std::array<uint32_t,2>{0,0}&&
         copy->extent==std::array<uint32_t,2>{Width(*dst,copy->destination),Height(*dst,copy->destination)})
        contents.insert(copy->destination);
      continue;
    }
    const auto& pass=std::get<Pass>(command);
    std::vector<const Attachment*> attachments;
    for(const auto& a:pass.colors)if(a)attachments.push_back(&*a);
    if(pass.depth)attachments.push_back(&*pass.depth);
    if(pass.stencil)attachments.push_back(&*pass.stencil);
    if(attachments.empty())return Reject(error,"Ordered pass has no attachments");
    uint32_t width=0,height=0,samples=0;
    std::set<SurfaceView> occupied;
    std::vector<SurfaceView> writes;
    const auto role=[&](const std::optional<Attachment>& a,Aspect aspect) {
      return !a||a->view.aspect==aspect;
    };
    for(const auto& a:pass.colors)if(!role(a,Aspect::Color))return Reject(error,"Invalid color attachment aspect");
    if(!role(pass.depth,Aspect::Depth)||!role(pass.stencil,Aspect::Stencil))
      return Reject(error,"Invalid depth/stencil attachment aspect");
    for(const auto* a:attachments) {
      if(!View(f,a->view)||a->load>=Load::Count||a->store>=Store::Count||
         !occupied.insert(a->view).second||!std::isfinite(a->clear_depth)||
         a->clear_depth<0||a->clear_depth>1||a->clear_stencil>255)
        return Reject(error,"Invalid ordered frame attachment");
      for(double c:a->clear_color)if(!std::isfinite(c))return Reject(error,"Nonfinite frame clear color");
      const auto* s=FindSurface(f,a->view.surface);
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
        if(!View(f,*a->resolve)||a->resolve->aspect!=a->view.aspect||s->samples==1||
           SameStorage(a->view,*a->resolve)||occupied.contains(*a->resolve))
          return Reject(error,"Invalid ordered pass resolve destination");
        const auto* dst=FindSurface(f,a->resolve->surface);
        if(dst->samples!=1||dst->format!=s->format||Width(*dst,*a->resolve)!=w||Height(*dst,*a->resolve)!=h)
          return Reject(error,"Ordered pass resolve format or extent differs");
        occupied.insert(*a->resolve);writes.push_back(*a->resolve);
      }
    }
    if(pass.depth&&pass.stencil) {
      const auto* depth=FindSurface(f,pass.depth->view.surface);
      const auto* stencil=FindSurface(f,pass.stencil->view.surface);
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
      const auto& item=std::get<FrameDraw>(command);
      if(!item.capture)return Reject(error,"Ordered pass has a missing draw");
      if(!Validate(*item.capture,error))return false;
      const auto& c=*item.capture;const auto& p=c.draw.pipeline;
      if(c.width!=width||c.height!=height||p.samples!=samples)return Reject(error,"Draw extent differs from its ordered pass");
      for(size_t i=0;i<4;++i) {
        const auto format=pass.colors[i] ? FindSurface(f,pass.colors[i]->view.surface)->format : Format::Invalid;
        if(p.colors[i]!=format)return Reject(error,"Draw color formats differ from its ordered pass");
      }
      const auto depth=pass.depth ? FindSurface(f,pass.depth->view.surface)->format : Format::Invalid;
      const auto stencil=pass.stencil ? FindSurface(f,pass.stencil->view.surface)->format : Format::Invalid;
      if(p.depth!=depth||p.stencil!=stencil)return Reject(error,"Draw depth/stencil differs from its ordered pass");
      for(size_t slot=0;slot<item.produced.size();++slot)if(item.produced[slot]) {
        const auto& view=*item.produced[slot];
        if(!View(f,view)||FindSurface(f,view.surface)->samples!=1||view.aspect==Aspect::Stencil||
           !contents.contains(view)||c.draw.fetches[slot].image||!c.draw.fetches[slot].sampler)
          return Reject(error,"Draw samples unavailable GPU-produced content");
        for(const auto& attachment:writes)if(SameStorage(view,attachment))
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
  if(f.output&&(!View(f,*f.output)||f.output->aspect!=Aspect::Color||
               FindSurface(f,f.output->surface)->samples!=1||!contents.contains(*f.output)))
    return Reject(error,"Frame output is unavailable or discarded");
  final=std::move(contents);error.clear();return true;
}
}
