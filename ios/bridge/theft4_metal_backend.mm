#include "theft4_retail_mode.h"
#include "theft4_metal_backend.h"
#include "theft4_metal_frame.h"
#include "theft4_upload_budget.h"
#include <algorithm>
#include <deque>
#include <thread>
#include <chrono>
#include <cstdio>
#include <map>
#include <TargetConditionals.h>
namespace theft4::metal {
namespace {
class Backend final:public render::FrameBackend {
 public:
  Backend(void* layer,std::string libraries,uint32_t maximum,void (*diagnostic)(const char*))
      :layer_((__bridge CAMetalLayer*)layer),libraries_(std::move(libraries)),
       maximum_(std::clamp(maximum,1u,3u)),diagnostic_(diagnostic) {
    device_=MTLCreateSystemDefaultDevice();
    if(device_) {
      // All supported iOS deployment devices use Apple family 3 or newer.
      // Reject an unknown GPU rather than overstate its texture limits.
      caps_.max_image_dimension_2d=[device_ supportsFamily:MTLGPUFamilyApple3] ||
          [device_ supportsFamily:MTLGPUFamilyMac2] ? 16384 : 0;
      caps_.bc_textures=device_.supportsBCTextureCompression;
      caps_.astc_textures=[device_ supportsFamily:MTLGPUFamilyApple3];
      caps_.mirror_clamp_to_edge=[device_ supportsFamily:MTLGPUFamilyApple7] || [device_ supportsFamily:MTLGPUFamilyMac2];
      caps_.float32_filtering=device_.supports32BitFloatFiltering;
      for(uint32_t samples:{1u,2u,4u,8u})
        if([device_ supportsTextureSampleCount:samples])caps_.sample_counts|=1u<<samples;
    }
  }
  ~Backend() override { Close(); }
  render::BackendCapabilities Capabilities() const override{return caps_;}
  bool HasPresentation() const override{return layer_!=nil;}
  bool Open(std::string& error) override {
    if(renderer_)return Worker(error);
    if(open_attempted_){error="Metal backend cannot reopen after failure or close";return false;}
    open_attempted_=true;worker_=std::this_thread::get_id();
    if(!device_ || !caps_.max_image_dimension_2d){error="Unsupported Metal device";return false;}
    renderer_=std::make_unique<Renderer>(maximum_);
    if(!renderer_->Ready() || renderer_->Device()!=device_) {
      error="Metal worker device initialization failed";renderer_.reset();return false;
    }
    const auto upload_budget=ImmutableUploadBudget(NSProcessInfo.processInfo.physicalMemory,caps_.bc_textures);
    adapter_=std::make_unique<FrameAdapter>(*renderer_,128,upload_budget);
    if(!theft4_retail_mode()) {
      char message[256];std::snprintf(message,sizeof(message),"gta4-metal-upload-budget: buffer-limit-bytes=%zu physical-memory-bytes=%llu bc-textures=%d",
          upload_budget,(unsigned long long)NSProcessInfo.processInfo.physicalMemory,int(caps_.bc_textures));
      if(diagnostic_)diagnostic_(message);else std::fprintf(stderr,"%s\n",message);
    }
    #if TARGET_OS_IOS
    if(layer_) {
      auto root=[NSFileManager.defaultManager URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject;
      if(root)adapter_->ConfigurePipelineCache(std::string(root.path.UTF8String)+"/Theft4MetalPipelines-v1");
    }
    #endif
    if(!adapter_->Open(libraries_,error)){adapter_.reset();renderer_.reset();return false;}
    // The UI creates the layer before constructing the backend. Its device
    // must be the same device used to allocate the frame's retained resources.
    if(layer_ && layer_.device && layer_.device!=device_) {
      error="Metal layer and render worker devices differ";adapter_.reset();renderer_.reset();return false;
    }
    if(layer_)layer_.device=device_;
    error.clear();return true;
  }
  bool Target(render::PresentationTarget& target,std::string& error) override {
    if(!Worker(error))return false;
    if(!layer_){error="Metal backend has no presentation layer";return false;}
    const CGSize size=layer_.drawableSize;
    render::PresentationTarget result;
    if(size.width<1 || size.height<1 || size.width>caps_.max_image_dimension_2d ||
       size.height>caps_.max_image_dimension_2d){error="Metal drawable extent unavailable";return false;}
    result.width=uint32_t(size.width);result.height=uint32_t(size.height);
    switch(layer_.pixelFormat) {
      case MTLPixelFormatBGRA8Unorm:result.format=render::Format::BGRA8Unorm;break;
      case MTLPixelFormatBGRA8Unorm_sRGB:result.format=render::Format::BGRA8Srgb;break;
      case MTLPixelFormatRGBA16Float:result.format=render::Format::RGBA16Float;break;
      default:error="Metal drawable format needs explicit output lowering";return false;
    }
    target=result;error.clear();return true;
  }
  bool Submit(std::shared_ptr<const render::FramePlan> plan,bool present,
              std::string& error,render::SurfaceContents* published=nullptr) override {
    if(!Worker(error) || !Healthy(error))return false;
    if(!plan || plan->commands.empty()){error="Empty Metal frame submission rejected";return false;}
    if(present && (!layer_ || !plan->output)) {
      error="Metal publication requires a layer and final output";return false;
    }
    using Clock=std::chrono::steady_clock;
    const bool diagnostics=!theft4_retail_mode();
    const auto now=[&]{return diagnostics?Clock::now():Clock::time_point{};};
    const auto began=now();
    // Reap completed work and bound admission before acquiring a scarce drawable.
    while(!pending_.empty() && (pending_.front().receipt.Completed() || pending_.size()>=maximum_)) {
      if(!FinishOldest(error))return false;
    }
    const auto admitted=now();
    const bool profile_gpu=!theft4_retail_mode()&&present&&plan->commands.size()>=40&&
        (!profile_attempted_||presentations_-last_profile_presentation_>=180);
    if(profile_gpu){profile_attempted_=true;last_profile_presentation_=presentations_;}
    double drawable_ms=0;Receipt receipt;
    @autoreleasepool {
      if(present) {
        render::PresentationTarget target;
        if(!Target(target,error))return false;
        const auto* output=render::FindSurface(*plan,plan->output->surface);
        if(!output || output->width!=target.width || output->height!=target.height ||
           output->format!=target.format || output->samples!=1 ||
           output->kind!=render::ImageKind::Texture2D || output->levels!=1 || output->layers!=1 ||
           plan->output->level || plan->output->slice || plan->output->aspect!=render::Aspect::Color) {
          error="Frame output does not match the current Metal drawable";return false;
        }
        const auto drawable_begin=now();
        id<CAMetalDrawable> drawable=[layer_ nextDrawable];
        drawable_ms=std::chrono::duration<double,std::milli>(now()-drawable_begin).count();
        if(!drawable){error="Metal drawable temporarily unavailable";return false;}
        receipt=adapter_->SubmitAndPresent(plan,output->key,drawable,error,published,profile_gpu);
      } else receipt=adapter_->Submit(plan,error,published);
    }
    if(!receipt)return false;
    const auto submitted_at=now();
    const double submit_ms=std::chrono::duration<double,std::milli>(submitted_at-began).count();
    // The periodic summary can miss the exact spike. Keep a bounded slow-only
    // record of admission, driver preparation and encoding for attribution.
    if(diagnostics&&submit_ms>40&&slow_submissions_<256&&
       (submit_ms>=80||submitted_at-last_slow_submission_>=std::chrono::seconds(1))) {
      last_slow_submission_=submitted_at;++slow_submissions_;
      const auto t=adapter_->LastTiming();const auto resources=adapter_->ImmutableStats();
      char message[1024];std::snprintf(message,sizeof(message),
        "gta4-metal-slow-submit: sequence=%llu present=%d total-ms=%.3f admission-ms=%.3f drawable-ms=%.3f validate-ms=%.3f prepare-ms=%.3f encode-ms=%.3f draws=%llu new-pipelines=%llu new-buffers=%llu new-textures=%llu upload-bytes=%llu buffer-cache-bytes=%llu buffer-evictions=%llu constant-cache-bytes=%llu constant-evictions=%llu",
        (unsigned long long)plan->sequence,int(present),submit_ms,
        std::chrono::duration<double,std::milli>(admitted-began).count(),drawable_ms,
        t.validation_ms,t.preparation_ms,t.encoding_ms,(unsigned long long)t.draws,
        (unsigned long long)t.pipelines_created,(unsigned long long)t.buffers_created,
        (unsigned long long)t.textures_created,(unsigned long long)t.uploaded_bytes,
        (unsigned long long)resources.resident_buffer_bytes,(unsigned long long)resources.buffer_evictions,
        (unsigned long long)resources.resident_constant_bytes,(unsigned long long)resources.constant_evictions);
      if(diagnostic_)diagnostic_(message);else std::fprintf(stderr,"%s\n",message);
    }
    if(!theft4_retail_mode()&&present&&++presentations_%60==1) {
      const auto& t=adapter_->LastTiming();const auto& e=t.encoder;
      const auto resources=adapter_->ImmutableStats();
      std::fprintf(stderr,"gta4-metal-upload-pages: present=%llu vm-allocations=%llu vm-reuses=%llu free-page-bytes=%llu buffer-extent-queries=%llu texture-shape-queries=%llu\n",
        (unsigned long long)presentations_,(unsigned long long)resources.page_memory_allocations,
        (unsigned long long)resources.page_memory_reuses,(unsigned long long)resources.free_page_bytes,
        (unsigned long long)e.buffer_extent_queries,(unsigned long long)e.texture_shape_queries);
      std::fprintf(stderr,"gta4-metal-attachment-performance: present=%llu avoided-stores=%llu avoided-loads=%llu native-identity-copies=%llu binding-storage-reuses=%llu binding-storage-fresh=%llu host-specializations=%zu host-specialization-fallbacks=%zu\n",
        (unsigned long long)presentations_,(unsigned long long)t.avoided_attachment_stores,
        (unsigned long long)t.avoided_attachment_loads,(unsigned long long)t.native_identity_copies,
        (unsigned long long)t.binding_storage_reuses,(unsigned long long)t.binding_storage_fresh,
        adapter_->Stats().host_specializations,adapter_->Stats().host_specialization_fallbacks);
      char message[1400];std::snprintf(message,sizeof(message),"gta4-metal-performance: present=%llu wait-ms=%.3f drawable-ms=%.3f validate-ms=%.3f prepare-ms=%.3f encode-ms=%.3f last-gpu-ms=%.3f commands=%llu draws=%llu new-pipelines=%llu new-buffers=%llu new-textures=%llu upload-bytes=%llu binding-calls=%llu avoided-calls=%llu resident-bytes=%zu buffer-cache-bytes=%llu buffer-cache-peak=%llu buffer-evictions=%llu gpu-allocated-bytes=%llu buffer-offset-calls=%llu constant-cache-bytes=%llu constant-evictions=%llu",
        (unsigned long long)presentations_,std::chrono::duration<double,std::milli>(admitted-began).count(),drawable_ms,
        t.validation_ms,t.preparation_ms,t.encoding_ms,last_gpu_ms_,(unsigned long long)t.commands,(unsigned long long)t.draws,
        (unsigned long long)t.pipelines_created,(unsigned long long)t.buffers_created,(unsigned long long)t.textures_created,
        (unsigned long long)t.uploaded_bytes,(unsigned long long)(e.state_calls+e.buffer_calls+e.texture_calls+e.sampler_calls),
        (unsigned long long)e.avoided_calls,adapter_->Stats().allocated_bytes,
        (unsigned long long)resources.resident_buffer_bytes,(unsigned long long)resources.peak_buffer_bytes,
        (unsigned long long)resources.buffer_evictions,(unsigned long long)device_.currentAllocatedSize,
        (unsigned long long)e.buffer_offset_calls,(unsigned long long)resources.resident_constant_bytes,
        (unsigned long long)resources.constant_evictions);
      if(diagnostic_)diagnostic_(message);else std::fprintf(stderr,"%s\n",message);
    }
    const auto timing=adapter_->LastTiming();
    pending_.push_back({std::move(receipt),std::move(plan),present,timing.render_passes,timing.image_copies});
    error.clear();return true;
  }
  bool Drain(std::string& error) override {
    if(!Worker(error))return false;
    // A failed buffer still completes its lifetime. Wait every accepted buffer
    // before releasing owners, even when the first completion reports failure.
    while(!pending_.empty()) {
      std::string completion_error;
      FinishOldest(completion_error);
    }
    if(adapter_)adapter_->FlushPipelineCache();
    return Healthy(error);
  }
  void Close() override {
    if(!renderer_)return;
    // Normal SDK teardown closes on the worker. Destructor fallback also waits
    // every accepted buffer after the worker has joined; it never encodes work.
    for(auto& submitted:pending_){std::string error;submitted.receipt.Wait(error);}
    pending_.clear();if(adapter_)adapter_->FlushPipelineCache();adapter_.reset();renderer_.reset();
  }
  bool ReadRGBA8(const render::FramePlan& plan,render::SurfaceView view,
                std::vector<uint8_t>& bytes,std::string& error) override {
    if(!Drain(error))return false;
    const auto* surface=render::FindSurface(plan,view.surface);
    if(!surface || view.aspect!=render::Aspect::Color || surface->samples!=1 ||
       (surface->format!=render::Format::RGBA8Unorm && surface->format!=render::Format::BGRA8Unorm)) {
      error="Metal readback requires an explicit single-sample RGBA8 color view";return false;
    }
    auto texture=adapter_->SampledTexture(plan,view,error);if(!texture)return false;
    auto result=renderer_->ReadRGBA8(texture,error);
    if(result.empty())return false;
    bytes=std::move(result);error.clear();return true;
  }
  bool ReadColor(const render::FramePlan& plan,render::SurfaceView view,
                 render::ColorReadback& output,std::string& error) override {
    if(!Drain(error))return false;
    const auto* surface=render::FindSurface(plan,view.surface);
    if(!surface||view.aspect!=render::Aspect::Color||surface->samples!=1||
       !render::SupportsAspect(surface->format,view.aspect)) {
      error="Metal native color readback requires a defined single-sample color allocation";return false;
    }
    auto texture=adapter_->SampledTexture(plan,view,error);if(!texture)return false;
    auto bytes=renderer_->ReadColorBytes(texture,error);if(bytes.empty())return false;
    render::ColorReadback result;result.format=surface->format;
    result.width=uint32_t(texture.width);result.height=uint32_t(texture.height);
    result.row_bytes=bytes.size()/result.height;result.bytes=std::move(bytes);
    output=std::move(result);error.clear();return true;
  }
 private:
  struct Submitted{Receipt receipt;std::shared_ptr<const render::FramePlan> owner;bool present=false;uint64_t render_passes=0,image_copies=0;};
  bool Worker(std::string& error) const {
    if(!renderer_ || !adapter_){error="Metal worker is not open";return false;}
    if(worker_!=std::this_thread::get_id()){error="Metal encoding belongs to the render worker";return false;}
    return true;
  }
  bool Healthy(std::string& error) const {
    if(!failure_.empty()){error=failure_;return false;}error.clear();return true;
  }
  bool FinishOldest(std::string& error) {
    auto submitted=std::move(pending_.front());pending_.pop_front();
    const bool okay=submitted.receipt.Wait(error);
    if(okay&&!theft4_retail_mode()) {
      if(submitted.present&&gpu_sample_count_%60==0) {
        char context[256];std::snprintf(context,sizeof(context),"gta4-metal-device-context: thermal-state=%ld low-power=%d",
          (long)NSProcessInfo.processInfo.thermalState,int(NSProcessInfo.processInfo.lowPowerModeEnabled));
        if(diagnostic_)diagnostic_(context);else std::fprintf(stderr,"%s\n",context);
      }
      last_gpu_ms_=submitted.receipt.GpuMilliseconds();
      frame_gpu_work_ms_+=last_gpu_ms_;
      if(submitted.present){gpu_samples_[gpu_sample_count_++%gpu_samples_.size()]=frame_gpu_work_ms_;frame_gpu_work_ms_=0;}
      if(submitted.present&&gpu_sample_count_%gpu_samples_.size()==0) {
        auto sorted=gpu_samples_;std::sort(sorted.begin(),sorted.end());
        char message[512];std::snprintf(message,sizeof(message),
          "gta4-metal-gpu-work-budget: title-frames=60 ceiling-ms=30 p50-ms=%.3f p95-ms=%.3f p99-ms=%.3f max-ms=%.3f over-budget=%zu",
          sorted[29],sorted[56],sorted[59],sorted[59],size_t(std::count_if(sorted.begin(),sorted.end(),[](double ms){return ms>30;})));
        if(diagnostic_)diagnostic_(message);else std::fprintf(stderr,"%s\n",message);
      }
      auto timings=submitted.receipt.GpuPassTimings();
      if(!timings.empty()) {
        std::vector<const render::Pass*> passes;
        std::vector<size_t> pass_commands;
        for(size_t i=0;i<submitted.owner->commands.size();++i)
          if(const auto* pass=std::get_if<render::Pass>(&submitted.owner->commands[i])){passes.push_back(pass);pass_commands.push_back(i);}
        const auto dead_stores=render::DeadAttachmentStores(*submitted.owner);
        const auto redundant_loads=render::RedundantAttachmentLoads(*submitted.owner);
        char summary[512];std::snprintf(summary,sizeof(summary),
          "gta4-metal-gpu-profile: sequence=%llu command-buffer-ms=%.3f pass-samples=%zu passes=%llu copies=%llu original-passes=%zu detail-limit=12 stage-times-overlap=true color-pixel-format-view=false",
          (unsigned long long)submitted.owner->sequence,last_gpu_ms_,timings.size(),
          (unsigned long long)submitted.render_passes,(unsigned long long)submitted.image_copies,passes.size());
        if(diagnostic_)diagnostic_(summary);else std::fprintf(stderr,"%s\n",summary);
        std::sort(timings.begin(),timings.end(),[](const auto& a,const auto& b){return std::max(a.vertex_ms,a.fragment_ms)>std::max(b.vertex_ms,b.fragment_ms);});
        for(size_t rank=0;rank<std::min(size_t(12),timings.size());++rank) {
          const auto& t=timings[rank];if(t.pass>=passes.size())continue;const auto& pass=*passes[t.pass];
          uint64_t vertex=0,pixel=0,vertices=0,indices=0;uint32_t host=0;size_t draws=0,host_draws=0,clears=0,dominant_count=0;
          std::map<std::pair<uint64_t,uint64_t>,size_t> shaders;
          for(const auto& c:pass.commands) {
            if(const auto* d=std::get_if<render::FrameDraw>(&c)) {
              ++draws;const auto& draw=d->capture->draw;vertices+=uint64_t(draw.vertex_count)*draw.instances;
              indices+=uint64_t(draw.index_count)*draw.instances;
              const auto key=std::pair{draw.pipeline.vertex.hash,draw.pipeline.fragment.hash};
              const auto count=++shaders[key];if(count>dominant_count){dominant_count=count;vertex=key.first;pixel=key.second;}
            }
            if(const auto* h=render::GetHostDraw(c)){host|=1u<<uint32_t(h->program);++host_draws;}
            if(std::holds_alternative<render::RectClear>(c))++clears;
          }
          const render::Attachment* attachment=pass.depth?&*pass.depth:nullptr;
          for(const auto& color:pass.colors)if(color){attachment=&*color;break;}
          const auto* surface=attachment?render::FindSurface(*submitted.owner,attachment->view.surface):nullptr;
          std::array<uint32_t,6> formats{};uint32_t loads=0,stores=0,resolves=0;
          const auto describe=[&](const std::optional<render::Attachment>& a,size_t slot) {
            if(!a)return;const auto* s=render::FindSurface(*submitted.owner,a->view.surface);
            formats[slot]=s?uint32_t(s->format):0;
            if(a->load==render::Load::Load)loads|=1u<<slot;
            if(a->store==render::Store::Store||a->store==render::Store::StoreAndResolve)stores|=1u<<slot;
            if(a->resolve)resolves|=1u<<slot;
          };
          for(size_t slot=0;slot<4;++slot)describe(pass.colors[slot],slot);describe(pass.depth,4);describe(pass.stencil,5);
          stores&=~uint32_t(dead_stores[pass_commands[t.pass]]);
          loads&=~uint32_t(redundant_loads[pass_commands[t.pass]]);
          char message[1152];std::snprintf(message,sizeof(message),
            "gta4-metal-gpu-pass: sequence=%llu rank=%zu pass=%zu vertex-ms=%.3f fragment-ms=%.3f draws=%zu target=%ux%u samples=%u vs=%016llx ps=%016llx shader-pairs=%zu dominant-draws=%zu host-draws=%zu clears=%zu vertices=%llu indices=%llu formats=%u,%u,%u,%u,%u,%u load-mask=%02x store-mask=%02x resolve-mask=%02x host-mask=%08x profiled=true",
            (unsigned long long)submitted.owner->sequence,rank,t.pass,t.vertex_ms,t.fragment_ms,draws,
            surface?std::max(1u,surface->width>>attachment->view.level):pass.attachmentless_extent[0],
            surface?std::max(1u,surface->height>>attachment->view.level):pass.attachmentless_extent[1],surface?surface->samples:1,
            (unsigned long long)vertex,(unsigned long long)pixel,shaders.size(),dominant_count,host_draws,clears,
            (unsigned long long)vertices,(unsigned long long)indices,
            formats[0],formats[1],formats[2],formats[3],formats[4],formats[5],loads,stores,resolves,host);
          if(diagnostic_)diagnostic_(message);else std::fprintf(stderr,"%s\n",message);
        }
      }
    }
    if(!okay && failure_.empty())failure_=error.empty()?"Metal GPU completion failed":error;
    return okay;
  }
  __strong CAMetalLayer* layer_=nil;
  __strong id<MTLDevice> device_=nil;
  render::BackendCapabilities caps_;
  std::string libraries_,failure_;
  uint32_t maximum_;
  void (*diagnostic_)(const char*)=nullptr;
  uint64_t presentations_=0;double last_gpu_ms_=0;
  uint32_t slow_submissions_=0;
  std::chrono::steady_clock::time_point last_slow_submission_{};
  std::array<double,60> gpu_samples_{};size_t gpu_sample_count_=0;
  double frame_gpu_work_ms_=0;
  bool profile_attempted_=false;uint64_t last_profile_presentation_=0;
  bool open_attempted_=false;
  std::thread::id worker_;
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<FrameAdapter> adapter_;
  std::deque<Submitted> pending_;
};
}
std::unique_ptr<render::FrameBackend> CreateFrameBackend(
    void* layer,std::string libraries,uint32_t maximum,void (*diagnostic)(const char*)) {
  return std::make_unique<Backend>(layer,std::move(libraries),maximum,diagnostic);
}
}
