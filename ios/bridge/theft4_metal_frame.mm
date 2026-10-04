#include "theft4_metal_frame.h"
#include "theft4_metal_host_shaders.h"
#include <algorithm>
#include <chrono>
#include <bit>
#include <map>
#include <cstdio>

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
    bool external=false;
    std::map<render::SurfaceView,id<MTLTexture>> views;
    std::map<render::SurfaceView,id<MTLTexture>> sampled_views;
    std::map<render::SampledSurfaceView,id<MTLTexture>> sampled_ranges;
  };
  Renderer& renderer;
  PlanAdapter draws;
  HostShaderStore host;
  std::map<std::pair<render::HostProgram,render::Pipeline>,std::shared_ptr<const Pipeline>> host_pipelines;
  std::map<render::SurfaceKey,Entry> surfaces;
  render::SurfaceContents contents;
  FrameResourceStats stats;
  FrameTiming timing;
  std::set<std::string> logged_preparation_errors;
  explicit Impl(Renderer& r):renderer(r),draws(r),host(r){}
  void Forget(render::SurfaceKey key) {
    for(auto it=contents.begin();it!=contents.end();) {
      if(it->surface==key)it=contents.erase(it);else ++it;
    }
  }
  bool Ensure(const std::shared_ptr<const render::Surface>& s,id<MTLTexture> external,std::string& error) {
    if(external && (external.device!=renderer.Device()||external.textureType!=MTLTextureType2D||external.sampleCount!=1||
       external.width!=s->width||external.height!=s->height||external.pixelFormat!=PlanAdapter::PixelFormat(s->format)||
       s->kind!=render::ImageKind::Texture2D||s->levels!=1||s->layers!=1||s->samples!=1)) {
      error="Drawable allocation differs from the declared Metal frame output";return false;
    }
    if(auto it=surfaces.find(s->key);it!=surfaces.end()) {
      const auto owner=it->second.owner.lock();
      if(owner) {
        if(owner.owner_before(s)||s.owner_before(owner)||it->second.descriptor!=*s||
           it->second.external!=bool(external)) {
          error="Mutable Metal surface generation changed identity for allocation "+std::to_string(s->key.id)+" generation "+std::to_string(s->key.generation)+" (owner="+std::to_string(owner.owner_before(s)||s.owner_before(owner))+", descriptor="+std::to_string(it->second.descriptor!=*s)+", external="+std::to_string(it->second.external!=bool(external))+")";return false;
        }
        // CAMetalLayer rotates physical drawables for the same logical output.
        // Each encoded command buffer retains its own texture, so rebinding the
        // next submission cannot replace an earlier in-flight attachment.
        if(external)it->second.texture=external;
        return true;
      }
      if(!it->second.external)stats.allocated_bytes-=SurfaceBytes(it->second.descriptor);
      surfaces.erase(it);Forget(s->key);
    }
    if(external) {
      surfaces.emplace(s->key,Entry{s,*s,external,true,{},{}});++stats.surface_creates;return true;
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
    surfaces.emplace(s->key,Entry{s,*s,texture,false,{},{}});stats.allocated_bytes+=bytes;++stats.surface_creates;
    return true;
  }
  id<MTLTexture> View(const render::SurfaceView& view,std::string& error) {
    auto it=surfaces.find(view.surface);
    if(it==surfaces.end()||it->second.owner.expired()) {
      error="Missing mutable Metal surface owner";return nil;
    }
    auto& entry=it->second;
    if(entry.external)return entry.texture;
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
    if(source.attachmentless_extent[0]) {
      result.renderTargetWidth=source.attachmentless_extent[0];
      result.renderTargetHeight=source.attachmentless_extent[1];result.defaultRasterSampleCount=1;
    }
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
  id<MTLTexture> SampledView(const render::SurfaceView& view,std::string& error) {
    auto texture=View(view,error);if(!texture)return nil;
    if(view.aspect!=render::Aspect::Stencil||texture.pixelFormat!=MTLPixelFormatDepth32Float_Stencil8)return texture;
    auto& entry=surfaces.at(view.surface);
    if(auto it=entry.sampled_views.find(view);it!=entry.sampled_views.end())return it->second;
    auto stencil=[texture newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8];
    if(!stencil){error="Metal rejected the sampled stencil alias";return nil;}
    entry.sampled_views.emplace(view,stencil);++stats.view_creates;return stencil;
  }
  id<MTLTexture> SampledView(const render::SampledSurfaceView& view,std::string& error) {
    auto it=surfaces.find(view.surface);
    if(it==surfaces.end()||it->second.owner.expired()||it->second.external) {
      error="Missing retained sampleable Metal allocation";return nil;
    }
    auto& entry=it->second;
    if(auto found=entry.sampled_ranges.find(view);found!=entry.sampled_ranges.end())return found->second;
    auto format=PlanAdapter::PixelFormat(view.format==render::Format::Invalid ? entry.descriptor.format : view.format);
    if(view.aspect==render::Aspect::Stencil&&format==MTLPixelFormatDepth32Float_Stencil8)format=MTLPixelFormatX32_Stencil8;
    const std::array identity{render::Swizzle::Red,render::Swizzle::Green,render::Swizzle::Blue,render::Swizzle::Alpha};
    // Host resolve shaders sample a multisample allocation directly. Preserve
    // its texture type when constructing a ranged or swizzled sampling view.
    const auto texture_type=entry.descriptor.samples>1 ? MTLTextureType2DMultisample : Type(view.kind);
    id<MTLTexture> texture=nil;
    if(view.swizzle==identity) {
      texture=[entry.texture newTextureViewWithPixelFormat:format textureType:texture_type
        levels:NSMakeRange(view.level,view.levels) slices:NSMakeRange(view.slice,view.slices)];
    }else {
      auto channels=MTLTextureSwizzleChannelsMake(MTLTextureSwizzle(view.swizzle[0]),MTLTextureSwizzle(view.swizzle[1]),
        MTLTextureSwizzle(view.swizzle[2]),MTLTextureSwizzle(view.swizzle[3]));
      texture=[entry.texture newTextureViewWithPixelFormat:format textureType:texture_type
        levels:NSMakeRange(view.level,view.levels) slices:NSMakeRange(view.slice,view.slices) swizzle:channels];
    }
    if(!texture){error="Metal rejected the sampled mip/layer/format alias";return nil;}
    entry.sampled_ranges.emplace(view,texture);++stats.view_creates;return texture;
  }
  bool PrepareHost(const render::HostDraw& source,MTLRenderPassDescriptor* pass,Draw& draw,std::string& error) {
    const auto key=std::pair{source.program,source.pipeline};
    const auto& info=render::kHostPrograms[size_t(source.program)];
    auto it=host_pipelines.find(key);
    if(it==host_pipelines.end()) {
      auto vs=host.Resolve("fullscreen_cw_vs",error),ps=host.Resolve(info.name,error);
      if(!vs.function||!ps.function)return false;
      auto pipeline=BuildFixedPipeline(renderer,source.pipeline,render::Primitive::Triangle,vs,&ps,error);
      if(!pipeline)return false;it=host_pipelines.emplace(key,pipeline).first;
    }
    Draw result;result.pipeline=it->second;result.vertex_count=3;
    id<MTLTexture> target=pass.depthAttachment.texture;
    if(!target)target=pass.stencilAttachment.texture;
    for(size_t i=0;i<4&&!target;++i)target=pass.colorAttachments[i].texture;
    if(!target){error="Host utility has no ordered render target";return false;}
    result.viewport={0,0,double(target.width),double(target.height),0,1};
    result.scissor={source.scissor[0],source.scissor[1],source.scissor[2],source.scissor[3]};
    result.stencil_reference=source.stencil_front_reference;result.stencil_back_reference=source.stencil_back_reference;
    result.blend_color=source.blend_color;
    std::vector<HostInput> inputs;
    for(size_t slot=0;slot<source.fetches.size();++slot)if(info.textures&(1u<<slot)) {
      const auto& input=source.fetches[slot];
      auto texture=input.produced ? SampledView(*input.produced,error) : draws.ImageFor(input.image,error);
      if(!texture)return false;
      auto sampler=draws.SamplerFor(*input.sampler,error);if(!sampler)return false;
      inputs.push_back({uint32_t(slot),texture,sampler});
    }
    auto constants=info.constants ? draws.ConstantFor(source.constants,error) : BufferView{};
    if(info.constants&&!constants.buffer)return false;
    if(!host.Bind(info.name,inputs,constants,result,error))return false;
    draw=std::move(result);return true;
  }
};
FrameAdapter::FrameAdapter(Renderer& renderer):impl_(std::make_unique<Impl>(renderer)){}
FrameAdapter::~FrameAdapter()=default;
bool FrameAdapter::Open(const std::string& libraries,std::string& error){
  if(!impl_->draws.Open(libraries,error)||!impl_->host.Open(libraries+"/Host",error))return false;
  impl_->host_pipelines.clear();return true;
}
Receipt FrameAdapter::Submit(const std::shared_ptr<const render::FramePlan>& plan,std::string& error,render::SurfaceContents* published,bool profile_gpu) {
  return SubmitFrame(plan,{},nil,error,published,profile_gpu);
}
Receipt FrameAdapter::SubmitAndPresent(const std::shared_ptr<const render::FramePlan>& plan,
                                      render::SurfaceKey target,id<CAMetalDrawable> drawable,std::string& error,render::SurfaceContents* published,bool profile_gpu) {
  if(!drawable||!target.id||!target.generation){error="Missing ordered Metal presentation target";return {};}
  return SubmitFrame(plan,target,drawable,error,published,profile_gpu);
}
Receipt FrameAdapter::SubmitFrame(const std::shared_ptr<const render::FramePlan>& plan,
                                 render::SurfaceKey target,id<CAMetalDrawable> drawable,std::string& error,render::SurfaceContents* published,bool profile_gpu) {
  using Clock=std::chrono::steady_clock;
  const auto begin=Clock::now();
  const auto before_resources=ImmutableStats();const auto before_pipelines=PipelineCount();
  if(!plan){error="Missing ordered Metal frame plan";return {};}
  if(drawable) {
    if(!plan->output||*plan->output!=render::SurfaceView{target,0,0,render::Aspect::Color}) {
      error="Drawable must be the declared ordered frame output";return {};
    }
    const auto* final=plan->commands.empty() ? nullptr : std::get_if<render::Pass>(&plan->commands.back());
    if(!final||!final->colors[0]||
       (final->colors[0]->view.surface!=target&&
        (!final->colors[0]->resolve||final->colors[0]->resolve->surface!=target))) {
      error="Drawable must be stored by the final color-zero pass";return {};
    }
    for(const auto& command:plan->commands) {
      if(const auto* copy=std::get_if<render::ImageCopy>(&command)) {
        if(copy->source.surface==target||copy->destination.surface==target) {
          error="Framebuffer-only drawable cannot participate in image copies";return {};
        }
        continue;
      }
      const auto& pass=std::get<render::Pass>(command);
      for(const auto& a:pass.colors)if(a&&a->view.surface==target&&a->load==render::Load::Load) {
        error="Drawable contents must be defined in their presentation pass";return {};
      }
      for(const auto& command:pass.commands) {
        if(const auto* draw=std::get_if<render::FrameDraw>(&command))
          for(const auto& v:draw->produced)if(v&&v->surface==target) {
            error="Framebuffer-only drawable cannot be sampled";return {};
          }
        if(const auto* host=std::get_if<render::HostDraw>(&command))
          for(const auto& input:host->fetches)if(input.produced&&input.produced->surface==target) {
            error="Framebuffer-only drawable cannot be sampled by a host utility";return {};
          }
      }
    }
  }
  RetireResources();impl_->draws.BeginUploadBatch();render::SurfaceContents final;render::DrawVertexRanges draw_ranges;
  if(!render::ValidateFrame(*plan,impl_->contents,final,error,&impl_->draws.IndexRanges(),&draw_ranges))return {};
  size_t draw_range_index=0;
  const auto validated=Clock::now();
  for(const auto& surface:plan->surfaces)
    if(!impl_->Ensure(surface,drawable&&surface->key==target ? drawable.texture : nil,error))return {};
  struct ReadyPass { MTLRenderPassDescriptor* descriptor;std::vector<std::variant<Draw,Clear>> commands; };
  struct ReadyCopy {id<MTLTexture> source,destination;MTLOrigin src,dst;MTLSize size;bool combined_depth_stencil;};
  std::vector<std::variant<ReadyPass,ReadyCopy>> ready;ready.reserve(plan->commands.size());
  size_t preparation_errors=0;std::string first_preparation_error;
  const auto failed_preparation=[&](uint64_t vertex,uint64_t fragment) {
    ++preparation_errors;if(first_preparation_error.empty())first_preparation_error=error;
    const auto key=std::to_string(vertex)+":"+std::to_string(fragment)+":"+error;
    if(impl_->logged_preparation_errors.size()<32&&impl_->logged_preparation_errors.insert(key).second)
      std::fprintf(stderr,"gta4-metal-preflight: vs=%016llx ps=%016llx reason=%s submitted=false\n",
          (unsigned long long)vertex,(unsigned long long)fragment,error.c_str());
    error.clear();
  };
  for(const auto& command:plan->commands) {
    if(const auto* copy=std::get_if<render::ImageCopy>(&command)) {
      auto source=impl_->View(copy->source,error),destination=impl_->View(copy->destination,error);
      if(!source||!destination)return {};
      ready.push_back(ReadyCopy{source,destination,
        MTLOriginMake(copy->source_origin[0],copy->source_origin[1],0),
        MTLOriginMake(copy->destination_origin[0],copy->destination_origin[1],0),
        MTLSizeMake(copy->extent[0],copy->extent[1],1),copy->combined_depth_stencil});continue;
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
      if(const auto* host=std::get_if<render::HostDraw>(&command)) {
        Draw draw;if(!impl_->PrepareHost(*host,prepared.descriptor,draw,error)) {failed_preparation(0,uint64_t(host->program));continue;}
        prepared.commands.push_back(std::move(draw));continue;
      }
      const auto& item=std::get<render::FrameDraw>(command);
      if(draw_range_index>=draw_ranges.size()||draw_ranges[draw_range_index].capture!=item.capture.get()) {
        error="Immutable draw order changed after frame admission";return {};
      }
      Draw draw;const bool base=impl_->draws.PrepareValidated(*item.capture,draw_ranges[draw_range_index++].maximum_vertex,draw,error);
      const auto fail_draw=[&] {failed_preparation(item.capture->draw.pipeline.vertex.hash,item.capture->draw.pipeline.fragment.hash);};
      if(!base){fail_draw();continue;}
      if(std::none_of(item.produced.begin(),item.produced.end(),[](const auto& v){return bool(v);})) {
        prepared.commands.push_back(std::move(draw));continue;
      }
      std::array<id<MTLTexture>,26> produced{};
      bool inputs_valid=true;
      for(size_t slot=0;slot<produced.size();++slot)if(item.produced[slot]) {
        produced[slot]=impl_->SampledView(*item.produced[slot],error);if(!produced[slot]){inputs_valid=false;break;}
      }
      if(!inputs_valid){fail_draw();continue;}
      if(!impl_->draws.BindProduced(*item.capture,produced,draw,error)){fail_draw();continue;}
      prepared.commands.push_back(std::move(draw));
    }
    ready.push_back(std::move(prepared));
  }
  if(preparation_errors) {
    error=first_preparation_error+" ("+std::to_string(preparation_errors)+" draw preparations failed; frame not submitted)";
    return {};
  }
  const auto prepared_at=Clock::now();
  auto frame=impl_->renderer.BeginFrame(error);if(!frame)return {};
  if(profile_gpu)frame.ProfilePasses(std::count_if(ready.begin(),ready.end(),[](const auto& c){return std::holds_alternative<ReadyPass>(c);}));
  for(const auto& command:ready) {
    if(const auto* copy=std::get_if<ReadyCopy>(&command)) {
      if(!frame.CopyTexture(copy->source,copy->destination,copy->src,copy->dst,copy->size,error,copy->combined_depth_stencil))return {};
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
  if(drawable&&!frame.Present(drawable,error))return {};
  auto receipt=frame.Submit(error);if(!receipt)return {};
  for(const auto& surface:plan->surfaces)impl_->Forget(surface->key);
  impl_->contents.insert(final.begin(),final.end());
  const auto ended=Clock::now();const auto after_resources=ImmutableStats();
  const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
  FrameTiming timing;timing.validation_ms=ms(begin,validated);timing.preparation_ms=ms(validated,prepared_at);
  timing.encoding_ms=ms(prepared_at,ended);timing.commands=plan->commands.size();timing.encoder=frame.Stats();timing.draws=timing.encoder.draws;
  timing.pipelines_created=PipelineCount()-before_pipelines;
  timing.buffers_created=after_resources.buffer_creates-before_resources.buffer_creates;
  timing.textures_created=after_resources.texture_creates-before_resources.texture_creates;
  timing.uploaded_bytes=after_resources.uploaded_bytes-before_resources.uploaded_bytes;impl_->timing=timing;
  if(published)*published=std::move(final);
  return receipt;
}
id<MTLTexture> FrameAdapter::Output(const render::FramePlan& plan,std::string& error) {
  if(!plan.output||!impl_->contents.contains(*plan.output)) {
    error="Ordered Metal frame has no stored output";return nil;
  }
  return impl_->View(*plan.output,error);
}
id<MTLTexture> FrameAdapter::SampledTexture(const render::FramePlan& plan,
                                          const render::SampledSurfaceView& view,std::string& error) {
  if(!render::ValidateSampledView(plan,view,error))return nil;
  if(!render::SampledViewDefined(view,impl_->contents)) {
    error="Sampled Metal range contains undefined or discarded content";return nil;
  }
  const auto it=impl_->surfaces.find(view.surface);
  const auto declaration=std::find_if(plan.surfaces.begin(),plan.surfaces.end(),
    [&](const auto& s){return s&&s->key==view.surface;});
  if(it==impl_->surfaces.end()||declaration==plan.surfaces.end()) {
    error="Sampled Metal range has no realized allocation";return nil;
  }
  const auto owner=it->second.owner.lock();
  if(!owner||owner.owner_before(*declaration)||declaration->owner_before(owner)||
     it->second.descriptor!=**declaration) {
    error="Sampled Metal allocation declaration changed identity";return nil;
  }
  auto texture=impl_->SampledView(view,error);if(texture)error.clear();return texture;
}
size_t FrameAdapter::RetireResources() {
  size_t retired=impl_->draws.RetireResources();
  for(auto it=impl_->surfaces.begin();it!=impl_->surfaces.end();) {
    if(it->second.owner.expired()) {
      impl_->Forget(it->first);if(!it->second.external)impl_->stats.allocated_bytes-=SurfaceBytes(it->second.descriptor);
      it=impl_->surfaces.erase(it);++retired;++impl_->stats.retired;
    }else ++it;
  }
  return retired;
}
FrameTiming FrameAdapter::LastTiming() const{return impl_->timing;}
FrameResourceStats FrameAdapter::Stats() const{return impl_->stats;}
ResourceCacheStats FrameAdapter::ImmutableStats() const{return impl_->draws.ResourceStats();}
size_t FrameAdapter::PipelineCount() const{return impl_->draws.PipelineCount()+impl_->host_pipelines.size();}
}
