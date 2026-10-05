#include "theft4_retail_mode.h"
#include "theft4_native_metal.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <filesystem>

namespace theft4::metal {
namespace {
bool ViewValid(const BufferView& v, NSUInteger required) {
  if(!v.buffer)return false;
  const NSUInteger allocation=v.buffer.length;
  return v.offset<=allocation&&v.length<=allocation-v.offset&&required<=v.length;
}
bool Error(std::string& error, const char* text) { error = text; return false; }
std::string Description(NSError* e) {
  return e ? std::string(e.localizedDescription.UTF8String) : "Metal returned no object";
}
NSUInteger FormatBytes(MTLVertexFormat f) {
  switch(f) {
    case MTLVertexFormatFloat: case MTLVertexFormatInt: case MTLVertexFormatUInt: return 4;
    case MTLVertexFormatFloat2: case MTLVertexFormatInt2: case MTLVertexFormatUInt2: return 8;
    case MTLVertexFormatFloat3: case MTLVertexFormatInt3: case MTLVertexFormatUInt3: return 12;
    case MTLVertexFormatFloat4: case MTLVertexFormatInt4: case MTLVertexFormatUInt4: return 16;
    case MTLVertexFormatHalf2: case MTLVertexFormatShort2: case MTLVertexFormatUShort2:
    case MTLVertexFormatShort2Normalized: case MTLVertexFormatUShort2Normalized:
    case MTLVertexFormatUChar4: case MTLVertexFormatUChar4Normalized:
    case MTLVertexFormatUChar4Normalized_BGRA: case MTLVertexFormatInt1010102Normalized: return 4;
    case MTLVertexFormatHalf4: case MTLVertexFormatShort4: case MTLVertexFormatUShort4:
    case MTLVertexFormatShort4Normalized: case MTLVertexFormatUShort4Normalized: return 8;
    default: return 0;
  }
}
}
struct Renderer::Impl {
  id<MTLBinaryArchive> archive=nil;
  NSURL* archive_url=nil;bool archive_dirty=false;uint64_t archive_hits=0;
  struct ClearKey {
    std::array<MTLPixelFormat,6> formats{};
    NSUInteger samples=1;
    uint32_t colors=0;
    bool depth=false,stencil=false;
    auto operator<=>(const ClearKey&) const = default;
  };
  struct ClearPipeline { id<MTLRenderPipelineState> pipeline;id<MTLDepthStencilState> depth; };
  std::map<ClearKey,ClearPipeline> clears;
  std::map<std::array<uint32_t,16>,id<MTLDepthStencilState>> depth_states;
  id<MTLDepthStencilState> DepthState(MTLDepthStencilDescriptor* descriptor) {
    std::array<uint32_t,16> key{};key[0]=uint32_t(descriptor.depthCompareFunction);
    key[1]=descriptor.depthWriteEnabled;size_t n=2;
    for(auto stencil:{descriptor.frontFaceStencil,descriptor.backFaceStencil}) {
      key[n++]=bool(stencil);
      key[n++]=uint32_t(stencil.stencilCompareFunction);
      key[n++]=uint32_t(stencil.stencilFailureOperation);
      key[n++]=uint32_t(stencil.depthFailureOperation);
      key[n++]=uint32_t(stencil.depthStencilPassOperation);
      key[n++]=stencil.readMask;key[n++]=stencil.writeMask;
    }
    if(auto found=depth_states.find(key);found!=depth_states.end())return found->second;
    auto state=[device newDepthStencilStateWithDescriptor:descriptor];
    if(state)depth_states.emplace(key,state);return state;
  }
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  id<MTLCommandQueue> queue = nil;
  dispatch_semaphore_t slots;
  explicit Impl(NSUInteger maximum) : slots(dispatch_semaphore_create(long(maximum))) {
    queue = [device newCommandQueue]; queue.label = @"Theft4 Direct Metal";
  }
};
struct Frame::Impl {
  std::shared_ptr<Renderer::Impl> renderer;
  id<MTLCommandBuffer> buffer = nil;
  id<MTLRenderCommandEncoder> encoder = nil;
  MTLRenderPassDescriptor* pass = nil;
  std::shared_ptr<void> lease;
  bool submitted = false;
  id<MTLTexture> stored_color = nil;
  bool presented = false;
  struct Bind {const void* object=nullptr;NSUInteger offset=0;};
  struct State {
    bool valid=false;
    const void* pipeline=nullptr;const void* depth=nullptr;
    MTLViewport viewport{};MTLScissorRect scissor{};
    MTLCullMode cull{};MTLWinding winding{};
    NSUInteger front=0,back=0;
    std::array<float,4> blend{};float bias=0,slope=0;
    bool clamp=false,lines=false;
    std::array<Bind,31> vertex_buffers{},fragment_buffers{};
    std::array<const void*,31> vertex_textures{},fragment_textures{};
    std::array<const void*,16> vertex_samplers{},fragment_samplers{};
  } state;
  struct PassShape {
    std::array<MTLPixelFormat,4> colors{};
    std::array<NSUInteger,4> color_samples{};
    MTLPixelFormat depth=MTLPixelFormatInvalid,stencil=MTLPixelFormatInvalid;
    NSUInteger depth_samples=0,stencil_samples=0,width=0,height=0,default_samples=0;
  } shape;
  EncoderStats stats;
  id<MTLCounterSampleBuffer> counters=nil;
  size_t profiled_passes=0;
  std::vector<size_t> pass_mapping;
  MTLTimestamp cpu_reference=0,gpu_reference=0;
  ~Impl() { if (encoder) [encoder endEncoding]; }
};
Renderer::Renderer(NSUInteger maximum) {
  if (maximum >= 1 && maximum <= 3) impl_ = std::make_shared<Impl>(maximum);
}
Renderer::~Renderer() = default;
bool Renderer::Ready() const { return impl_ && impl_->device && impl_->queue; }
id<MTLDevice> Renderer::Device() const { return impl_ ? impl_->device : nil; }
id<MTLBuffer> Renderer::ImmutableBuffer(std::span<const uint8_t> bytes, std::string& error) {
  if (!Ready() || bytes.empty() || bytes.size() > Device().maxBufferLength) {
    error = "Invalid Metal immutable buffer size"; return nil;
  }
  auto buffer = [Device() newBufferWithBytes:bytes.data() length:bytes.size()
      options:MTLResourceStorageModeShared];
  if (!buffer) error = "Metal immutable buffer allocation failed";
  return buffer;
}
id<MTLTexture> Renderer::Texture(MTLTextureDescriptor* d, std::string& error) {
  if (!Ready() || !d || !d.width || !d.height || !d.depth || !d.mipmapLevelCount ||
      ![Device() supportsTextureSampleCount:d.sampleCount]) {
    error = "Invalid Metal texture descriptor"; return nil;
  }
  if (d.pixelFormat >= MTLPixelFormatBC1_RGBA && d.pixelFormat <= MTLPixelFormatBC7_RGBAUnorm_sRGB
      && !Device().supportsBCTextureCompression) {
    error = "This Metal device requires the prepared ASTC texture cache"; return nil;
  }
  auto texture = [Device() newTextureWithDescriptor:d];
  if (!texture) error = "Metal texture allocation failed";
  return texture;
}
id<MTLLibrary> Renderer::LoadLibrary(std::span<const uint8_t> bytes,std::string& error) {
  if(!Ready()||bytes.empty()){error="Invalid Metal library bytes";return nil;}
  dispatch_data_t data = dispatch_data_create(bytes.data(), bytes.size(), nullptr,
                                             DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  NSError* e = nil;
  id<MTLLibrary> library = [Device() newLibraryWithData:data error:&e];
  if(!library)error=Description(e);else error.clear();return library;
}
Shader Renderer::LoadShader(std::span<const uint8_t> bytes, Stage stage,
    ShaderInterface interface, uint32_t specialization, std::string& error,const char* entry,std::span<const uint32_t> host_constants) {
  return LoadShader(LoadLibrary(bytes,error),stage,interface,specialization,error,entry,host_constants);
}
Shader Renderer::LoadShader(id<MTLLibrary> library, Stage stage,
    ShaderInterface interface, uint32_t specialization, std::string& error,const char* entry,std::span<const uint32_t> host_constants) {
  Shader result; result.stage = stage; result.interface = interface;
  if(!library){if(error.empty())error="Missing Metal shader library";return result;}
  if(!Ready()||library.device!=Device()||(stage!=Stage::Vertex&&stage!=Stage::Fragment)||
     (interface.textures&0x80000000u)||(interface.samplers&0xffff0000u)) {
    error="Invalid Metal shader interface";return result;
  }
  NSError* e=nil;
  MTLFunctionConstantValues* values = [MTLFunctionConstantValues new];
  if(!entry||(std::string(entry)!="theft4_shader"&&specialization)) {
    error="Unsupported Metal entry point specialization";return result;
  }
  if(std::string(entry)=="theft4_shader")[values setConstantValue:&specialization type:MTLDataTypeUInt atIndex:0];
  if(host_constants.size()>8||(std::string(entry)=="theft4_shader"&&!host_constants.empty())) {
    error="Invalid host shader function constants";return result;
  }
  for(size_t i=0;i<host_constants.size();++i)[values setConstantValue:&host_constants[i] type:MTLDataTypeUInt atIndex:i+1];
  NSString* name=[NSString stringWithUTF8String:entry];
  // Even undefined optional constants require a concrete specialized
  // function. Plain lookup can return an abstract function that Metal
  // rejects when creating the fallback render pipeline.
  result.function=[library newFunctionWithName:name constantValues:values error:&e];
  if (!result.function) { error = Description(e); return result; }
  const auto expected = stage == Stage::Vertex ? MTLFunctionTypeVertex : MTLFunctionTypeFragment;
  if (result.function.functionType != expected) {
    result.function = nil; error = "Metal shader stage mismatch";
  }else error.clear();
  return result;
}
std::shared_ptr<const Pipeline> Renderer::MakePipeline(const Shader& vs, const Shader& ps,
    MTLRenderPipelineDescriptor* fixed, MTLDepthStencilDescriptor* depth,
    std::string& error) {
  return MakePipelineInternal(vs,&ps,fixed,depth,error);
}
std::shared_ptr<const Pipeline> Renderer::MakeDepthPipeline(const Shader& vs,
    MTLRenderPipelineDescriptor* fixed, MTLDepthStencilDescriptor* depth,std::string& error) {
  if (!fixed || !depth || (fixed.depthAttachmentPixelFormat==MTLPixelFormatInvalid &&
      fixed.stencilAttachmentPixelFormat==MTLPixelFormatInvalid)) {
    error="A Metal depth-only pipeline requires a depth/stencil target and state";return {};
  }
  for (NSUInteger i=0;i<8;++i) if(fixed.colorAttachments[i].pixelFormat!=MTLPixelFormatInvalid) {
    error="A Metal depth-only pipeline cannot have color targets";return {};
  }
  return MakePipelineInternal(vs,nullptr,fixed,depth,error);
}
std::shared_ptr<const Pipeline> Renderer::MakePipelineInternal(const Shader& vs, const Shader* ps,
    MTLRenderPipelineDescriptor* fixed, MTLDepthStencilDescriptor* depth,std::string& error) {
  if (!Ready() || !vs.function || !fixed || vs.stage != Stage::Vertex ||
      (ps && (!ps->function || ps->stage != Stage::Fragment))) {
    error = "Invalid direct Metal shader pair"; return {};
  }
  MTLRenderPipelineDescriptor* desc = [fixed copy]; desc.vertexFunction = vs.function;
  desc.fragmentFunction = ps ? ps->function : nil;
  auto p = std::make_shared<Pipeline>(); p->vertex = vs.interface;
  if(ps)p->fragment = ps->interface;else p->fragment.constant_bytes={};
  for(size_t i=0;i<3;++i)p->constant_bytes[i]=std::max(vs.interface.constant_bytes[i],ps ? ps->interface.constant_bytes[i] : 0ul);
  p->samples = desc.rasterSampleCount; p->depth = desc.depthAttachmentPixelFormat;
  p->stencil = desc.stencilAttachmentPixelFormat;
  for (NSUInteger i = 0; i < 4; ++i) p->colors[i] = desc.colorAttachments[i].pixelFormat;
  for (NSUInteger i = 4; i < 8; ++i) if (desc.colorAttachments[i].pixelFormat != MTLPixelFormatInvalid) {
    error = "The game pass contract supports four color targets"; return {};
  }
  for (NSUInteger i = 0; i < 31; ++i) {
    auto a = desc.vertexDescriptor.attributes[i];
    if (a.format == MTLVertexFormatInvalid) continue;
    const NSUInteger size = FormatBytes(a.format);
    if (!size || a.bufferIndex < 8 || a.bufferIndex >= 8+kGameVertexStreamCount ||
        a.offset > std::numeric_limits<NSUInteger>::max() - size) {
      error = "Unsupported Metal vertex declaration"; return {};
    }
    const NSUInteger s = a.bufferIndex - 8;
    auto layout = desc.vertexDescriptor.layouts[a.bufferIndex];
    if (!layout.stride || (layout.stepFunction != MTLVertexStepFunctionPerVertex &&
        layout.stepFunction != MTLVertexStepFunctionPerInstance) ||
        layout.stepRate != 1 || a.offset + size > layout.stride) {
      error = "Unsupported Metal vertex stream layout"; return {};
    }
    p->vertex_streams |= 1u << s; p->strides[s] = layout.stride;
    if(layout.stepFunction==MTLVertexStepFunctionPerInstance)p->instance_streams|=1u<<s;
    p->attribute_extents[s] = std::max(p->attribute_extents[s], a.offset + size);
  }
  // Every admitted bank is an immutable snapshot through GPU completion.
  for(size_t i=0;i<3;++i) {
    if(p->vertex.constant_bytes[i])desc.vertexBuffers[i].mutability=MTLMutabilityImmutable;
    if(p->fragment.constant_bytes[i])desc.fragmentBuffers[i].mutability=MTLMutabilityImmutable;
  }
  NSError* e = nil;
  if(impl_->archive) {
    desc.binaryArchives=@[impl_->archive];
    p->state=[Device() newRenderPipelineStateWithDescriptor:desc options:MTLPipelineOptionFailOnBinaryArchiveMiss reflection:nil error:&e];
    if(p->state)++impl_->archive_hits;
    else {
      // Compile the missing functions into the archive once. No disk IO is
      // performed during drawing; persist only when the backend is drained.
      e=nil;if([impl_->archive addRenderPipelineFunctionsWithDescriptor:desc error:&e])impl_->archive_dirty=true;
    }
  }
  if(!p->state){e=nil;p->state=[Device() newRenderPipelineStateWithDescriptor:desc error:&e];}
  if(!p->state&&impl_->archive) {
    // Binary archives are optional. Retry without a failing archive before
    // rejecting an otherwise valid pipeline, and stop consulting it.
    desc.binaryArchives=nil;impl_->archive=nil;impl_->archive_dirty=false;
    e=nil;p->state=[Device() newRenderPipelineStateWithDescriptor:desc error:&e];
  }
  if (!p->state) { error = Description(e); return {}; }
  if (depth) {
    p->depth_stencil = impl_->DepthState(depth);
    if (!p->depth_stencil) { error = "Metal depth state creation failed"; return {}; }
  }
  return p;
}
std::shared_ptr<const Pipeline> Renderer::MakeDepthVariant(const Pipeline& base,
    MTLDepthStencilDescriptor* depth,std::string& error) {
  if(!Ready()||!base.state||base.state.device!=Device()||!depth){error="Invalid shared Metal render pipeline";return {};}
  auto result=std::make_shared<Pipeline>(base);
  result->depth_stencil=impl_->DepthState(depth);
  if(!result->depth_stencil){error="Metal depth state creation failed";return {};}
  error.clear();return result;
}
void Renderer::ConfigurePipelineArchive(const std::string& path) {
  if(!Ready()||path.empty()||impl_->archive)return;
  try {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    auto descriptor=[MTLBinaryArchiveDescriptor new];
    impl_->archive_url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    if(std::filesystem::exists(path)&&std::filesystem::file_size(path)<=256ull*1024*1024)descriptor.url=impl_->archive_url;
    NSError* error=nil;impl_->archive=[Device() newBinaryArchiveWithDescriptor:descriptor error:&error];
    if(!impl_->archive){descriptor.url=nil;impl_->archive=[Device() newBinaryArchiveWithDescriptor:descriptor error:&error];}
  }catch(...){impl_->archive=nil;impl_->archive_url=nil;}
}
void Renderer::FlushPipelineArchive() {
  if(!impl_->archive||!impl_->archive_dirty||!impl_->archive_url)return;
  // An atomic rename leaves the last usable archive intact after interruption.
  auto temporary=[NSURL fileURLWithPath:[impl_->archive_url.path stringByAppendingString:@".tmp"]];
  NSError* error=nil;
  if([impl_->archive serializeToURL:temporary error:&error]) {
    if(std::rename(temporary.path.UTF8String,impl_->archive_url.path.UTF8String)==0)impl_->archive_dirty=false;
  }
}
uint64_t Renderer::PipelineArchiveHits() const{return impl_->archive_hits;}
Frame Renderer::BeginFrame(std::string& error) {
  if (!Ready()) { error = "Direct Metal renderer is unavailable"; return {}; }
  // Bounded wait. Aborted frames release the lease just like completed frames.
  if (dispatch_semaphore_wait(impl_->slots, dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC))) {
    error = "All direct Metal frame slots are still in use"; return {};
  }
  auto p = std::make_unique<Frame::Impl>();
  p->renderer=impl_;
  p->lease = std::shared_ptr<void>(impl_.get(), [owner = impl_](void*) {
    dispatch_semaphore_signal(owner->slots);
  });
  p->buffer = [impl_->queue commandBuffer];
  if (!p->buffer) { error = "Metal command buffer allocation failed"; return {}; }
  p->buffer.label = @"Theft4 Native Game Passes";
  return Frame(std::move(p));
}
Frame::Frame() = default;
Frame::Frame(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Frame::~Frame() = default;
Frame::Frame(Frame&&) noexcept = default;
Frame& Frame::operator=(Frame&&) noexcept = default;
Frame::operator bool() const { return impl_ && !impl_->submitted && impl_->buffer; }
bool Frame::ProfilePasses(size_t maximum_passes,std::span<const size_t> pass_mapping) {
  if(theft4_retail_mode())return false;
  if(!pass_mapping.empty()&&pass_mapping.size()!=maximum_passes)return false;
  if(!*this||impl_->encoder||impl_->counters||!maximum_passes||maximum_passes>4096)return false;
  impl_->pass_mapping.assign(pass_mapping.begin(),pass_mapping.end());
  auto device=impl_->renderer->device;
  // GPU timestamp clocks are hardware dependent. Capture a paired reference
  // before encoding; the receipt samples again after completion to calibrate
  // counter ticks against Metal's CPU nanoseconds. This never waits for GPU work.
  if(![device supportsFamily:MTLGPUFamilyApple1]||
     ![device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])return false;
  id<MTLCounterSet> timestamps=nil;
  for(id<MTLCounterSet> set in device.counterSets)if([set.name isEqualToString:MTLCommonCounterSetTimestamp]) {
    for(id<MTLCounter> counter in set.counters)if([counter.name isEqualToString:MTLCommonCounterTimestamp])timestamps=set;
  }
  if(!timestamps)return false;
  auto descriptor=[MTLCounterSampleBufferDescriptor new];descriptor.counterSet=timestamps;
  descriptor.storageMode=MTLStorageModeShared;descriptor.sampleCount=maximum_passes*4;
  descriptor.label=@"Theft4 Occasional Pass Timings";
  NSError* error=nil;impl_->counters=[device newCounterSampleBufferWithDescriptor:descriptor error:&error];
  if(impl_->counters)[device sampleTimestamps:&impl_->cpu_reference gpuTimestamp:&impl_->gpu_reference];
  if(!impl_->cpu_reference||!impl_->gpu_reference)impl_->counters=nil;
  return impl_->counters!=nil;
}
bool Frame::BeginPass(MTLRenderPassDescriptor* pass, std::string& error) {
  if (!*this || impl_->encoder || !pass) return Error(error, "Invalid direct Metal pass transition");
  impl_->state={};
  impl_->pass = [pass copy];
  // Attachment properties are immutable for this encoder's copied descriptor.
  // Read them once instead of messaging every Metal object for every draw.
  impl_->shape={};auto& shape=impl_->shape;
  for(size_t i=0;i<4;++i) {
    auto texture=impl_->pass.colorAttachments[i].texture;
    if(texture) {
      shape.colors[i]=texture.pixelFormat;shape.color_samples[i]=texture.sampleCount;
      shape.width=texture.width;shape.height=texture.height;
    }
  }
  auto depth=impl_->pass.depthAttachment.texture,stencil=impl_->pass.stencilAttachment.texture;
  if(depth){shape.depth=depth.pixelFormat;shape.depth_samples=depth.sampleCount;
    if(!shape.width){shape.width=depth.width;shape.height=depth.height;}}
  if(stencil){shape.stencil=stencil.pixelFormat;shape.stencil_samples=stencil.sampleCount;
    if(!shape.width){shape.width=stencil.width;shape.height=stencil.height;}}
  if(!shape.width&&!depth&&!stencil) {
    shape.width=impl_->pass.renderTargetWidth;shape.height=impl_->pass.renderTargetHeight;
  }
  shape.default_samples=impl_->pass.defaultRasterSampleCount;
  if(impl_->counters&&impl_->profiled_passes<impl_->counters.sampleCount/4) {
    auto attachment=impl_->pass.sampleBufferAttachments[0];attachment.sampleBuffer=impl_->counters;
    const size_t first=impl_->profiled_passes++*4;
    attachment.startOfVertexSampleIndex=first;attachment.endOfVertexSampleIndex=first+1;
    attachment.startOfFragmentSampleIndex=first+2;attachment.endOfFragmentSampleIndex=first+3;
  }
  impl_->encoder = [impl_->buffer renderCommandEncoderWithDescriptor:impl_->pass];
  if (!impl_->encoder) return Error(error, "Metal render encoder allocation failed");
  return true;
}
bool Frame::Encode(const Draw& d, std::string& error) {
  if (!*this || !impl_->encoder || !d.pipeline || !d.pipeline->state || !d.instance_count)
    return Error(error, "Invalid direct Metal draw state");
  const auto& p = *d.pipeline;
  const auto& shape=impl_->shape;
  const NSUInteger width=shape.width,height=shape.height;
  for (NSUInteger i = 0; i < 4; ++i) {
    if(shape.colors[i]!=p.colors[i])
      return Error(error, "Metal pipeline color target does not match its pass");
    if(shape.color_samples[i]&&shape.color_samples[i]!=p.samples)
      return Error(error,"Metal color sample count mismatch");
  }
  if(shape.depth!=p.depth||shape.stencil!=p.stencil)
    return Error(error, "Metal depth/stencil target does not match its pipeline");
  if(shape.depth_samples&&shape.depth_samples!=p.samples)return Error(error,"Metal depth sample count mismatch");
  if(shape.stencil_samples&&shape.stencil_samples!=p.samples)return Error(error,"Metal stencil sample count mismatch");
  if(std::all_of(shape.color_samples.begin(),shape.color_samples.end(),[](NSUInteger s){return !s;})&&
     !shape.depth_samples&&!shape.stencil_samples&&p.samples!=shape.default_samples)
    return Error(error,"Metal attachmentless sample count mismatch");
  if (!width || !height || !std::isfinite(d.viewport.originX) || !std::isfinite(d.viewport.originY) ||
      !std::isfinite(d.viewport.width) || !std::isfinite(d.viewport.height) ||
      !std::isfinite(d.viewport.znear) || !std::isfinite(d.viewport.zfar) ||
      d.viewport.width <= 0 || d.viewport.height <= 0 || d.viewport.znear < 0 ||
      d.viewport.znear > 1 || d.viewport.zfar < 0 || d.viewport.zfar > 1 ||
      d.scissor.x > width || d.scissor.y > height || !d.scissor.width || !d.scissor.height ||
      d.scissor.width > width - d.scissor.x || d.scissor.height > height - d.scissor.y)
    return Error(error, "Invalid direct Metal viewport/scissor");
  if(!std::isfinite(d.depth_bias)||!std::isfinite(d.slope_bias)||
      d.stencil_reference>255||d.stencil_back_reference>255)
    return Error(error,"Invalid direct Metal dynamic state");
  for(float value:d.blend_color)if(!std::isfinite(value))
    return Error(error,"Invalid direct Metal blend color");
  for (size_t i = 0; i < 3; ++i) if (p.constant_bytes[i]&&(!ViewValid(d.constants[i], p.constant_bytes[i]) || d.constants[i].offset % 16))
    return Error(error, "Invalid Metal game constant bank");
  NSUInteger maximum = d.maximum_vertex;
  if (d.index_count) {
    const NSUInteger size = d.index_type == MTLIndexTypeUInt16 ? 2 : 4;
    if (d.index_count > std::numeric_limits<NSUInteger>::max() / size ||
        !ViewValid(d.indices, d.index_count * size) || d.indices.offset % size)
      return Error(error, "Invalid Metal index range");
  } else {
    if (!d.vertex_count || d.first_vertex > std::numeric_limits<NSUInteger>::max() - (d.vertex_count - 1))
      return Error(error, "Invalid Metal vertex range");
    maximum = d.first_vertex + d.vertex_count - 1;
  }
  for (NSUInteger s = 0; s < kGameVertexStreamCount; ++s) if (p.vertex_streams & (1u << s)) {
    const NSUInteger last=(p.instance_streams & (1u<<s)) ? d.instance_count-1 : maximum;
    if (last > (std::numeric_limits<NSUInteger>::max() - p.attribute_extents[s]) / p.strides[s] ||
        !ViewValid(d.vertices[s], last * p.strides[s] + p.attribute_extents[s]))
      return Error(error, "Invalid Metal vertex stream range");
  }
  uint32_t texture_masks[2]{}, sampler_masks[2]{};
  for (const auto& b : d.textures) {
    const unsigned s = b.stage == Stage::Vertex ? 0 : 1;
    const auto& abi = s ? p.fragment : p.vertex;
    if ((b.stage!=Stage::Vertex&&b.stage!=Stage::Fragment) || b.index >= 31 || !b.texture || b.texture.framebufferOnly || !(abi.textures & (1u << b.index)) ||
        (texture_masks[s] & (1u << b.index)) || b.texture.textureType != abi.texture_types[b.index])
      return Error(error, "Invalid Metal texture binding");
    texture_masks[s] |= 1u << b.index;
  }
  for (const auto& b : d.samplers) {
    const unsigned s = b.stage == Stage::Vertex ? 0 : 1;
    const auto& abi = s ? p.fragment : p.vertex;
    if ((b.stage!=Stage::Vertex&&b.stage!=Stage::Fragment) || b.index >= 16 || !b.sampler || !(abi.samplers & (1u << b.index)) ||
        (sampler_masks[s] & (1u << b.index))) return Error(error, "Invalid Metal sampler binding");
    sampler_masks[s] |= 1u << b.index;
  }
  if (texture_masks[0] != p.vertex.textures || texture_masks[1] != p.fragment.textures ||
      sampler_masks[0] != p.vertex.samplers || sampler_masks[1] != p.fragment.samplers)
    return Error(error, "Incomplete Metal game resource bindings");
  // Admission is complete. No binding changes happen before every range and
  // interface has been checked, so a rejected packet cannot corrupt a later draw.
  auto e = impl_->encoder;auto& cached=impl_->state;auto& stats=impl_->stats;
  const bool first=!cached.valid;
  const auto changed=[&](bool difference,auto&& apply) {
    if(first||difference){apply();++stats.state_calls;}else ++stats.avoided_calls;
  };
  changed(cached.pipeline!=(__bridge const void*)p.state,[&]{[e setRenderPipelineState:p.state];cached.pipeline=(__bridge const void*)p.state;});
  changed(cached.depth!=(__bridge const void*)p.depth_stencil,[&]{[e setDepthStencilState:p.depth_stencil];cached.depth=(__bridge const void*)p.depth_stencil;});
  changed(std::memcmp(&cached.viewport,&d.viewport,sizeof(d.viewport))!=0,[&]{[e setViewport:d.viewport];cached.viewport=d.viewport;});
  changed(std::memcmp(&cached.scissor,&d.scissor,sizeof(d.scissor))!=0,[&]{[e setScissorRect:d.scissor];cached.scissor=d.scissor;});
  changed(cached.cull!=d.cull,[&]{[e setCullMode:d.cull];cached.cull=d.cull;});
  changed(cached.winding!=d.winding,[&]{[e setFrontFacingWinding:d.winding];cached.winding=d.winding;});
  changed(cached.front!=d.stencil_reference||cached.back!=d.stencil_back_reference,[&]{
    [e setStencilFrontReferenceValue:uint32_t(d.stencil_reference) backReferenceValue:uint32_t(d.stencil_back_reference)];
    cached.front=d.stencil_reference;cached.back=d.stencil_back_reference;
  });
  changed(std::memcmp(cached.blend.data(),d.blend_color.data(),sizeof(d.blend_color))!=0,[&]{[e setBlendColorRed:d.blend_color[0] green:d.blend_color[1] blue:d.blend_color[2] alpha:d.blend_color[3]];cached.blend=d.blend_color;});
  changed(std::bit_cast<uint32_t>(cached.bias)!=std::bit_cast<uint32_t>(d.depth_bias)||std::bit_cast<uint32_t>(cached.slope)!=std::bit_cast<uint32_t>(d.slope_bias),[&]{[e setDepthBias:d.depth_bias slopeScale:d.slope_bias clamp:0];cached.bias=d.depth_bias;cached.slope=d.slope_bias;});
  changed(cached.clamp!=d.depth_clamp,[&]{[e setDepthClipMode:d.depth_clamp?MTLDepthClipModeClamp:MTLDepthClipModeClip];cached.clamp=d.depth_clamp;});
  changed(cached.lines!=d.lines,[&]{[e setTriangleFillMode:d.lines?MTLTriangleFillModeLines:MTLTriangleFillModeFill];cached.lines=d.lines;});
  const auto buffer=[&](Stage stage,NSUInteger slot,const BufferView& view) {
    auto& previous=stage==Stage::Vertex?cached.vertex_buffers[slot]:cached.fragment_buffers[slot];
    const auto object=(__bridge const void*)view.buffer;
    if(previous.object==object&&previous.offset==view.offset){++stats.avoided_calls;return;}
    if(previous.object==object) {
      if(stage==Stage::Vertex)[e setVertexBufferOffset:view.offset atIndex:slot];
      else [e setFragmentBufferOffset:view.offset atIndex:slot];
      ++stats.buffer_offset_calls;
    }else {
      if(stage==Stage::Vertex)[e setVertexBuffer:view.buffer offset:view.offset atIndex:slot];
      else [e setFragmentBuffer:view.buffer offset:view.offset atIndex:slot];
    }
    previous={object,view.offset};++stats.buffer_calls;
  };
  for(NSUInteger i=0;i<3;++i) {
    if(p.vertex.constant_bytes[i])buffer(Stage::Vertex,i,d.constants[i]);
    if(p.fragment.constant_bytes[i])buffer(Stage::Fragment,i,d.constants[i]);
  }
  for(NSUInteger slot=0;slot<kGameVertexStreamCount;++slot)if(p.vertex_streams&(1u<<slot))
    buffer(Stage::Vertex,slot+8,d.vertices[slot]);
  // Batch each changed stage range in one driver call. The draw still owns
  // and validates every texture/sampler; unused holes are explicitly nil.
  for(Stage stage:{Stage::Vertex,Stage::Fragment}) {
    id<MTLTexture> __unsafe_unretained textures[31]={};
    auto& previous=stage==Stage::Vertex?cached.vertex_textures:cached.fragment_textures;
    NSUInteger first_changed=31,last_changed=0;
    for(const auto& binding:d.textures)if(binding.stage==stage) {
      textures[binding.index]=binding.texture;
      const auto object=(__bridge const void*)binding.texture;
      if(previous[binding.index]==object){++stats.avoided_calls;continue;}
      first_changed=std::min(first_changed,binding.index);last_changed=std::max(last_changed,binding.index);
    }
    if(first_changed!=31) {
      const auto range=NSMakeRange(first_changed,last_changed-first_changed+1);
      if(stage==Stage::Vertex)[e setVertexTextures:textures+first_changed withRange:range];
      else [e setFragmentTextures:textures+first_changed withRange:range];
      for(NSUInteger i=first_changed;i<=last_changed;++i)previous[i]=(__bridge const void*)textures[i];
      ++stats.texture_calls;
    }
    id<MTLSamplerState> __unsafe_unretained samplers[16]={};
    auto& old_samplers=stage==Stage::Vertex?cached.vertex_samplers:cached.fragment_samplers;
    first_changed=16;last_changed=0;
    for(const auto& binding:d.samplers)if(binding.stage==stage) {
      samplers[binding.index]=binding.sampler;
      const auto object=(__bridge const void*)binding.sampler;
      if(old_samplers[binding.index]==object){++stats.avoided_calls;continue;}
      first_changed=std::min(first_changed,binding.index);last_changed=std::max(last_changed,binding.index);
    }
    if(first_changed!=16) {
      const auto range=NSMakeRange(first_changed,last_changed-first_changed+1);
      if(stage==Stage::Vertex)[e setVertexSamplerStates:samplers+first_changed withRange:range];
      else [e setFragmentSamplerStates:samplers+first_changed withRange:range];
      for(NSUInteger i=first_changed;i<=last_changed;++i)old_samplers[i]=(__bridge const void*)samplers[i];
      ++stats.sampler_calls;
    }
  }
  cached.valid=true;++stats.draws;
  if (d.index_count) [e drawIndexedPrimitives:d.primitive indexCount:d.index_count indexType:d.index_type
      indexBuffer:d.indices.buffer indexBufferOffset:d.indices.offset instanceCount:d.instance_count
      baseVertex:d.base_vertex baseInstance:0];
  else [e drawPrimitives:d.primitive vertexStart:d.first_vertex vertexCount:d.vertex_count
      instanceCount:d.instance_count];
  return true;
}
EncoderStats Frame::Stats() const{return impl_?impl_->stats:EncoderStats{};}
bool Frame::EndPass(std::string& error) {
  if (!*this || !impl_->encoder) return Error(error, "No direct Metal pass to end");
  auto output = impl_->pass.colorAttachments[0];
  impl_->stored_color = output.storeAction == MTLStoreActionStore ? output.texture :
      (output.storeAction == MTLStoreActionMultisampleResolve ||
       output.storeAction == MTLStoreActionStoreAndMultisampleResolve) ? output.resolveTexture : nil;
  [impl_->encoder endEncoding]; impl_->encoder = nil; impl_->pass = nil; return true;
}
bool Frame::ClearRectangle(const Clear& clear,std::string& error) {
  if(!*this||!impl_->encoder)return Error(error,"Rectangular clear requires an active Metal pass");
  Renderer::Impl::ClearKey key;key.colors=clear.colors;key.depth=clear.depth;key.stencil=clear.stencil;
  NSUInteger width=0,height=0;
  const auto shape=[&](id<MTLTexture> t) {
    if(!t)return true;
    if(width&&(width!=t.width||height!=t.height||key.samples!=t.sampleCount))return false;
    width=t.width;height=t.height;key.samples=t.sampleCount;return true;
  };
  for(size_t i=0;i<4;++i) {
    auto t=impl_->pass.colorAttachments[i].texture;
    key.formats[i]=t ? t.pixelFormat : MTLPixelFormatInvalid;
    if(!shape(t)||((clear.colors&(1u<<i))&&!t))return Error(error,"Rectangular clear color attachment mismatch");
  }
  auto depth=impl_->pass.depthAttachment.texture,stencil=impl_->pass.stencilAttachment.texture;
  key.formats[4]=depth ? depth.pixelFormat : MTLPixelFormatInvalid;
  key.formats[5]=stencil ? stencil.pixelFormat : MTLPixelFormatInvalid;
  const auto& r=clear.rectangle;
  if(!shape(depth)||!shape(stencil)||!width||(clear.depth&&!depth)||(clear.stencil&&!stencil)||
     (clear.colors&~15u)||(!clear.colors&&!clear.depth&&!clear.stencil)||
     !r.width||!r.height||r.x>width||r.y>height||r.width>width-r.x||r.height>height-r.y||
     !std::isfinite(clear.depth_value)||clear.depth_value<0||clear.depth_value>1||clear.stencil_value>255)
    return Error(error,"Invalid Metal rectangular clear range or aspect");
  for(float c:clear.color)if(!std::isfinite(c))return Error(error,"Nonfinite Metal rectangular clear color");
  auto& cache=impl_->renderer->clears;
  auto found=cache.find(key);
  if(found==cache.end()) {
    // This utility pipeline is cached by attachment shape and aspect mask.
    // No guest constants, draw state, blend state or shaders participate.
    std::string source="#include <metal_stdlib>\nusing namespace metal;\n"
      "struct V { float4 p [[position]]; };\n"
      "vertex V clear_vs(uint id [[vertex_id]]) { const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; return V{float4(p[id],0,1)}; }\n";
    bool output=clear.depth;
    for(size_t i=0;i<4;++i)output|=key.formats[i]!=MTLPixelFormatInvalid;
    if(output) {
      source+="struct O { ";
      for(size_t i=0;i<4;++i)if(key.formats[i]!=MTLPixelFormatInvalid)
        source+="float4 c"+std::to_string(i)+" [[color("+std::to_string(i)+")]]; ";
      if(clear.depth)source+="float d [[depth(any)]]; ";
      source+="};\nfragment O clear_fs(constant float4* data [[buffer(0)]]) { O o; ";
      for(size_t i=0;i<4;++i)if(key.formats[i]!=MTLPixelFormatInvalid)
        source+="o.c"+std::to_string(i)+"=data[0]; ";
      if(clear.depth)source+="o.d=data[1].x; ";
      source+="return o; }\n";
    }else source+="fragment void clear_fs() {}\n";
    NSError* e=nil;auto options=[MTLCompileOptions new];options.languageVersion=MTLLanguageVersion2_4;
    auto library=[impl_->renderer->device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()] options:options error:&e];
    if(!library){error=Description(e);return false;}
    auto fixed=[MTLRenderPipelineDescriptor new];fixed.label=@"Theft4 Rectangular Attachment Clear";
    fixed.vertexFunction=[library newFunctionWithName:@"clear_vs"];
    fixed.fragmentFunction=[library newFunctionWithName:@"clear_fs"];fixed.rasterSampleCount=key.samples;
    for(size_t i=0;i<4;++i){fixed.colorAttachments[i].pixelFormat=key.formats[i];
      fixed.colorAttachments[i].writeMask=(clear.colors&(1u<<i)) ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;}
    fixed.depthAttachmentPixelFormat=key.formats[4];fixed.stencilAttachmentPixelFormat=key.formats[5];
    auto pipeline=[impl_->renderer->device newRenderPipelineStateWithDescriptor:fixed error:&e];
    if(!pipeline){error=Description(e);return false;}
    auto d=[MTLDepthStencilDescriptor new];d.depthCompareFunction=MTLCompareFunctionAlways;d.depthWriteEnabled=clear.depth;
    if(clear.stencil){auto s=[MTLStencilDescriptor new];s.stencilCompareFunction=MTLCompareFunctionAlways;
      s.stencilFailureOperation=s.depthFailureOperation=s.depthStencilPassOperation=MTLStencilOperationReplace;
      s.readMask=s.writeMask=255;d.frontFaceStencil=d.backFaceStencil=s;}
    auto ds=[impl_->renderer->device newDepthStencilStateWithDescriptor:d];
    if(!ds)return Error(error,"Metal rectangular clear depth state creation failed");
    found=cache.emplace(key,Renderer::Impl::ClearPipeline{pipeline,ds}).first;
  }
  impl_->state={};
  auto e=impl_->encoder;[e setRenderPipelineState:found->second.pipeline];[e setDepthStencilState:found->second.depth];
  [e setViewport:MTLViewport{0,0,double(width),double(height),0,1}];[e setScissorRect:r];
  [e setCullMode:MTLCullModeNone];[e setFrontFacingWinding:MTLWindingCounterClockwise];
  [e setDepthBias:0 slopeScale:0 clamp:0];[e setDepthClipMode:MTLDepthClipModeClip];[e setTriangleFillMode:MTLTriangleFillModeFill];
  [e setStencilReferenceValue:clear.stencil_value];
  const std::array<float,8> data{clear.color[0],clear.color[1],clear.color[2],clear.color[3],clear.depth_value,0,0,0};
  if(clear.colors||clear.depth)[e setFragmentBytes:data.data() length:sizeof(data) atIndex:0];
  [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];return true;
}
bool Frame::CopyTexture(id<MTLTexture> source,id<MTLTexture> destination,MTLOrigin src,MTLOrigin dst,MTLSize size,std::string& error,bool combined_depth_stencil) {
  if(!*this||impl_->encoder||!source||!destination||source==destination||source.framebufferOnly||destination.framebufferOnly||
     source.textureType!=MTLTextureType2D||destination.textureType!=MTLTextureType2D||
     source.pixelFormat!=destination.pixelFormat||source.sampleCount!=1||destination.sampleCount!=1||
     (source.pixelFormat==MTLPixelFormatDepth32Float_Stencil8)!=combined_depth_stencil||
     src.z||dst.z||size.depth!=1||!size.width||!size.height||src.x>source.width||src.y>source.height||
     dst.x>destination.width||dst.y>destination.height||size.width>source.width-src.x||
     size.height>source.height-src.y||size.width>destination.width-dst.x||size.height>destination.height-dst.y)
    return Error(error,"Invalid Metal image copy or pass transition");
  auto e=[impl_->buffer blitCommandEncoder];if(!e)return Error(error,"Metal image-copy encoder allocation failed");
  e.label=@"Theft4 Ordered Texture Copy";
  [e copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:src sourceSize:size
      toTexture:destination destinationSlice:0 destinationLevel:0 destinationOrigin:dst];
  [e endEncoding];impl_->stored_color=destination;return true;
}
bool Frame::Present(id<CAMetalDrawable> drawable, std::string& error) {
  if (!*this || impl_->encoder || impl_->presented || !drawable ||
      drawable.texture != impl_->stored_color)
    return Error(error, "Metal presentation requires the stored drawable pass");
  [impl_->buffer presentDrawable:drawable]; impl_->presented = true; return true;
}
Receipt Frame::Submit(std::string& error) {
  Receipt result;
  if (!*this || impl_->encoder) { error = "Metal submission requires an ended pass"; return result; }
  auto lease = impl_->lease;
  [impl_->buffer addCompletedHandler:^(id<MTLCommandBuffer>) { (void)lease; }];
  result.buffer_ = impl_->buffer;
  result.counters_=impl_->counters;result.profiled_passes_=impl_->profiled_passes;result.pass_mapping_=std::move(impl_->pass_mapping);
  result.cpu_reference_=impl_->cpu_reference;result.gpu_reference_=impl_->gpu_reference;
  impl_->submitted = true; impl_->lease.reset();
  [impl_->buffer commit]; return result;
}
Receipt::operator bool() const { return buffer_ != nil; }
bool Receipt::Wait(std::string& error) const {
  if (!buffer_) return Error(error, "No Metal submission to wait for");
  [buffer_ waitUntilCompleted];
  if (buffer_.status != MTLCommandBufferStatusCompleted) { error = Description(buffer_.error); return false; }
  return true;
}
bool Receipt::Completed() const { return buffer_ && buffer_.status == MTLCommandBufferStatusCompleted; }
double Receipt::GpuMilliseconds() const { return Completed() ? (buffer_.GPUEndTime - buffer_.GPUStartTime) * 1000 : 0; }
std::vector<GpuPassTiming> Receipt::GpuPassTimings() const {
  std::vector<GpuPassTiming> result;
  if(!Completed()||!counters_||!profiled_passes_)return result;
  auto data=[counters_ resolveCounterRange:NSMakeRange(0,profiled_passes_*4)];
  if(!data||data.length!=profiled_passes_*4*sizeof(MTLCounterResultTimestamp))return result;
  MTLTimestamp cpu_end=0,gpu_end=0;
  [counters_.device sampleTimestamps:&cpu_end gpuTimestamp:&gpu_end];
  if(!cpu_reference_||!gpu_reference_||cpu_end<=cpu_reference_||gpu_end<=gpu_reference_)return result;
  // sampleTimestamps CPU values are nanoseconds, not mach_absolute_time ticks.
  const double scale=double(cpu_end-cpu_reference_)/double(gpu_end-gpu_reference_)/1e6;
  if(!std::isfinite(scale)||scale<=0)return result;
  const auto* timestamps=static_cast<const MTLCounterResultTimestamp*>(data.bytes);
  for(size_t i=0;i<profiled_passes_;++i) {
    const auto* t=timestamps+i*4;
    bool valid=true;for(size_t j=0;j<4;++j)valid&=t[j].timestamp&&t[j].timestamp!=MTLCounterErrorValue;
    if(!valid||t[1].timestamp<t[0].timestamp||t[3].timestamp<t[2].timestamp)continue;
    const double vertex=double(t[1].timestamp-t[0].timestamp)*scale,fragment=double(t[3].timestamp-t[2].timestamp)*scale;
    // Reject an inconsistent clock sample instead of reporting a render stage
    // longer than its entire command buffer. Allow measurement granularity.
    const auto envelope=GpuMilliseconds()*1.1+0.05;
    if(vertex>envelope||fragment>envelope)continue;
    result.push_back({pass_mapping_.empty()?i:pass_mapping_[i],vertex,fragment});
  }
  return result;
}
std::vector<uint8_t> Renderer::ReadRGBA8(id<MTLTexture> t, std::string& error,
    NSUInteger level, NSUInteger slice, NSUInteger depth_plane) {
  if(!t||(t.pixelFormat!=MTLPixelFormatRGBA8Unorm&&t.pixelFormat!=MTLPixelFormatBGRA8Unorm)) {
    error="Unsupported direct Metal RGBA8 readback";return {};
  }
  auto bytes=ReadColorBytes(t,error,level,slice,depth_plane);
  if(t.pixelFormat==MTLPixelFormatBGRA8Unorm)
    for(size_t i=0;i<bytes.size();i+=4)std::swap(bytes[i],bytes[i+2]);
  return bytes;
}
std::vector<uint8_t> Renderer::ReadColorBytes(id<MTLTexture> t,std::string& error,
    NSUInteger level,NSUInteger slice,NSUInteger depth_plane) {
  NSUInteger pixel_bytes=0;
  switch(t.pixelFormat) {
    case MTLPixelFormatR8Unorm:pixel_bytes=1;break;
    case MTLPixelFormatRG8Unorm:case MTLPixelFormatR16Unorm:case MTLPixelFormatR16Float:pixel_bytes=2;break;
    case MTLPixelFormatRGBA8Unorm:case MTLPixelFormatRGBA8Unorm_sRGB:
    case MTLPixelFormatBGRA8Unorm:case MTLPixelFormatBGRA8Unorm_sRGB:
    case MTLPixelFormatRG16Unorm:case MTLPixelFormatRG16Float:
    case MTLPixelFormatR32Float:case MTLPixelFormatRGB10A2Unorm:pixel_bytes=4;break;
    case MTLPixelFormatRGBA16Unorm:case MTLPixelFormatRGBA16Float:case MTLPixelFormatRG32Float:pixel_bytes=8;break;
    case MTLPixelFormatRGBA32Float:pixel_bytes=16;break;
    default:break;
  }
  const auto type=t.textureType;
  const bool cube=type==MTLTextureTypeCube || type==MTLTextureTypeCubeArray;
  const bool array=type==MTLTextureType2DArray || type==MTLTextureTypeCubeArray;
  const bool volume=type==MTLTextureType3D;
  const NSUInteger slices=cube ? t.arrayLength*6 : array ? t.arrayLength : 1;
  if (!Ready() || !t || t.framebufferOnly || (!cube && !array && !volume && type!=MTLTextureType2D) || t.sampleCount != 1 ||
      !pixel_bytes ||
      t.width > 16384 || t.height > 16384 ||
      level>=t.mipmapLevelCount || slice>=slices ||
      depth_plane>=(volume ? std::max(NSUInteger(1),t.depth>>level) : 1)) {
    error = "Unsupported direct Metal validation readback"; return {};
  }
  const NSUInteger width=std::max(NSUInteger(1),t.width>>level),height=std::max(NSUInteger(1),t.height>>level);
  const NSUInteger stride = (width * pixel_bytes + 255) & ~NSUInteger(255);
  if(stride*height>64*1024*1024){error="Metal color readback exceeds its bounded transfer budget";return {};}
  auto out = [Device() newBufferWithLength:stride * height options:MTLResourceStorageModeShared];
  auto command = [impl_->queue commandBuffer];
  if (!out || !command) { error = "Metal readback allocation failed"; return {}; }
  auto blit = [command blitCommandEncoder];
  if (!blit) { error = "Metal readback encoder failed"; return {}; }
  [blit copyFromTexture:t sourceSlice:slice sourceLevel:level sourceOrigin:MTLOriginMake(0,0,depth_plane)
      sourceSize:MTLSizeMake(width,height,1) toBuffer:out destinationOffset:0
      destinationBytesPerRow:stride destinationBytesPerImage:stride*height];
  [blit endEncoding]; [command commit]; [command waitUntilCompleted];
  if (command.status != MTLCommandBufferStatusCompleted) { error = Description(command.error); return {}; }
  std::vector<uint8_t> bytes(width * height * pixel_bytes);
  for (NSUInteger y = 0; y < height; ++y)
    memcpy(bytes.data() + y * width * pixel_bytes, static_cast<const uint8_t*>(out.contents) + y * stride, width * pixel_bytes);
  error.clear();
  return bytes;
}
}
