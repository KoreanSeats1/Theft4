#include "theft4_metal_backend.h"
#include "theft4_metal_frame.h"
#include <algorithm>
#include <deque>
#include <thread>
#include <chrono>
#include <cstdio>
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
    adapter_=std::make_unique<FrameAdapter>(*renderer_);
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
              std::string& error) override {
    if(!Worker(error) || !Healthy(error))return false;
    if(!plan || plan->commands.empty()){error="Empty Metal frame submission rejected";return false;}
    if(present && (!layer_ || !plan->output)) {
      error="Metal publication requires a layer and final output";return false;
    }
    using Clock=std::chrono::steady_clock;const auto began=Clock::now();
    // Reap completed work and bound admission before acquiring a scarce drawable.
    while(!pending_.empty() && (pending_.front().receipt.Completed() || pending_.size()>=maximum_)) {
      if(!FinishOldest(error))return false;
    }
    const auto admitted=Clock::now();
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
        const auto drawable_begin=Clock::now();
        id<CAMetalDrawable> drawable=[layer_ nextDrawable];
        drawable_ms=std::chrono::duration<double,std::milli>(Clock::now()-drawable_begin).count();
        if(!drawable){error="Metal drawable temporarily unavailable";return false;}
        receipt=adapter_->SubmitAndPresent(plan,output->key,drawable,error);
      } else receipt=adapter_->Submit(plan,error);
    }
    if(!receipt)return false;
    if(present&&++presentations_%60==1) {
      const auto& t=adapter_->LastTiming();const auto& e=t.encoder;
      char message[1024];std::snprintf(message,sizeof(message),"gta4-metal-performance: present=%llu wait-ms=%.3f drawable-ms=%.3f validate-ms=%.3f prepare-ms=%.3f encode-ms=%.3f last-gpu-ms=%.3f commands=%llu draws=%llu new-pipelines=%llu new-buffers=%llu new-textures=%llu upload-bytes=%llu binding-calls=%llu avoided-calls=%llu resident-bytes=%zu",
        (unsigned long long)presentations_,std::chrono::duration<double,std::milli>(admitted-began).count(),drawable_ms,
        t.validation_ms,t.preparation_ms,t.encoding_ms,last_gpu_ms_,(unsigned long long)t.commands,(unsigned long long)t.draws,
        (unsigned long long)t.pipelines_created,(unsigned long long)t.buffers_created,(unsigned long long)t.textures_created,
        (unsigned long long)t.uploaded_bytes,(unsigned long long)(e.state_calls+e.buffer_calls+e.texture_calls+e.sampler_calls),
        (unsigned long long)e.avoided_calls,adapter_->Stats().allocated_bytes);
      if(diagnostic_)diagnostic_(message);else std::fprintf(stderr,"%s\n",message);
    }
    pending_.push_back({std::move(receipt),std::move(plan)});
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
    return Healthy(error);
  }
  void Close() override {
    if(!renderer_)return;
    // Normal SDK teardown closes on the worker. Destructor fallback also waits
    // every accepted buffer after the worker has joined; it never encodes work.
    for(auto& submitted:pending_){std::string error;submitted.receipt.Wait(error);}
    pending_.clear();adapter_.reset();renderer_.reset();
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
  struct Submitted{Receipt receipt;std::shared_ptr<const render::FramePlan> owner;};
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
    if(okay)last_gpu_ms_=submitted.receipt.GpuMilliseconds();
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
