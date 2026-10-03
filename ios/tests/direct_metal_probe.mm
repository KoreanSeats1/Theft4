#include "direct_metal_probe.h"
#include "theft4_native_metal.h"
#include "native_color_output.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

using namespace theft4::metal;
namespace {
constexpr NSUInteger W = 64, H = 64;
struct Vertex { float position[4], color[4], uv[4]; };
struct Case {
  const char* name;
  bool texture = false, indexed16 = false, indexed32 = false, depth = false;
  bool blend = false, alpha_test = false, output_scale = false, multisample = false;
  bool alpha_coverage = false, astc = false, two_pass = false;
  bool transform = false, pixel_constants = false;
};
template<class T> std::span<const uint8_t> Bytes(const std::vector<T>& v) {
  return {reinterpret_cast<const uint8_t*>(v.data()), v.size() * sizeof(T)};
}
struct Probe {
  Renderer renderer{1};
  NSString* libraries;
  NSString* output;
  std::string error;
  NSMutableArray* results = [NSMutableArray new];
  void Require(bool truth) {
    if (!truth) throw std::runtime_error(error.empty() ? "Metal validation assertion" : error);
  }
  Shader ShaderFor(const char* key, bool vertex, bool textured, uint32_t spec = 0) {
    NSString* path = [libraries stringByAppendingPathComponent:
        [[NSString stringWithUTF8String:key] stringByAppendingString:@".metallib"]];
    NSData* data = [NSData dataWithContentsOfFile:path];
    if (!data) throw std::runtime_error(std::string("Missing game library: ") + key);
    ShaderInterface abi{};
    if (textured) { abi.textures = 1; abi.samplers = 1; abi.texture_types[0] = MTLTextureType2D; }
    auto shader = renderer.LoadShader({static_cast<const uint8_t*>(data.bytes), data.length},
        vertex ? Stage::Vertex : Stage::Fragment, abi, spec, error);
    Require(shader.function); return shader;
  }
  id<MTLTexture> Color(NSUInteger samples = 1) {
    auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:W height:H mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    if (samples > 1) { d.textureType = MTLTextureType2DMultisample; d.sampleCount = samples; }
    auto result = renderer.Texture(d, error); Require(result); return result;
  }
  id<MTLTexture> SourceTexture(bool astc) {
    auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
        astc ? MTLPixelFormatASTC_4x4_LDR : MTLPixelFormatRGBA8Unorm
        width:astc ? 4 : 2 height:astc ? 4 : 2 mipmapped:NO];
    d.storageMode = MTLStorageModeShared; d.usage = MTLTextureUsageShaderRead;
    auto texture = renderer.Texture(d, error); Require(texture);
    if (astc) {
      // ASTC LDR void-extent block, UNORM16 channels (51,102,204,255)/255.
      const uint8_t block[]{0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff,
                            0x33,0x33,0x66,0x66,0xcc,0xcc,0xff,0xff};
      [texture replaceRegion:MTLRegionMake2D(0,0,4,4) mipmapLevel:0
          withBytes:block bytesPerRow:16];
    } else {
      const uint8_t pixels[]{255,0,0,64, 0,255,0,128, 0,0,255,192, 255,255,0,255};
      [texture replaceRegion:MTLRegionMake2D(0,0,2,2) mipmapLevel:0
          withBytes:pixels bytesPerRow:8];
    }
    return texture;
  }
  std::vector<Vertex> Quad(std::array<float,4> c, float z = 0.5f, bool indexed = false) {
    const Vertex q[]{ {{-1,-1,z,1}, {c[0],c[1],c[2],c[3]}, {0,1,0,0}},
                      {{ 1,-1,z,1}, {c[0],c[1],c[2],c[3]}, {1,1,0,0}},
                      {{ 1, 1,z,1}, {c[0],c[1],c[2],c[3]}, {1,0,0,0}},
                      {{-1, 1,z,1}, {c[0],c[1],c[2],c[3]}, {0,0,0,0}} };
    if (indexed) return {q[0],q[1],q[2],q[3]};
    return {q[0],q[1],q[2],q[0],q[2],q[3]};
  }
  BufferView Buffer(std::span<const uint8_t> bytes, bool offset = false) {
    std::vector<uint8_t> storage(offset ? 16 : 0, 0xab);
    storage.insert(storage.end(), bytes.begin(), bytes.end());
    auto buffer = renderer.ImmutableBuffer(storage,error); Require(buffer);
    return {buffer, offset ? 16u : 0u, bytes.size()};
  }
  std::array<BufferView,3> Constants(const Case& c, NSUInteger samples) {
    std::vector<uint8_t> vs(4096),ps(3584),shared(1056);
    // Deliberately invalid Vulkan descriptor indices. Direct Metal must use
    // its compiled slot map and never consult these old table fields.
    std::fill(shared.begin(),shared.begin()+520,0xff);
    if(c.transform){
      const float matrix[]{0.5f,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      memcpy(vs.data()+3328,matrix,sizeof(matrix));
    }
    if(c.pixel_constants){
      const float value[]{1,0.5f,0,0},alpha[]{0.5f,0,0,0};
      memcpy(ps.data()+624,value,sizeof(value));memcpy(ps.data()+736,alpha,sizeof(alpha));
    }
    const auto f = [&](size_t offset, float value) { memcpy(shared.data()+offset,&value,4); };
    const auto u = [&](size_t offset, uint32_t value) { memcpy(shared.data()+offset,&value,4); };
    f(744,1); f(748,1); f(580,0.75f);
    u(736,c.alpha_coverage ? 0x100 : 0); u(740,uint32_t(samples));
    for (uint32_t target = 0; target < 4; ++target) {
      rex::graphics::gta4_native::NativeColorOutputParameters p{};
      p.minimum.fill(0);p.maximum.fill(1);
      if (c.output_scale) p.scale = {2,2,2,0.5f};
      memcpy(shared.data()+0x360+target*sizeof(p),&p,sizeof(p));
    }
    return {Buffer(vs,true),Buffer(ps,true),Buffer(shared,true)};
  }
  std::shared_ptr<const Pipeline> PipelineFor(const Case& c, NSUInteger samples,
      MTLPixelFormat format=MTLPixelFormatRGBA8Unorm) {
    auto vs = ShaderFor(c.transform ? "2668e8f9bb250542" : "048e49996734f6b5",true,false);
    const uint32_t spec = c.alpha_test ? (2u | (6u << 8)) : 0;
    auto ps = ShaderFor(c.pixel_constants ? "d72b5cf469e02c54" : c.texture ? "b9589da9f4b1770f" :
        c.alpha_test ? "949ed69300fb92b7-late" : "949ed69300fb92b7",false,c.texture,spec);
    auto fixed = [MTLRenderPipelineDescriptor new];
    fixed.rasterSampleCount=samples; fixed.colorAttachments[0].pixelFormat=format;
    if(c.blend){
      auto b=fixed.colorAttachments[0]; b.blendingEnabled=YES;
      b.sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;
      b.destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
      b.sourceAlphaBlendFactor=MTLBlendFactorOne;
      b.destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    }
    if(c.depth)fixed.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;
    auto vertex=[MTLVertexDescriptor new];
    for(auto [location,offset]: {std::pair{0u,0u}, {17u,16u}, {13u,32u}}){
      vertex.attributes[location].format=MTLVertexFormatFloat4;
      vertex.attributes[location].offset=offset;vertex.attributes[location].bufferIndex=8;
    }
    vertex.layouts[8].stride=sizeof(Vertex);vertex.layouts[8].stepRate=1;
    vertex.layouts[8].stepFunction=MTLVertexStepFunctionPerVertex;
    fixed.vertexDescriptor=vertex;
    auto depth=[MTLDepthStencilDescriptor new];
    depth.depthCompareFunction=c.depth ? MTLCompareFunctionLess : MTLCompareFunctionAlways;
    depth.depthWriteEnabled=c.depth;
    auto pipeline=renderer.MakePipeline(vs,ps,fixed,depth,error);Require(bool(pipeline));return pipeline;
  }
  Draw Packet(const Case& c, std::shared_ptr<const Pipeline> pipeline,
      std::array<float,4> color, float z=0.5f) {
    Draw d;d.pipeline=pipeline;d.constants=Constants(c,pipeline->samples);
    auto vertices=Quad(color,z,c.indexed16||c.indexed32);
    if(c.indexed32){vertices.insert(vertices.begin(),vertices.front());d.base_vertex=1;}
    d.vertices[0]=Buffer(Bytes(vertices),true);d.vertex_count=vertices.size();
    if(c.indexed16){
      std::vector<uint16_t> index{0,1,2,0,2,3};d.indices=Buffer(Bytes(index),true);
      d.index_type=MTLIndexTypeUInt16;d.index_count=6;d.maximum_vertex=3;
    }
    if(c.indexed32){
      std::vector<uint32_t> index{0,1,2,0,2,3};d.indices=Buffer(Bytes(index),true);
      d.index_type=MTLIndexTypeUInt32;d.index_count=6;d.maximum_vertex=4;
    }
    if(c.texture){
      d.textures.push_back({Stage::Fragment,0,SourceTexture(c.astc)});
      auto s=[MTLSamplerDescriptor new];s.minFilter=MTLSamplerMinMagFilterNearest;
      s.magFilter=MTLSamplerMinMagFilterNearest;s.sAddressMode=MTLSamplerAddressModeClampToEdge;
      s.tAddressMode=MTLSamplerAddressModeClampToEdge;
      auto sampler=[renderer.Device() newSamplerStateWithDescriptor:s];Require(sampler);
      d.samplers.push_back({Stage::Fragment,0,sampler});
    }
    d.viewport={0,0,double(W),double(H),0,1};d.scissor={0,0,W,H};return d;
  }
  MTLRenderPassDescriptor* Pass(id<MTLTexture> target, id<MTLTexture> resolve,
      id<MTLTexture> depth, bool load=false) {
    auto p=[MTLRenderPassDescriptor renderPassDescriptor];
    p.colorAttachments[0].texture=target;
    p.colorAttachments[0].loadAction=load ? MTLLoadActionLoad : MTLLoadActionClear;
    p.colorAttachments[0].storeAction=resolve ? MTLStoreActionMultisampleResolve : MTLStoreActionStore;
    p.colorAttachments[0].resolveTexture=resolve;
    p.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
    if(depth){p.depthAttachment.texture=depth;p.depthAttachment.loadAction=MTLLoadActionClear;
      p.depthAttachment.storeAction=MTLStoreActionDontCare;p.depthAttachment.clearDepth=1;}
    return p;
  }
  void Run(const Case& c) {
    NSUInteger samples=1;
    if(c.multisample||c.alpha_coverage){
      if(![renderer.Device() supportsTextureSampleCount:4])throw std::runtime_error("Required four-sample validation is unsupported");
      samples=4;
    }
    auto target=Color(samples);auto resolve=samples>1 ? Color() : nil;
    id<MTLTexture> depth=nil;
    if(c.depth){
      auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
          width:W height:H mipmapped:NO];
      desc.storageMode=MTLStorageModePrivate;desc.usage=MTLTextureUsageRenderTarget;
      depth=renderer.Texture(desc,error);Require(depth);
    }
    std::array<float,4> tint = c.texture ? std::array<float,4>{0.5f,0.25f,1,0.75f}
                                       : std::array<float,4>{0.25f,0.5f,0.75f,1};
    if(c.alpha_test)tint={1,0,0,0.5f};
    if(c.depth)tint={1,0,0,1};
    if(c.blend)tint={0,0,1,1};
    if(c.output_scale)tint={0.75f,0.5f,0.25f,1};
    if(c.alpha_coverage)tint={1,1,1,0.5f};
    auto pipeline=PipelineFor(c,samples);auto draw=Packet(c,pipeline,tint,c.depth ? 0.25f : 0.5f);
    auto frame=renderer.BeginFrame(error);Require(bool(frame));
    Require(frame.BeginPass(Pass(target,resolve,depth),error));Require(frame.Encode(draw,error));
    if(c.depth){auto far=Packet(c,pipeline,{0,0,1,1},0.75f);Require(frame.Encode(far,error));}
    Require(frame.EndPass(error));
    if(c.blend){auto overlay=Packet(c,pipeline,{1,0,0,0.5f});
      Require(frame.BeginPass(Pass(target,nil,nil,true),error));Require(frame.Encode(overlay,error));
      Require(frame.EndPass(error));}
    if(c.two_pass){
      Case sampled{};sampled.texture=true;
      auto second=Color();auto sampled_pipeline=PipelineFor(sampled,1);
      auto copied=Packet(sampled,sampled_pipeline,{1,1,1,1});
      copied.textures[0].texture=target;
      Require(frame.BeginPass(Pass(second,nil,nil),error));Require(frame.Encode(copied,error));
      Require(frame.EndPass(error));target=second;
    }
    // CPU draw/resource owners may retire immediately after submission. Metal
    // command buffers retain their resources until the GPU has finished.
    auto receipt=frame.Submit(error);Require(bool(receipt));draw={};pipeline.reset();
    Require(receipt.Wait(error));auto pixels=renderer.ReadRGBA8(resolve ? resolve : target,error);
    Require(pixels.size()==W*H*4);
    NSUInteger wrong=0,max_error=0;
    const uint8_t texture_pixels[4][4]{{255,0,0,64},{0,255,0,128},{0,0,255,192},{255,255,0,255}};
    for(NSUInteger y=0;y<H;++y)for(NSUInteger x=0;x<W;++x){
      std::array<float,4> expected=tint;
      if(c.texture){
        for(size_t k=0;k<4;++k)expected[k]*=(c.astc ? std::array<float,4>{0.2f,0.4f,0.8f,1}[k]
            : texture_pixels[(y>=H/2)*2+(x>=W/2)][k]/255.0f);
      }
      if(c.pixel_constants)for(float& value:expected)value*=0.5f;
      if(c.alpha_test)expected={0,0,0,1};
      if(c.blend)expected={0.5f,0,0.5f,1};
      if(c.output_scale)expected={1,1,0.5f,0.5f};
      if(c.alpha_coverage)expected={0.5f,0.5f,0.5f,0.75f};
      if(c.transform&&(x<W/4||x>=W*3/4))expected={0,0,0,1};
      for(size_t k=0;k<4;++k){
        int wanted=int(std::lround(std::clamp(expected[k],0.0f,1.0f)*255));
        NSUInteger difference=NSUInteger(std::abs(int(pixels[(y*W+x)*4+k])-wanted));
        max_error=std::max(max_error,difference);wrong+=difference>1;
      }
    }
    [[NSData dataWithBytes:pixels.data() length:pixels.size()] writeToFile:
        [output stringByAppendingPathComponent:[[NSString stringWithUTF8String:c.name] stringByAppendingString:@".rgba"]]
        atomically:YES];
    [results addObject:@{@"case":[NSString stringWithUTF8String:c.name],@"passed":@(wrong==0),
      @"wrong_channels":@(wrong),@"max_byte_error":@(max_error),@"gpu_ms":@(receipt.GpuMilliseconds())}];
    if(wrong)throw std::runtime_error(std::string(c.name)+" differs from the CPU pixel oracle");
  }
  void AdmissionAndLifetime() {
    {auto held=renderer.BeginFrame(error);Require(bool(held));
      auto blocked=renderer.BeginFrame(error);Require(!blocked);}
    error.clear();auto frame=renderer.BeginFrame(error);Require(bool(frame));auto target=Color();
    Require(frame.BeginPass(Pass(target,nil,nil),error));
    Case c{};c.texture=true;auto p=PipelineFor(c,1);auto good=Packet(c,p,{1,1,1,1});
    auto bad=good;bad.textures.clear();Require(!frame.Encode(bad,error));
    bad=good;bad.constants[2].length=1000;Require(!frame.Encode(bad,error));
    bad=good;bad.vertices[0].length=1;Require(!frame.Encode(bad,error));
    bad=good;bad.textures.push_back(bad.textures.front());Require(!frame.Encode(bad,error));
    bad=good;bad.samplers[0].index=16;Require(!frame.Encode(bad,error));
    bad=good;bad.scissor.width=W+1;Require(!frame.Encode(bad,error));
    Require(!frame.Submit(error));
    error.clear();Require(frame.Encode(good,error));Require(frame.EndPass(error));
    auto receipt=frame.Submit(error);Require(bool(receipt));Require(!frame.Submit(error));
    error.clear();good={};bad={};p.reset();Require(receipt.Wait(error));
    frame={};auto next=renderer.BeginFrame(error);Require(bool(next));
    [results addObject:@{@"case":@"admission_and_gpu_lifetime",@"passed":@YES,
      @"rejected_packets":@6,@"aborted_slot_reclaimed":@YES,@"completed_slot_reclaimed":@YES}];
    auto bc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBC3_RGBA
        width:4 height:4 mipmapped:NO];bc.storageMode=MTLStorageModeShared;
    auto bc_texture=renderer.Texture(bc,error);
    Require(bool(bc_texture)==bool(renderer.Device().supportsBCTextureCompression));
    [results addObject:@{@"case":@"bc_capability_gate",@"passed":@YES,
      @"bc_supported":@(renderer.Device().supportsBCTextureCompression)}];
  }
};
}
NSDictionary* RunDirectMetalValidation(NSString* libraries, NSString* output) {
  [[NSFileManager defaultManager] createDirectoryAtPath:output withIntermediateDirectories:YES attributes:nil error:nil];
  Probe probe;probe.libraries=libraries;probe.output=output;
  bool passed=false;NSString* failure=@"";
  try {
    probe.Require(probe.renderer.Ready());
    const Case cases[]{
      {.name="vertex_color"},
      {.name="textured_tint",.texture=true},
      {.name="indexed16_offsets",.indexed16=true},
      {.name="indexed32_base_vertex",.indexed32=true},
      {.name="depth_occlusion",.depth=true},
      {.name="alpha_blending",.blend=true},
      {.name="late_alpha_discard",.alpha_test=true},
      {.name="native_output_contract",.alpha_test=true,.output_scale=true},
      {.name="msaa_resolve",.multisample=true},
      {.name="xenos_alpha_coverage",.alpha_coverage=true},
      {.name="prepared_astc_texture",.texture=true,.astc=true},
      {.name="render_target_sampling",.two_pass=true}
      ,{.name="vertex_constant_bank",.transform=true}
      ,{.name="pixel_constant_bank",.texture=true,.pixel_constants=true}
    };
    for(const auto& c:cases)probe.Run(c);
    probe.AdmissionAndLifetime();passed=true;
  } catch(const std::exception& error){failure=[NSString stringWithUTF8String:error.what()];}
  auto device=probe.renderer.Device();
  NSDictionary* report=@{@"schema":@1,@"passed":@(passed),@"failure":failure,@"cases":probe.results,
    @"device":device ? device.name : @"No Metal device",@"os":NSProcessInfo.processInfo.operatingSystemVersionString,
    @"supports_bc":@(device.supportsBCTextureCompression),@"apple7":@([device supportsFamily:MTLGPUFamilyApple7]),
    @"vulkan_linked":@NO,@"moltenvk_linked":@NO,@"game_started":@NO,
    @"shaders":@[@"gta_im_vs3",@"gta_im_vs4",@"gta_im_ps1",@"gta_im_ps2",@"gta_im_ps3",@"gta_im_ps3_late"],
    @"pixel_count_per_case":@(W*H),@"tolerance_unorm_bytes":@1};
  NSData* json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
  [json writeToFile:[output stringByAppendingPathComponent:@"DIRECT_METAL_VALIDATION.json"] atomically:YES];
  return report;
}
bool PresentDirectMetalPreview(CAMetalLayer* layer, NSString* libraries, NSString** failure) {
  @autoreleasepool {
    Probe probe;probe.libraries=libraries;
    try {
      probe.Require(layer && probe.renderer.Ready());
      layer.device=probe.renderer.Device();layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
      layer.framebufferOnly=YES;layer.drawableSize=CGSizeMake(256,256);
      id<CAMetalDrawable> drawable=[layer nextDrawable];probe.Require(drawable);
      Case c{};c.texture=true;
      auto p=probe.PipelineFor(c,1,MTLPixelFormatBGRA8Unorm);
      auto d=probe.Packet(c,p,{1,1,1,1});
      d.viewport={0,0,double(drawable.texture.width),double(drawable.texture.height),0,1};
      d.scissor={0,0,drawable.texture.width,drawable.texture.height};
      auto f=probe.renderer.BeginFrame(probe.error);probe.Require(bool(f));
      probe.Require(f.BeginPass(probe.Pass(drawable.texture,nil,nil),probe.error));
      probe.Require(f.Encode(d,probe.error));probe.Require(f.EndPass(probe.error));
      probe.Require(f.Present(drawable,probe.error));
      auto receipt=f.Submit(probe.error);probe.Require(bool(receipt));
      probe.Require(receipt.Wait(probe.error));return true;
    } catch(const std::exception& error){
      if(failure)*failure=[NSString stringWithUTF8String:error.what()];return false;
    }
  }
}
