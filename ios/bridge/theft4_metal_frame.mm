#include "theft4_metal_frame.h"
#include <algorithm>
#include <bit>
#include <map>

namespace theft4::metal {
namespace {
constexpr uint64_t kSurfaceBudget=1024ull*1024*1024;
MTLTextureType Type(render::ImageKind k) {
  const MTLTextureType types[]{MTLTextureType2D,MTLTextureType2DArray,MTLTextureType3D,MTLTextureTypeCube,MTLTextureTypeCubeArray};
  return types[size_t(k)];
}
size_t PixelBytes(render::Format f) {
  using F=render::Format;
  switch(f) {
    case F::R8Unorm:case F::Stencil8:return 1;
    case F::RG8Unorm:case F::R16Unorm:case F::R16Float:return 2;
    case F::RGBA16Unorm:case F::RGBA16Float:case F::RG32Float:case F::Depth32FloatStencil8:return 8;
    case F::RGBA32Float:return 16;
    default:return 4;
  }
}
uint64_t SurfaceBytes(const render::Surface& s) {
  uint64_t bytes=0;
  for(uint32_t level=0;level<s.levels;++level)
    bytes+=uint64_t(std::max(1u,s.width>>level))*std::max(1u,s.height>>level)*PixelBytes(s.format)*
           render::SurfaceSlices(s)*s.samples;
  return bytes;
}
MTLLoadAction Load(render::Load value){return value==render::Load::Clear ? MTLLoadActionClear :
  value==render::Load::Load ? MTLLoadActionLoad : MTLLoadActionDontCare;}
MTLStoreAction Store(render::Store value) {
  switch(value){case render::Store::Store:return MTLStoreActionStore;
    case render::Store::Resolve:return MTLStoreActionMultisampleResolve;
    case render::Store::StoreAndResolve:return MTLStoreActionStoreAndMultisampleResolve;
    default:return MTLStoreActionDontCare;}
}
}
struct FrameAdapter::Impl {
  struct Entry {
    std::weak_ptr<const render::Surface> owner;
    render::Surface descriptor;
    id<MTLTexture> texture=nil;
    std::map<render::SurfaceView,id<MTLTexture>> views;
  };
  Renderer& renderer;
  PlanAdapter draws;
  std::map<render::SurfaceKey,Entry> surfaces;
  render::SurfaceContents contents;
  FrameResourceStats stats;
  explicit Impl(Renderer& r):renderer(r),draws(r){}
  void Forget(render::SurfaceKey key) {
    for(auto it=contents.begin();it!=contents.end();) {
      if(it->surface==key)it=contents.erase(it);else ++it;
    }
  }
  bool Ensure(const std::shared_ptr<const render::Surface>& s,std::string& error) {
    if(auto it=surfaces.find(s->key);it!=surfaces.end()) {
      const auto owner=it->second.owner.lock();
      if(owner) {
        if(owner.owner_before(s)||s.owner_before(owner)||it->second.descriptor!=*s) {
          error="Mutable Metal surface generation changed identity";return false;
        }
        return true;
      }
      stats.allocated_bytes-=SurfaceBytes(it->second.descriptor);surfaces.erase(it);Forget(s->key);
    }
    const auto bytes=SurfaceBytes(*s);
    if(bytes>kSurfaceBudget||stats.allocated_bytes>kSurfaceBudget-bytes) {
      error="Mutable Metal frame target budget exceeded";return false;
    }
    if(![renderer.Device() supportsTextureSampleCount:s->samples]) {
      error="Frame target sample count is unsupported on this Metal device";return false;
    }
    auto d=[MTLTextureDescriptor new];d.textureType=s->samples>1 ? MTLTextureType2DMultisample : Type(s->kind);
    d.pixelFormat=PlanAdapter::PixelFormat(s->format);d.width=s->width;d.height=s->height;
    d.mipmapLevelCount=s->levels;d.arrayLength=s->layers;d.sampleCount=s->samples;
    d.storageMode=MTLStorageModePrivate;d.hazardTrackingMode=MTLHazardTrackingModeTracked;
    d.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead|MTLTextureUsagePixelFormatView;
    auto texture=renderer.Texture(d,error);if(!texture)return false;
    surfaces.emplace(s->key,Entry{s,*s,texture,{}});stats.allocated_bytes+=bytes;++stats.surface_creates;
    return true;
  }
  id<MTLTexture> View(const render::SurfaceView& view,std::string& error) {
    auto it=surfaces.find(view.surface);
    if(it==surfaces.end()||it->second.owner.expired()) {
      error="Missing mutable Metal surface owner";return nil;
    }
    auto& entry=it->second;
    if(auto found=entry.views.find(view);found!=entry.views.end())return found->second;
    if(entry.descriptor.samples>1)return entry.texture;
    // Select the exact mip and layer for both attachment and sampled aliases.
    // The resulting 2D view exposes the correct dimensions to draw admission.
    auto texture=[entry.texture newTextureViewWithPixelFormat:entry.texture.pixelFormat
      textureType:MTLTextureType2D levels:NSMakeRange(view.level,1) slices:NSMakeRange(view.slice,1)];
    if(!texture){error="Metal rejected the ordered frame subresource view";return nil;}
    entry.views.emplace(view,texture);++stats.view_creates;return texture;
  }
  MTLRenderPassDescriptor* Pass(const render::Pass& source,std::string& error) {
    auto result=[MTLRenderPassDescriptor renderPassDescriptor];
    const auto assign=[&](MTLRenderPassAttachmentDescriptor* d,const render::Attachment& a) {
      d.texture=View(a.view,error);if(!d.texture)return false;
      d.loadAction=Load(a.load);d.storeAction=Store(a.store);
      if(a.resolve){d.resolveTexture=View(*a.resolve,error);if(!d.resolveTexture)return false;}
      return true;
    };
    for(size_t i=0;i<4;++i)if(source.colors[i]) {
      const auto& a=*source.colors[i];auto d=result.colorAttachments[i];
      if(!assign(d,a))return nil;
      d.clearColor=MTLClearColorMake(a.clear_color[0],a.clear_color[1],a.clear_color[2],a.clear_color[3]);
    }
    if(source.depth) {
      const auto& a=*source.depth;auto d=result.depthAttachment;if(!assign(d,a))return nil;d.clearDepth=a.clear_depth;
      if(a.resolve) {
        d.depthResolveFilter=a.filter==render::ResolveFilter::Min ? MTLMultisampleDepthResolveFilterMin :
          a.filter==render::ResolveFilter::Max ? MTLMultisampleDepthResolveFilterMax : MTLMultisampleDepthResolveFilterSample0;
      }
    }
    if(source.stencil) {
      auto d=result.stencilAttachment;if(!assign(d,*source.stencil))return nil;
      d.clearStencil=source.stencil->clear_stencil;
      if(source.stencil->resolve)d.stencilResolveFilter=MTLMultisampleStencilResolveFilterSample0;
    }
    return result;
  }
};
FrameAdapter::FrameAdapter(Renderer& renderer):impl_(std::make_unique<Impl>(renderer)){}
FrameAdapter::~FrameAdapter()=default;
bool FrameAdapter::Open(const std::string& libraries,std::string& error){return impl_->draws.Open(libraries,error);}
Receipt FrameAdapter::Submit(const std::shared_ptr<const render::FramePlan>& plan,std::string& error) {
  if(!plan){error="Missing ordered Metal frame plan";return {};}
  RetireResources();render::SurfaceContents final;
  if(!render::ValidateFrame(*plan,impl_->contents,final,error))return {};
  for(const auto& surface:plan->surfaces)if(!impl_->Ensure(surface,error))return {};
  struct ReadyPass { MTLRenderPassDescriptor* descriptor;std::vector<std::variant<Draw,Clear>> commands; };
  struct ReadyCopy {id<MTLTexture> source,destination;MTLOrigin src,dst;MTLSize size;};
  std::vector<std::variant<ReadyPass,ReadyCopy>> ready;ready.reserve(plan->commands.size());
  for(const auto& command:plan->commands) {
    if(const auto* copy=std::get_if<render::ImageCopy>(&command)) {
      auto source=impl_->View(copy->source,error),destination=impl_->View(copy->destination,error);
      if(!source||!destination)return {};
      ready.push_back(ReadyCopy{source,destination,
        MTLOriginMake(copy->source_origin[0],copy->source_origin[1],0),
        MTLOriginMake(copy->destination_origin[0],copy->destination_origin[1],0),
        MTLSizeMake(copy->extent[0],copy->extent[1],1)});continue;
    }
    const auto& pass=std::get<render::Pass>(command);
    ReadyPass prepared{impl_->Pass(pass,error),{}};if(!prepared.descriptor)return {};
    prepared.commands.reserve(pass.commands.size());
    for(const auto& command:pass.commands) {
      if(const auto* clear=std::get_if<render::RectClear>(&command)) {
        prepared.commands.push_back(Clear{clear->colors,clear->depth,clear->stencil,
          MTLScissorRect{clear->rectangle[0],clear->rectangle[1],clear->rectangle[2],clear->rectangle[3]},
          clear->color,clear->depth_value,clear->stencil_value});continue;
      }
      const auto& item=std::get<render::FrameDraw>(command);
      auto base=impl_->draws.Realize(item.capture,error);if(!base)return {};
      std::array<id<MTLTexture>,26> produced{};
      for(size_t slot=0;slot<produced.size();++slot)if(item.produced[slot]) {
        produced[slot]=impl_->View(*item.produced[slot],error);if(!produced[slot])return {};
      }
      Draw draw=*base;if(!impl_->draws.BindProduced(*item.capture,produced,draw,error))return {};
      prepared.commands.push_back(std::move(draw));
    }
    ready.push_back(std::move(prepared));
  }
  auto frame=impl_->renderer.BeginFrame(error);if(!frame)return {};
  for(const auto& command:ready) {
    if(const auto* copy=std::get_if<ReadyCopy>(&command)) {
      if(!frame.CopyTexture(copy->source,copy->destination,copy->src,copy->dst,copy->size,error))return {};
      continue;
    }
    const auto& pass=std::get<ReadyPass>(command);
    if(!frame.BeginPass(pass.descriptor,error))return {};
    for(const auto& command:pass.commands) {
      if(const auto* draw=std::get_if<Draw>(&command)){if(!frame.Encode(*draw,error))return {};}
      else if(!frame.ClearRectangle(std::get<Clear>(command),error))return {};
    }
    if(!frame.EndPass(error))return {};
  }
  auto receipt=frame.Submit(error);if(!receipt)return {};
  for(const auto& surface:plan->surfaces)impl_->Forget(surface->key);
  impl_->contents.insert(final.begin(),final.end());
  return receipt;
}
id<MTLTexture> FrameAdapter::Output(const render::FramePlan& plan,std::string& error) {
  if(!plan.output||!impl_->contents.contains(*plan.output)) {
    error="Ordered Metal frame has no stored output";return nil;
  }
  return impl_->View(*plan.output,error);
}
size_t FrameAdapter::RetireResources() {
  size_t retired=impl_->draws.RetireResources();
  for(auto it=impl_->surfaces.begin();it!=impl_->surfaces.end();) {
    if(it->second.owner.expired()) {
      impl_->Forget(it->first);impl_->stats.allocated_bytes-=SurfaceBytes(it->second.descriptor);
      it=impl_->surfaces.erase(it);++retired;++impl_->stats.retired;
    }else ++it;
  }
  return retired;
}
FrameResourceStats FrameAdapter::Stats() const{return impl_->stats;}
ResourceCacheStats FrameAdapter::ImmutableStats() const{return impl_->draws.ResourceStats();}
size_t FrameAdapter::PipelineCount() const{return impl_->draws.PipelineCount();}
}
