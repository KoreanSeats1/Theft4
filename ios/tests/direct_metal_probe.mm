#include "direct_metal_probe.h"
#include "theft4_native_metal.h"
#include "theft4_metal_shader_store.h"
#include "theft4_metal_resources.h"
#include "theft4_metal_plan.h"
#include "theft4_metal_frame.h"
#include "theft4_metal_host_shaders.h"
#include "present_constants.h"
#include "direct_metal_capture.h"
#include "native_color_output.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifndef THEFT4_METAL_SOURCE_REVISION
#define THEFT4_METAL_SOURCE_REVISION "development"
#endif

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
  ShaderStore shaders{renderer};
  NSString* libraries;
  NSString* output;
  std::string error;
  NSMutableArray* results = [NSMutableArray new];
  void Require(bool truth) {
    if (!truth) throw std::runtime_error(error.empty() ? "Metal validation assertion" : error);
  }
  Shader ShaderFor(ShaderKey key, Stage stage, uint32_t spec = 0) {
    if (!shaders.CatalogSize()) Require(shaders.Open(libraries.UTF8String,error));
    auto shader = shaders.Resolve(key,stage,spec,error);
    Require(shader.function); return shader;
  }
  ShaderKey FragmentFor(const Case& c) {
    return {c.pixel_constants ? 0xD72B5CF469E02C54ull : c.texture ? 0xB9589DA9F4B1770Full :
        0x949ED69300FB92B7ull,c.alpha_test};
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
  MTLVertexDescriptor* VertexDeclaration(bool instance_color=false) {
    auto vertex=[MTLVertexDescriptor new];
    for(auto [location,offset]: {std::pair{0u,0u}, {17u,16u}, {13u,32u}}){
      vertex.attributes[location].format=MTLVertexFormatFloat4;
      vertex.attributes[location].offset=offset;vertex.attributes[location].bufferIndex=8;
    }
    vertex.layouts[8].stride=sizeof(Vertex);vertex.layouts[8].stepRate=1;
    vertex.layouts[8].stepFunction=MTLVertexStepFunctionPerVertex;
    if(instance_color){
      vertex.attributes[17].offset=0;vertex.attributes[17].bufferIndex=24;
      vertex.layouts[24].stride=16;vertex.layouts[24].stepRate=1;
      vertex.layouts[24].stepFunction=MTLVertexStepFunctionPerInstance;
    }
    return vertex;
  }
  std::shared_ptr<const Pipeline> PipelineFor(const Case& c, NSUInteger samples,
      MTLPixelFormat format=MTLPixelFormatRGBA8Unorm,
      MTLCompareFunction depth_function=MTLCompareFunctionLess) {
    auto vs = ShaderFor({c.transform ? 0x2668E8F9BB250542ull : 0x048E49996734F6B5ull,false},Stage::Vertex);
    const uint32_t spec = c.alpha_test ? (2u | (6u << 8)) : 0;
    auto ps = ShaderFor(FragmentFor(c),Stage::Fragment,spec);
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
    fixed.vertexDescriptor=VertexDeclaration();
    auto depth=[MTLDepthStencilDescriptor new];
    depth.depthCompareFunction=c.depth ? depth_function : MTLCompareFunctionAlways;
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
    std::array<FetchResources,26> fetches{};
    if(c.texture){
      fetches[0].images[0]=SourceTexture(c.astc);
      auto s=[MTLSamplerDescriptor new];s.minFilter=MTLSamplerMinMagFilterNearest;
      s.magFilter=MTLSamplerMinMagFilterNearest;s.sAddressMode=MTLSamplerAddressModeClampToEdge;
      s.tAddressMode=MTLSamplerAddressModeClampToEdge;
      auto sampler=[renderer.Device() newSamplerStateWithDescriptor:s];Require(sampler);
      fetches[0].sampler=sampler;
    }
    Require(shaders.Bind(FragmentFor(c),Stage::Fragment,fetches,d,error));
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
  void CatalogAndCache() {
    const auto loaded=shaders.LoadedFunctions();
    auto first=ShaderFor({0x048E49996734F6B5ull,false},Stage::Vertex);
    auto reused=ShaderFor({0x048E49996734F6B5ull,false},Stage::Vertex,UINT32_MAX);
    Require(first.function==reused.function && shaders.LoadedFunctions()==loaded);
    auto alpha=ShaderFor({0x949ED69300FB92B7ull,true},Stage::Fragment,0x602u);
    auto alpha_reused=ShaderFor({0x949ED69300FB92B7ull,true},Stage::Fragment,0x80000602u);
    Require(alpha.function==alpha_reused.function && shaders.LoadedFunctions()==loaded);
    auto wrong=shaders.Resolve({0x048E49996734F6B5ull,false},Stage::Fragment,0,error);
    Require(!wrong.function && shaders.LoadedFunctions()==loaded);
    auto missing=shaders.Resolve({0x1111111111111111ull,false},Stage::Vertex,0,error);
    Require(!missing.function && shaders.LoadedFunctions()==loaded);
    const ShaderKey sparse{0xECA7E919FECAAD3Cull,false};
    auto sparse_shader=ShaderFor(sparse,Stage::Fragment);
    Require(sparse_shader.interface.textures==3 && sparse_shader.interface.samplers==3);
    std::array<FetchResources,26> fetches{};
    fetches[0].images[0]=SourceTexture(false);fetches[15].images[0]=SourceTexture(true);
    auto sampler=[renderer.Device() newSamplerStateWithDescriptor:[MTLSamplerDescriptor new]];
    Require(sampler);fetches[0].sampler=sampler;
    Draw packet;Require(!shaders.Bind(sparse,Stage::Fragment,fetches,packet,error));
    Require(packet.textures.empty() && packet.samplers.empty());
    fetches[15].sampler=sampler;
    Require(shaders.Bind(sparse,Stage::Fragment,fetches,packet,error));
    Require(packet.textures.size()==2 && packet.samplers.size()==2 &&
        packet.textures[0].index==0 && packet.textures[0].texture==fetches[0].images[0] &&
        packet.textures[1].index==1 && packet.textures[1].texture==fetches[15].images[0]);
    Require(!shaders.Bind(sparse,Stage::Fragment,fetches,packet,error));
    Require(packet.textures.size()==2 && packet.samplers.size()==2);
    error.clear();
    [results addObject:@{@"case":@"game_shader_catalog_and_cache",@"passed":@YES,
        @"catalog_entries":@(shaders.CatalogSize()),@"functions_loaded":@(shaders.LoadedFunctions()),
        @"sparse_fetch_slots":@[@0,@15],@"metal_indices":@[@0,@1],
        @"specialization_cache_reused":@YES,@"failed_binding_transaction_retained":@YES}];
  }
  void ResourceGenerations() {
    ResourceCache cache(renderer);
    auto vertices=std::make_shared<const std::vector<Vertex>>(Quad({1,1,1,1}));
    ResourceVersion buffer_version{vertices,1,{}};
    id<MTLBuffer> vertex_buffer=cache.Buffer(buffer_version,Bytes(*vertices),error);Require(vertex_buffer);
    Require(cache.Buffer(buffer_version,Bytes(*vertices),error)==vertex_buffer);
    Require(!cache.Buffer(buffer_version,Bytes(*vertices).first(1),error));
    {auto next=buffer_version;next.generation=2;
      Require(cache.Buffer(next,Bytes(*vertices),error)!=vertex_buffer);}
    {auto converted=buffer_version;converted.conversion[0]=1;
      Require(cache.Buffer(converted,Bytes(*vertices),error)!=vertex_buffer);}
    const std::array<std::array<uint8_t,4>,4> colors{{{51,102,204,255},{204,51,102,255},
        {102,204,51,255},{255,255,255,255}}};
    auto rgba=std::make_shared<std::vector<uint8_t>>();std::vector<TextureUpload> rgba_uploads;
    for(NSUInteger level=0;level<4;++level){
      const NSUInteger size=8>>level,pitch=size*4+4;const size_t start=rgba->size();
      rgba->resize(start+pitch*size,0xde);
      for(NSUInteger y=0;y<size;++y)for(NSUInteger x=0;x<size;++x)
        memcpy(rgba->data()+start+y*pitch+x*4,colors[level].data(),4);
      rgba_uploads.push_back({level,0,size,size,1,pitch,0,start,pitch*size});
    }
    auto rgba_desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:8 height:8 mipmapped:YES];rgba_desc.storageMode=MTLStorageModeShared;
    rgba_desc.usage=MTLTextureUsageShaderRead;ResourceVersion rgba_version{rgba,1,{}};
    id<MTLTexture> rgba_texture=cache.Texture(rgba_version,rgba_desc,*rgba,rgba_uploads,error);Require(rgba_texture);
    Require(cache.Texture(rgba_version,rgba_desc,*rgba,rgba_uploads,error)==rgba_texture);
    MTLTextureDescriptor* wrong_shape=[rgba_desc copy];wrong_shape.pixelFormat=MTLPixelFormatRGBA8Unorm_sRGB;
    Require(!cache.Texture(rgba_version,wrong_shape,*rgba,rgba_uploads,error));
    {auto replacement=rgba_version;replacement.generation=2;
      auto bad=rgba_uploads;bad[0].offset=rgba->size();
      Require(!cache.Texture(replacement,rgba_desc,*rgba,bad,error));
      bad=rgba_uploads;bad[1]=bad[0];Require(!cache.Texture(replacement,rgba_desc,*rgba,bad,error));
      Require(!cache.Texture(replacement,rgba_desc,*rgba,std::span(rgba_uploads).first(3),error));
      MTLTextureDescriptor* renderable=[rgba_desc copy];renderable.usage=MTLTextureUsageRenderTarget;
      Require(!cache.Texture(replacement,renderable,*rgba,rgba_uploads,error));}
    auto astc=std::make_shared<std::vector<uint8_t>>();std::vector<TextureUpload> astc_uploads;
    for(NSUInteger level=0;level<3;++level){
      const size_t start=astc->size();
      const uint8_t extent[]{0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff};
      astc->insert(astc->end(),std::begin(extent),std::end(extent));
      for(uint8_t channel:colors[level]){astc->push_back(channel);astc->push_back(channel);}
      astc_uploads.push_back({level,0,4u>>level,4u>>level,1,16,0,start,16});
    }
    auto astc_desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatASTC_4x4_LDR
        width:4 height:4 mipmapped:YES];astc_desc.storageMode=MTLStorageModeShared;
    astc_desc.usage=MTLTextureUsageShaderRead;ResourceVersion astc_version{astc,1,{}};
    auto astc_texture=cache.Texture(astc_version,astc_desc,*astc,astc_uploads,error);Require(astc_texture);
    // Upload all faces/layers/volume planes, then inspect them through GPU blits.
    std::vector<std::shared_ptr<std::vector<uint8_t>>> dimensional_owners;
    size_t dimensional_subresources=0;
    for(MTLTextureType type:{MTLTextureTypeCube,MTLTextureType2DArray,MTLTextureType3D}){
      auto data=std::make_shared<std::vector<uint8_t>>();std::vector<TextureUpload> uploads;
      const NSUInteger slices=type==MTLTextureTypeCube ? 6 : type==MTLTextureType2DArray ? 2 : 1;
      for(NSUInteger slice=0;slice<slices;++slice)for(NSUInteger level=0;level<2;++level){
        const NSUInteger width=4>>level,depth=type==MTLTextureType3D ? 2>>level : 1;
        const size_t start=data->size();
        for(NSUInteger z=0;z<depth;++z)for(NSUInteger pixel=0;pixel<width*width;++pixel){
          const uint8_t color[]{uint8_t(20+slice*10+level*3+z),40,60,255};
          data->insert(data->end(),std::begin(color),std::end(color));
        }
        uploads.push_back({level,slice,width,width,depth,width*4,
            type==MTLTextureType3D ? width*width*4 : 0,start,data->size()-start});
      }
      auto desc=[MTLTextureDescriptor new];desc.pixelFormat=MTLPixelFormatRGBA8Unorm;
      desc.textureType=type;desc.width=desc.height=4;desc.depth=type==MTLTextureType3D ? 2 : 1;
      desc.arrayLength=type==MTLTextureType2DArray ? 2 : 1;desc.mipmapLevelCount=2;
      desc.storageMode=MTLStorageModeShared;desc.usage=MTLTextureUsageShaderRead;
      auto texture=cache.Texture({data,1,{}},desc,*data,uploads,error);Require(texture);
      for(const auto& u:uploads)for(NSUInteger z=0;z<u.depth;++z){
        auto pixels=renderer.ReadRGBA8(texture,error,u.level,u.slice,z);
        Require(pixels.size()==u.width*u.height*4);
        const uint8_t color[]{uint8_t(20+u.slice*10+u.level*3+z),40,60,255};
        for(size_t byte=0;byte<pixels.size();++byte)Require(pixels[byte]==color[byte%4]);
        ++dimensional_subresources;
      }
      dimensional_owners.push_back(data);
    }
    Case sampled{};sampled.texture=true;auto pipeline=PipelineFor(sampled,1);
    auto render=[&](id<MTLTexture> texture,NSUInteger level,bool retire){
      auto target=Color();auto draw=Packet(sampled,pipeline,{1,1,1,1});
      draw.vertices[0]={vertex_buffer,0,Bytes(*vertices).size()};draw.textures[0].texture=texture;
      auto sampler=[MTLSamplerDescriptor new];sampler.minFilter=MTLSamplerMinMagFilterNearest;
      sampler.magFilter=MTLSamplerMinMagFilterNearest;sampler.mipFilter=MTLSamplerMipFilterNearest;
      sampler.lodMinClamp=sampler.lodMaxClamp=float(level);
      draw.samplers[0].sampler=[renderer.Device() newSamplerStateWithDescriptor:sampler];
      error.clear();auto frame=renderer.BeginFrame(error);Require(bool(frame));
      Require(frame.BeginPass(Pass(target,nil,nil),error));Require(frame.Encode(draw,error));Require(frame.EndPass(error));
      auto receipt=frame.Submit(error);Require(bool(receipt));draw={};
      if(retire){
        vertices.reset();buffer_version={};rgba.reset();rgba_version={};vertex_buffer=nil;rgba_texture=nil;
        texture=nil;
        Require(cache.SweepRetired()==4 && cache.BufferCount()==0 && cache.TextureCount()==4);
      }
      Require(receipt.Wait(error));auto pixels=renderer.ReadRGBA8(target,error);Require(pixels.size()==W*H*4);
      for(size_t byte=0;byte<pixels.size();++byte)Require(std::abs(int(pixels[byte])-int(colors[level][byte%4]))<=1);
    };
    for(NSUInteger level=0;level<3;++level)render(astc_texture,level,false);
    for(NSUInteger level=0;level<4;++level)render(rgba_texture,level,level==3);
    const auto stats=cache.Stats();Require(stats.buffer_creates==3 && stats.texture_creates==5 &&
        stats.buffer_hits==1 && stats.texture_hits==1 && stats.retired==4);
    [results addObject:@{@"case":@"immutable_game_resource_generations",@"passed":@YES,
        @"rgba_mips_sampled":@4,@"astc_mips_sampled":@3,@"cube_array_volume_planes_checked":@(dimensional_subresources),
        @"buffer_creates":@(stats.buffer_creates),@"texture_creates":@(stats.texture_creates),
        @"buffer_reuses":@(stats.buffer_hits),@"texture_reuses":@(stats.texture_hits),
        @"gpu_retains_retired_resources":@YES,@"invalid_uploads_rejected":@5}];
  }
  void GameDepthClip() {
    namespace r=theft4::render;
    PlanAdapter adapter(renderer);Require(adapter.Open(libraries.UTF8String,error));
    const auto copy=[](std::span<const uint8_t> bytes) {
      auto source=std::make_shared<r::Bytes>();source->generation=1;
      source->value.assign(bytes.begin(),bytes.end());return r::Buffer{source,0,bytes.size()};
    };
    const auto capture=[&](float z,float w,bool negative,bool depth_only,r::Compare compare,
                           double minimum,double maximum) {
      auto result=std::make_shared<r::Capture>();result->width=W;result->height=H;
      auto& d=result->draw;auto& p=d.pipeline;
      p.vertex.hash=0x048E49996734F6B5ull;p.negative_one_to_one=negative;
      if(!depth_only){p.fragment.hash=0x949ED69300FB92B7ull;p.colors[0]=r::Format::RGBA8Unorm;}
      p.depth=r::Format::Depth32Float;p.depth_test=true;p.depth_write=true;p.depth_compare=compare;
      p.attributes={{0,0,0,r::VertexFormat::Float4},{17,0,16,r::VertexFormat::Float4},
                    {13,0,32,r::VertexFormat::Float4}};p.streams[0]={sizeof(Vertex),false};
      d.viewport={0,0,double(W),double(H),minimum,maximum};d.scissor={0,0,W,H};d.vertex_count=6;
      const Case c{};auto banks=Constants(c,1);
      for(size_t i=0;i<3;++i)d.constants[i]=copy({
        static_cast<const uint8_t*>(banks[i].buffer.contents)+banks[i].offset,banks[i].length});
      auto vertices=Quad({1,0,0,1},z);
      for(auto& vertex:vertices){vertex.position[0]*=w;vertex.position[1]*=w;vertex.position[3]=w;}
      d.vertices[0]=copy(Bytes(vertices));return result;
    };
    struct Input {float z,w;bool negative,depth_only,visible;double minimum,maximum;};
    const Input inputs[]{
      {-0.5f,1,true,false,true,0,1},{-1,2,true,false,true,0,1},
      {-0.25f,0.5f,true,false,true,0,1},{0,2,true,false,true,0,1},
      {-1,1,true,false,true,0,1},{1,1,true,false,true,0,1},
      {-1.25f,1,true,false,false,0,1},{1.25f,1,true,false,false,0,1},
      {-0.5f,1,false,false,false,0,1},{0.25f,1,false,false,true,0,1},
      {-0.5f,1,true,true,true,0,1},{-1,2,true,true,true,0.25,0.75}};
    size_t checked=0;
    for(const auto& input:inputs) {
      auto original=capture(input.z,input.w,input.negative,input.depth_only,r::Compare::Always,input.minimum,input.maximum);
      auto draw=adapter.Realize(original,error);Require(bool(draw));
      Require(adapter.Realize(original,error)==draw);
      auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
          width:W height:H mipmapped:NO];descriptor.storageMode=MTLStorageModePrivate;
      descriptor.usage=MTLTextureUsageRenderTarget;auto depth=renderer.Texture(descriptor,error);Require(depth);
      auto first=Color(),second=Color();auto pass=Pass(first,nil,depth);
      pass.depthAttachment.storeAction=MTLStoreActionStore;
      if(input.depth_only)pass.colorAttachments[0].texture=nil;
      auto frame=renderer.BeginFrame(error);Require(bool(frame));Require(frame.BeginPass(pass,error));
      Require(frame.Encode(*draw,error));Require(frame.EndPass(error));
      // The stock positive-depth shader is an independent oracle. Equal must
      // pass only at the remapped depth, including the viewport depth interval.
      const float expected=input.negative ? (input.z/input.w+1)*0.5f : input.z/input.w;
      auto probe=capture(input.visible ? expected : 0.25f,1,false,false,r::Compare::Equal,input.minimum,input.maximum);
      auto probe_draw=adapter.Realize(probe,error);Require(bool(probe_draw));
      auto shade=Pass(second,nil,depth);shade.depthAttachment.loadAction=MTLLoadActionLoad;
      Require(frame.BeginPass(shade,error));Require(frame.Encode(*probe_draw,error));Require(frame.EndPass(error));
      auto receipt=frame.Submit(error);Require(bool(receipt));Require(receipt.Wait(error));
      auto pixels=renderer.ReadRGBA8(second,error);Require(pixels.size()==W*H*4);
      const uint8_t red[]{255,0,0,255},black[]{0,0,0,255};
      for(size_t byte=0;byte<pixels.size();++byte)
        Require(pixels[byte]==(input.visible ? red[byte%4] : black[byte%4]));
      ++checked;
    }
    [results addObject:@{@"case":@"game_negative_depth_clip",@"passed":@YES,@"projection_cases":@(checked),
      @"homogeneous_w_preserved":@YES,@"near_and_far_clip":@YES,@"stock_positive_depth_unchanged":@YES,
      @"stored_depth_matches_positive_shader":@YES,@"depth_only_and_viewport_range":@YES}];
  }
  void GamePipelineLayouts() {
    auto vs=ShaderFor({0x048E49996734F6B5ull,false},Stage::Vertex);
    auto fixed=[MTLRenderPipelineDescriptor new];fixed.vertexDescriptor=VertexDeclaration();
    fixed.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;
    auto state=[MTLDepthStencilDescriptor new];state.depthCompareFunction=MTLCompareFunctionLess;
    state.depthWriteEnabled=YES;
    Require(!renderer.MakePipeline(vs,Shader{},fixed,state,error));
    auto depth_pipeline=renderer.MakeDepthPipeline(vs,fixed,state,error);Require(bool(depth_pipeline));
    MTLRenderPipelineDescriptor* incompatible=[fixed copy];
    incompatible.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
    Require(!renderer.MakeDepthPipeline(vs,incompatible,state,error));
    auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
        width:W height:H mipmapped:NO];descriptor.storageMode=MTLStorageModePrivate;
    descriptor.usage=MTLTextureUsageRenderTarget;auto depth=renderer.Texture(descriptor,error);Require(depth);
    error.clear();auto frame=renderer.BeginFrame(error);Require(bool(frame));
    auto pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.depthAttachment.texture=depth;
    pass.depthAttachment.loadAction=MTLLoadActionClear;pass.depthAttachment.clearDepth=1;
    pass.depthAttachment.storeAction=MTLStoreActionStore;
    Require(frame.BeginPass(pass,error));Case c{};
    auto far=Packet(c,depth_pipeline,{0,0,1,1},0.75f),near=Packet(c,depth_pipeline,{1,0,0,1},0.25f);
    Require(frame.Encode(far,error));Require(frame.Encode(near,error));Require(frame.EndPass(error));
    auto target=Color();Case colored{};colored.depth=true;
    auto color_pipeline=PipelineFor(colored,1,MTLPixelFormatRGBA8Unorm,MTLCompareFunctionEqual);
    auto shade=Pass(target,nil,depth);shade.depthAttachment.loadAction=MTLLoadActionLoad;
    Require(frame.BeginPass(shade,error));
    near.pipeline=color_pipeline;far.pipeline=color_pipeline;
    Require(frame.Encode(near,error));Require(frame.Encode(far,error));Require(frame.EndPass(error));
    auto receipt=frame.Submit(error);Require(bool(receipt));Require(receipt.Wait(error));
    auto pixels=renderer.ReadRGBA8(target,error);Require(pixels.size()==W*H*4);
    const uint8_t red[]{255,0,0,255};
    for(size_t byte=0;byte<pixels.size();++byte)Require(pixels[byte]==red[byte%4]);
    [results addObject:@{@"case":@"game_depth_only_pipeline",@"passed":@YES,
        @"fragment_shader_omitted":@YES,@"stored_depth_reused":@YES}];
    frame={};
    MTLDepthStencilDescriptor* reverse_state=[state copy];reverse_state.depthCompareFunction=MTLCompareFunctionGreater;
    auto reverse_pipeline=renderer.MakeDepthPipeline(vs,fixed,reverse_state,error);Require(bool(reverse_pipeline));
    frame=renderer.BeginFrame(error);Require(bool(frame));pass.depthAttachment.clearDepth=0;
    near.pipeline=far.pipeline=reverse_pipeline;
    near.viewport.znear=far.viewport.znear=1;near.viewport.zfar=far.viewport.zfar=0;
    Require(frame.BeginPass(pass,error));Require(frame.Encode(near,error));Require(frame.Encode(far,error));Require(frame.EndPass(error));
    Require(frame.BeginPass(shade,error));near.pipeline=far.pipeline=color_pipeline;
    Require(frame.Encode(near,error));Require(frame.Encode(far,error));Require(frame.EndPass(error));
    receipt=frame.Submit(error);Require(bool(receipt));Require(receipt.Wait(error));
    pixels=renderer.ReadRGBA8(target,error);Require(pixels.size()==W*H*4);
    for(size_t byte=0;byte<pixels.size();++byte)Require(pixels[byte]==red[byte%4]);
    [results addObject:@{@"case":@"reversed_viewport_depth",@"passed":@YES,
        @"greater_depth_occlusion":@YES,@"stored_depth_reused":@YES}];
    frame={};
    auto fragment=ShaderFor({0x949ED69300FB92B7ull,false},Stage::Fragment);
    auto instanced=[MTLRenderPipelineDescriptor new];instanced.vertexDescriptor=VertexDeclaration(true);
    instanced.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
    auto pipeline=renderer.MakePipeline(vs,fragment,instanced,nil,error);Require(bool(pipeline));
    auto draw=Packet(c,pipeline,{1,1,1,1});draw.instance_count=2;
    const std::vector<float> instance_colors{1,0,0,1,0,0,1,1};draw.vertices[16]=Buffer(Bytes(instance_colors));
    target=Color();frame=renderer.BeginFrame(error);Require(bool(frame));Require(frame.BeginPass(Pass(target,nil,nil),error));
    auto too_small=draw;too_small.vertices[16].length=16;Require(!frame.Encode(too_small,error));error.clear();
    Require(frame.Encode(draw,error));Require(frame.EndPass(error));receipt=frame.Submit(error);
    Require(bool(receipt));Require(receipt.Wait(error));pixels=renderer.ReadRGBA8(target,error);
    Require(pixels.size()==W*H*4);const uint8_t blue[]{0,0,255,255};
    for(size_t byte=0;byte<pixels.size();++byte)Require(pixels[byte]==blue[byte%4]);
    [results addObject:@{@"case":@"game_stream_16_per_instance",@"passed":@YES,
        @"game_streams":@17,@"metal_buffer_slot":@24,@"instances":@2,@"short_instance_stream_rejected":@YES}];
  }
  void OrderedGameFrame() {
    namespace r=theft4::render;
    FrameAdapter adapter(renderer);Require(adapter.Open(libraries.UTF8String,error));
    const auto surface=[&](uint64_t id,uint32_t samples,bool array=false) {
      auto s=std::make_shared<r::Surface>();s->key={id,1};s->format=r::Format::RGBA8Unorm;
      s->width=s->height=array ? W*2 : W;s->samples=samples;
      if(array){s->kind=r::ImageKind::Texture2DArray;s->layers=2;s->levels=2;}
      return s;
    };
    auto msaa=surface(1,4),resolved=surface(2,1),layered=surface(3,1,true);
    const r::SurfaceView first{{1,1},0,0,r::Aspect::Color},second{{2,1},0,0,r::Aspect::Color},
      outputView{{3,1},1,1,r::Aspect::Color};
    const auto attachment=[](r::SurfaceView view,r::Load load=r::Load::Clear) {
      r::Attachment a;a.view=view;a.load=load;a.store=r::Store::Store;return a;
    };
    const auto capture=[&](std::array<float,4> color,bool texture,bool blend,uint32_t samples) {
      auto c=std::make_shared<r::Capture>();c->width=W;c->height=H;auto& d=c->draw;
      d.pipeline.vertex.hash=0x048E49996734F6B5ull;
      d.pipeline.fragment.hash=texture ? 0xB9589DA9F4B1770Full : 0x949ED69300FB92B7ull;
      d.pipeline.colors[0]=r::Format::RGBA8Unorm;d.pipeline.samples=samples;
      d.pipeline.attributes={{0,0,0,r::VertexFormat::Float4},{17,0,16,r::VertexFormat::Float4},
                             {13,0,32,r::VertexFormat::Float4}};d.pipeline.streams[0]={sizeof(Vertex),false};
      if(blend){auto& b=d.pipeline.blends[0];b.enabled=true;b.source_rgb=r::BlendFactor::SourceAlpha;
        b.destination_rgb=b.destination_alpha=r::BlendFactor::OneMinusSourceAlpha;}
      const auto copy=[](std::span<const uint8_t> bytes) {
        auto b=std::make_shared<r::Bytes>();b->generation=1;b->value.assign(bytes.begin(),bytes.end());
        return r::Buffer{b,0,bytes.size()};
      };
      Case test{};test.texture=texture;auto banks=Constants(test,samples);
      for(size_t i=0;i<3;++i)d.constants[i]=copy({static_cast<const uint8_t*>(banks[i].buffer.contents)+banks[i].offset,banks[i].length});
      auto vertices=Quad(color);d.vertices[0]=copy(Bytes(vertices));d.vertex_count=vertices.size();
      d.viewport={0,0,double(W),double(H),0,1};d.scissor={0,0,W,H};
      if(texture)d.fetches[0].sampler=std::make_shared<r::Sampler>();return c;
    };
    auto plan=std::make_shared<r::FramePlan>();plan->sequence=1;plan->surfaces={msaa,resolved,layered};plan->output=outputView;
    r::Pass a;a.colors[0]=attachment(first);a.colors[0]->store=r::Store::StoreAndResolve;a.colors[0]->resolve=second;
    a.commands.push_back(r::FrameDraw{capture({0.2f,0.4f,0.8f,1},false,false,4),{}});
    r::Pass b;b.colors[0]=attachment(outputView);r::FrameDraw sample{capture({1,1,1,1},true,false,1),{}};
    sample.produced[0]=second;b.commands.push_back(sample);
    r::Pass c;c.colors[0]=attachment(outputView,r::Load::Load);
    c.commands.push_back(r::FrameDraw{capture({1,0,0,0.5f},false,true,1),{}});plan->commands={a,b,c};
    auto receipt=adapter.Submit(plan,error);Require(bool(receipt));Require(receipt.Wait(error));
    auto target=adapter.Output(*plan,error);Require(target);
    const auto oracle=[&](const uint8_t* expected) {
      auto pixels=renderer.ReadRGBA8(target,error);Require(pixels.size()==W*H*4);
      for(size_t i=0;i<pixels.size();++i)Require(std::abs(int(pixels[i])-int(expected[i%4]))<=1);
    };
    const uint8_t firstExpected[]{153,51,102,255};oracle(firstExpected);
    const auto cold=adapter.Stats();const auto immutable=adapter.ImmutableStats();const auto pipelines=adapter.PipelineCount();
    receipt=adapter.Submit(plan,error);Require(bool(receipt));Require(receipt.Wait(error));oracle(firstExpected);
    const auto warm=adapter.Stats();const auto warmImmutable=adapter.ImmutableStats();
    Require(cold.surface_creates==3&&cold.surface_creates==warm.surface_creates&&cold.view_creates==warm.view_creates&&
      immutable.buffer_creates==warmImmutable.buffer_creates&&immutable.uploaded_bytes==warmImmutable.uploaded_bytes&&
      pipelines==adapter.PipelineCount());
    auto next=std::make_shared<r::FramePlan>();next->sequence=2;next->surfaces={layered};next->output=outputView;
    r::Pass continued;continued.colors[0]=attachment(outputView,r::Load::Load);
    continued.commands.push_back(r::FrameDraw{capture({0,0,1,0.5f},false,true,1),{}});next->commands={continued};
    receipt=adapter.Submit(next,error);Require(bool(receipt));
    // Drop every CPU target declaration while its final GPU frame is in flight.
    plan.reset();next.reset();msaa.reset();resolved.reset();layered.reset();
    Require(adapter.RetireResources()>=3);Require(adapter.Stats().allocated_bytes==0);
    Require(receipt.Wait(error));const uint8_t nextExpected[]{77,26,179,255};oracle(nextExpected);
    [results addObject:@{@"case":@"ordered_game_frame_passes",@"passed":@YES,
      @"msaa_store_and_resolve":@YES,@"gpu_produced_fetch":@YES,@"mip_and_array_slice":@YES,
      @"load_preserves_previous_pass_and_frame":@YES,@"warm_targets_and_uploads_reused":@YES,
      @"gpu_retains_retired_targets":@YES,@"synthetic_validation_geometry":@YES}];
  }
  void SampledGameRanges() {
    namespace r=theft4::render;
    FrameAdapter adapter(renderer);Require(adapter.Open(libraries.UTF8String,error));
    auto layered=std::make_shared<r::Surface>();layered->key={70,1};layered->format=r::Format::RGBA8Unorm;
    layered->kind=r::ImageKind::Texture2DArray;layered->width=layered->height=W*4;layered->levels=3;layered->layers=12;
    auto output=std::make_shared<r::Surface>();output->key={71,1};output->format=r::Format::RGBA8Unorm;output->width=W;output->height=H;
    auto plan=std::make_shared<r::FramePlan>();plan->sequence=70;plan->surfaces={layered,output};
    for(uint32_t level=0;level<3;++level)for(uint32_t face=0;face<12;++face) {
      r::Pass clear;r::Attachment a;a.view={layered->key,level,face,r::Aspect::Color};a.load=r::Load::Clear;a.store=r::Store::Store;
      a.clear_color={0.2+0.2*level,double(face+1)/16,0.4,0.8};clear.colors[0]=a;plan->commands.push_back(clear);
    }
    const auto copy=[](std::span<const uint8_t> bytes) {
      auto owner=std::make_shared<r::Bytes>();owner->generation=1;owner->value.assign(bytes.begin(),bytes.end());
      return r::Buffer{owner,0,bytes.size()};
    };
    auto capture=std::make_shared<r::Capture>();capture->width=W;capture->height=H;auto& d=capture->draw;
    d.pipeline.vertex.hash=0x048E49996734F6B5ull;d.pipeline.fragment.hash=0xB9589DA9F4B1770Full;
    d.pipeline.colors[0]=r::Format::RGBA8Unorm;
    d.pipeline.attributes={{0,0,0,r::VertexFormat::Float4},{17,0,16,r::VertexFormat::Float4},{13,0,32,r::VertexFormat::Float4}};
    d.pipeline.streams[0]={sizeof(Vertex),false};
    auto banks=Constants(Case{.texture=true},1);
    for(size_t i=0;i<3;++i)d.constants[i]=copy({static_cast<const uint8_t*>(banks[i].buffer.contents)+banks[i].offset,banks[i].length});
    auto vertices=Quad({1,1,1,1});d.vertices[0]=copy(Bytes(vertices));d.vertex_count=vertices.size();
    d.viewport={0,0,double(W),double(H),0,1};d.scissor={0,0,W,H};
    auto sampler=std::make_shared<r::Sampler>();sampler->min_lod_bits=sampler->max_lod_bits=std::bit_cast<uint32_t>(1.f);
    d.fetches[0].sampler=sampler;
    r::SampledSurfaceView input;input.surface=layered->key;input.level=1;input.levels=2;input.slice=7;
    input.swizzle={r::Swizzle::Blue,r::Swizzle::Green,r::Swizzle::Red,r::Swizzle::One};
    r::FrameDraw draw{capture,{}};draw.produced[0]=input;
    r::Pass sampled;r::Attachment target;target.view={output->key,0,0,r::Aspect::Color};
    target.load=r::Load::Clear;target.store=r::Store::Store;sampled.colors[0]=target;sampled.commands={draw};
    plan->commands.push_back(sampled);plan->output=target.view;
    const auto check=[&](const std::array<uint8_t,4>& expected) {
      auto receipt=adapter.Submit(plan,error);Require(bool(receipt));Require(receipt.Wait(error));
      auto pixels=renderer.ReadRGBA8(adapter.Output(*plan,error),error);Require(pixels.size()==W*H*4);
      for(size_t byte=0;byte<pixels.size();++byte)Require(std::abs(int(pixels[byte])-int(expected[byte%4]))<=1);
    };
    check({102,128,153,255});
    auto& fetch=std::get<r::FrameDraw>(std::get<r::Pass>(plan->commands.back()).commands[0]);
    fetch.produced[0]->format=r::Format::RGBA8Srgb;check({34,55,81,255});
    const auto warm=adapter.Stats();const auto uploads=adapter.ImmutableStats();check({34,55,81,255});
    Require(adapter.Stats().surface_creates==warm.surface_creates&&adapter.Stats().view_creates==warm.view_creates&&
      adapter.ImmutableStats().uploaded_bytes==uploads.uploaded_bytes);
    r::SampledSurfaceView cube;cube.surface=layered->key;cube.kind=r::ImageKind::TextureCube;
    cube.level=1;cube.levels=2;cube.slice=6;cube.slices=6;
    auto cubeTexture=adapter.SampledTexture(*plan,cube,error);Require(cubeTexture);
    Require(cubeTexture.textureType==MTLTextureTypeCube&&cubeTexture.width==W*2&&cubeTexture.mipmapLevelCount==2);
    auto face=[cubeTexture newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D
      levels:NSMakeRange(1,1) slices:NSMakeRange(3,1)];Require(face);
    auto pixels=renderer.ReadRGBA8(face,error);Require(pixels.size()==W*H*4);
    const uint8_t expectedFace[]{153,159,102,204};
    for(size_t byte=0;byte<pixels.size();++byte)Require(std::abs(int(pixels[byte])-int(expectedFace[byte%4]))<=1);
    cube.kind=r::ImageKind::TextureCubeArray;cube.slice=0;cube.slices=12;
    auto cubes=adapter.SampledTexture(*plan,cube,error);Require(cubes);
    Require(cubes.textureType==MTLTextureTypeCubeArray&&cubes.arrayLength==2&&cubes.mipmapLevelCount==2);
    cube.kind=r::ImageKind::Texture2DArray;cube.slice=5;cube.slices=3;
    auto array=adapter.SampledTexture(*plan,cube,error);Require(array);
    Require(array.textureType==MTLTextureType2DArray&&array.arrayLength==3&&array.width==W*2);
    auto impostor=std::make_shared<r::FramePlan>(*plan);impostor->surfaces[0]=std::make_shared<r::Surface>(*layered);
    Require(!adapter.SampledTexture(*impostor,cube,error));error.clear();
    cube.levels=3;Require(!adapter.SampledTexture(*plan,cube,error));error.clear();
    [results addObject:@{ @"case":@"sampled_game_texture_ranges",@"passed":@YES,
      @"mip_base_and_range":@YES,@"array_and_cube_aliases":@YES,@"cube_face_content":@YES,
      @"game_shader_lod_and_channel_swizzle":@YES,@"srgb_alias_decode":@YES,
      @"warm_aliases_and_uploads_reused":@YES,@"changed_owner_rejected":@YES,
      @"synthetic_validation_geometry":@YES }];
  }
  void OrderedFrameOperations() {
    namespace r=theft4::render;
    FrameAdapter adapter(renderer);Require(adapter.Open(libraries.UTF8String,error));
    const auto surface=[](uint64_t id,r::Format format) {
      auto s=std::make_shared<r::Surface>();s->key={id,1};s->width=W;s->height=H;s->format=format;return s;
    };
    auto color=surface(20,r::Format::RGBA8Unorm),other=surface(21,r::Format::RGBA8Unorm),
      depth=surface(22,r::Format::Depth32FloatStencil8),copied=surface(23,r::Format::RGBA8Unorm);
    const r::SurfaceView cv{{20,1},0,0,r::Aspect::Color},ov{{21,1},0,0,r::Aspect::Color},
      dv{{22,1},0,0,r::Aspect::Depth},sv{{22,1},0,0,r::Aspect::Stencil},out{{23,1},0,0,r::Aspect::Color};
    const auto attachment=[](r::SurfaceView view,std::array<double,4> color={}) {
      r::Attachment a;a.view=view;a.load=r::Load::Clear;a.store=r::Store::Store;a.clear_color=color;return a;
    };
    auto c=std::make_shared<r::Capture>();c->width=W;c->height=H;auto& d=c->draw;
    d.pipeline.vertex.hash=0x048E49996734F6B5ull;d.pipeline.fragment.hash=0x949ED69300FB92B7ull;
    d.pipeline.colors[0]=d.pipeline.colors[1]=r::Format::RGBA8Unorm;d.pipeline.blends[1].write_mask=0;
    d.pipeline.depth=d.pipeline.stencil=r::Format::Depth32FloatStencil8;
    d.pipeline.depth_test=true;d.pipeline.depth_compare=r::Compare::Less;d.pipeline.stencil_test=true;
    d.pipeline.front.compare=d.pipeline.back.compare=r::Compare::Equal;
    d.pipeline.front.write_mask=d.pipeline.back.write_mask=0;d.stencil_front_reference=d.stencil_back_reference=7;
    d.pipeline.attributes={{0,0,0,r::VertexFormat::Float4},{17,0,16,r::VertexFormat::Float4},
                           {13,0,32,r::VertexFormat::Float4}};d.pipeline.streams[0]={sizeof(Vertex),false};
    const auto copy=[](std::span<const uint8_t> data) {
      auto b=std::make_shared<r::Bytes>();b->generation=1;b->value.assign(data.begin(),data.end());
      return r::Buffer{b,0,data.size()};
    };
    Case test{};auto banks=Constants(test,1);
    for(size_t i=0;i<3;++i)d.constants[i]=copy({static_cast<const uint8_t*>(banks[i].buffer.contents)+banks[i].offset,banks[i].length});
    auto vertices=Quad({1,1,0,1});d.vertices[0]=copy(Bytes(vertices));d.vertex_count=vertices.size();
    d.viewport={0,0,double(W),double(H),0,1};d.scissor={0,0,W,H};
    r::Pass pass;pass.colors[0]=attachment(cv,{0,0,1,1});pass.colors[1]=attachment(ov,{0,1,0,1});
    pass.depth=attachment(dv);pass.depth->clear_depth=0.1;pass.stencil=attachment(sv);
    r::RectClear clear;clear.colors=1;clear.depth=clear.stencil=true;clear.rectangle={10,14,17,21};
    clear.color={1,0,0,1};clear.depth_value=0.8;clear.stencil_value=7;pass.commands.push_back(clear);
    // Depth-only clear must preserve stencil. The yellow draw fails depth in
    // this hole and succeeds elsewhere inside the stencil rectangle.
    clear.colors=0;clear.stencil=false;clear.rectangle={15,20,4,5};clear.depth_value=0.1;
    pass.commands.push_back(clear);pass.commands.push_back(r::FrameDraw{c,{}});
    // A clear after a masked draw must still write the selected MRT slot.
    clear.colors=2;clear.depth=false;clear.rectangle={3,5,7,9};clear.color={1,0,1,1};pass.commands.push_back(clear);
    r::Pass seed;seed.colors[0]=attachment(out,{1,1,1,1});
    r::ImageCopy transfer{cv,out,{8,12},{22,28},{21,25}};
    auto plan=std::make_shared<r::FramePlan>();plan->sequence=3;plan->surfaces={color,other,depth,copied};
    plan->commands={pass,seed,transfer};plan->output=out;
    const auto inside=[](size_t x,size_t y,size_t left,size_t top,size_t w,size_t h){return x>=left&&y>=top&&x-left<w&&y-top<h;};
    const auto colorAt=[&](size_t x,size_t y) {
      if(inside(x,y,15,20,4,5))return std::array<uint8_t,4>{255,0,0,255};
      if(inside(x,y,10,14,17,21))return std::array<uint8_t,4>{255,255,0,255};
      return std::array<uint8_t,4>{0,0,255,255};
    };
    const auto check=[&](r::SurfaceView view,int which) {
      auto selected=*plan;selected.output=view;auto target=adapter.Output(selected,error);Require(target);
      auto pixels=renderer.ReadRGBA8(target,error);Require(pixels.size()==W*H*4);
      for(size_t y=0;y<H;++y)for(size_t x=0;x<W;++x) {
        auto expected=which==0 ? colorAt(x,y) : which==1 ?
          (inside(x,y,3,5,7,9) ? std::array<uint8_t,4>{255,0,255,255} : std::array<uint8_t,4>{0,255,0,255}) :
          (inside(x,y,22,28,21,25) ? colorAt(x-22+8,y-28+12) : std::array<uint8_t,4>{255,255,255,255});
        for(size_t channel=0;channel<4;++channel)Require(pixels[(y*W+x)*4+channel]==expected[channel]);
      }
    };
    for(size_t repeat=0;repeat<2;++repeat) {
      auto receipt=adapter.Submit(plan,error);Require(bool(receipt));Require(receipt.Wait(error));
      check(cv,0);check(ov,1);check(out,2);
    }
    auto invalid=std::make_shared<r::FramePlan>(*plan);
    std::get<r::ImageCopy>(invalid->commands.back()).extent={UINT32_MAX,25};
    Require(!adapter.Submit(invalid,error));error.clear();check(cv,0);check(out,2);
    // Exercise a stencil-only utility pipeline (no color or depth outputs),
    // then independently test the stored stencil through a normal game draw.
    auto onlyStencil=surface(24,r::Format::Stencil8);
    const r::SurfaceView only{{24,1},0,0,r::Aspect::Stencil};
    r::Pass stencilPass;stencilPass.stencil=attachment(only);
    clear={};clear.stencil=true;clear.stencil_value=7;clear.rectangle={10,14,17,21};
    stencilPass.commands.push_back(clear);
    r::Pass stencilDraw;stencilDraw.colors[0]=attachment(cv,{0,0,1,1});stencilDraw.stencil=attachment(only);
    stencilDraw.stencil->load=r::Load::Load;
    auto noDepth=std::make_shared<r::Capture>(*c);noDepth->draw.pipeline.colors[1]=r::Format::Invalid;
    noDepth->draw.pipeline.depth=r::Format::Invalid;noDepth->draw.pipeline.stencil=r::Format::Stencil8;
    noDepth->draw.pipeline.depth_test=false;stencilDraw.commands.push_back(r::FrameDraw{noDepth,{}});
    auto stencilPlan=std::make_shared<r::FramePlan>();stencilPlan->sequence=4;stencilPlan->surfaces={color,onlyStencil};
    stencilPlan->commands={stencilPass,stencilDraw};stencilPlan->output=cv;
    auto receipt=adapter.Submit(stencilPlan,error);Require(bool(receipt));Require(receipt.Wait(error));
    auto pixels=renderer.ReadRGBA8(adapter.Output(*stencilPlan,error),error);Require(pixels.size()==W*H*4);
    for(size_t y=0;y<H;++y)for(size_t x=0;x<W;++x) {
      const auto expected=inside(x,y,10,14,17,21) ? std::array<uint8_t,4>{255,255,0,255} : std::array<uint8_t,4>{0,0,255,255};
      for(size_t channel=0;channel<4;++channel)Require(pixels[(y*W+x)*4+channel]==expected[channel]);
    }
    [results addObject:@{@"case":@"ordered_rectangular_clears_and_copies",@"passed":@YES,
      @"mrt_clear_ignores_draw_write_masks":@YES,@"depth_clear_preserves_stencil":@YES,
      @"draw_state_restored_after_clear":@YES,@"offset_copy_preserves_outside_pixels":@YES,
      @"stencil_only_pass_and_clear":@YES,
      @"invalid_copy_rejected_before_encoding":@YES,@"synthetic_validation_geometry":@YES}];
  }
  void OrderedHostUtilities() {
    namespace r=theft4::render;
    FrameAdapter adapter(renderer);Require(adapter.Open(libraries.UTF8String,error));
    const auto surface=[](uint64_t id,r::Format format) {
      auto s=std::make_shared<r::Surface>();s->key={id,1};s->width=W;s->height=H;s->format=format;return s;
    };
    auto source=surface(30,r::Format::RGBA8Unorm),output=surface(31,r::Format::BGRA8Unorm),
      depth=surface(32,r::Format::Depth32Float),forward=surface(33,r::Format::Depth32Float);
    const r::SurfaceView src{{30,1},0,0,r::Aspect::Color},dst{{31,1},0,0,r::Aspect::Color},
      sceneDepth{{32,1},0,0,r::Aspect::Depth},forwardDepth{{33,1},0,0,r::Aspect::Depth};
    const auto attachment=[](r::SurfaceView v,std::array<double,4> color={}) {
      r::Attachment a;a.view=v;a.load=r::Load::Clear;a.store=r::Store::Store;a.clear_color=color;return a;
    };
    r::Pass scene;scene.colors[0]=attachment(src,{0.2,0.4,0.8,0.5});scene.depth=attachment(sceneDepth);scene.depth->clear_depth=0.4;
    r::RectClear clear;clear.colors=1;clear.rectangle={7,11,19,23};clear.color={1,0,0,0.25};scene.commands.push_back(clear);
    r::Pass handoff;handoff.depth=attachment(forwardDepth);r::HostDraw copyDepth;
    copyDepth.program=r::HostProgram::DepthHandoff;copyDepth.pipeline.depth=r::Format::Depth32Float;copyDepth.pipeline.depth_write=true;
    copyDepth.scissor={0,0,W,H};copyDepth.fetches[0].produced=sceneDepth;copyDepth.fetches[0].sampler=std::make_shared<r::Sampler>();
    handoff.commands.push_back(copyDepth);
    r::Pass present;present.colors[0]=attachment(dst);present.colors[0]->load=r::Load::Discard;r::HostDraw display;
    display.pipeline.colors[0]=r::Format::BGRA8Unorm;display.scissor={0,0,W,H};display.fetches[0].produced=src;
    display.fetches[0].sampler=std::make_shared<r::Sampler>();
    rex::graphics::gta4_native::NativePresentConstants constants;constants.source_width=W;constants.source_height=H;
    constants.destination_width=W;constants.destination_height=H;constants.output_mode=4;
    auto bank=std::make_shared<r::Bytes>();bank->generation=1;bank->value.resize(16+sizeof(constants));
    memcpy(bank->value.data()+16,&constants,sizeof(constants));display.constants={bank,16,sizeof(constants)};present.commands.push_back(display);
    auto plan=std::make_shared<r::FramePlan>();plan->sequence=5;plan->surfaces={source,output,depth,forward};
    plan->commands={scene,handoff,present};plan->output=dst;
    const auto check=[&](bool astc) {
      auto pixels=renderer.ReadRGBA8(adapter.Output(*plan,error),error);Require(pixels.size()==W*H*4);
      for(size_t y=0;y<H;++y)for(size_t x=0;x<W;++x) {
        const bool rectangle=x>=7&&y>=11&&x<26&&y<34;
        const auto expected=astc ? std::array<uint8_t,4>{51,102,204,255} : rectangle ?
          std::array<uint8_t,4>{255,0,0,64} : std::array<uint8_t,4>{51,102,204,128};
        for(size_t ch=0;ch<4;++ch)Require(std::abs(int(pixels[(y*W+x)*4+ch])-int(expected[ch]))<=1);
      }
    };
    auto receipt=adapter.Submit(plan,error);Require(bool(receipt));Require(receipt.Wait(error));check(false);
    const auto cold=adapter.ImmutableStats();Require(adapter.PipelineCount()==2);
    receipt=adapter.Submit(plan,error);Require(bool(receipt));Require(receipt.Wait(error));check(false);
    Require(adapter.ImmutableStats().buffer_creates==cold.buffer_creates&&adapter.PipelineCount()==2);
    auto invalid=std::make_shared<r::FramePlan>(*plan);
    std::get<r::HostDraw>(std::get<r::Pass>(invalid->commands.back()).commands[0]).constants.length=43;
    Require(!adapter.Submit(invalid,error));error.clear();check(false);
    // The same presentation command also consumes a prepared immutable ASTC
    // source; this must retain the older-device cache's compressed upload.
    auto image=std::make_shared<r::Image>();image->format=r::Format::ASTC4x4;image->width=image->height=4;
    auto astc=std::make_shared<r::Bytes>();astc->generation=1;
    astc->value={0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff,0x33,0x33,0x66,0x66,0xcc,0xcc,0xff,0xff};
    image->source=astc;image->mips={{0,0,4,4,1,16,16,0,16}};
    display.fetches[0].produced.reset();display.fetches[0].image=image;
    constants.source_width=constants.source_height=4;bank=std::make_shared<r::Bytes>();bank->generation=2;
    bank->value.resize(sizeof(constants));memcpy(bank->value.data(),&constants,sizeof(constants));display.constants={bank,0,sizeof(constants)};
    present.commands={display};plan=std::make_shared<r::FramePlan>();plan->sequence=6;plan->surfaces={output};plan->commands={present};plan->output=dst;
    receipt=adapter.Submit(plan,error);Require(bool(receipt));Require(receipt.Wait(error));check(true);
    const auto uploaded=adapter.ImmutableStats();receipt=adapter.Submit(plan,error);Require(bool(receipt));Require(receipt.Wait(error));check(true);
    Require(adapter.ImmutableStats().texture_creates==uploaded.texture_creates&&adapter.ImmutableStats().uploaded_bytes==uploaded.uploaded_bytes);
    [results addObject:@{@"case":@"ordered_host_utility_commands",@"passed":@YES,
      @"gpu_depth_handoff_and_presentation":@YES,@"prepared_astc_source":@YES,
      @"warm_pipelines_and_resources_reused":@YES,@"short_host_constants_rejected":@YES,
      @"synthetic_validation_geometry":@YES}];
  }
  void HostUtilityShaders() {
    HostShaderStore host(renderer);Require(host.Open(std::string(libraries.UTF8String)+"/Host",error));
    Require(host.Size()==24);
    const char* names[]{"fullscreen_cw_vs","gta4_native_depth_handoff_ps","gta4_native_scene_depth_handoff_ps",
      "gta4_native_packed_depth_alias_ps","gta4_native_resolve_convert_ps","gta4_native_resolve_convert_msaa_ps",
      "gta4_native_resolve_convert_hdr_ps","gta4_native_resolve_convert_hdr_msaa_ps","gta4_native_resolve_depth_msaa_ps",
      "gta4_native_hdr_present_ps","gta4_native_split_postfx_ps","gta4_native_sun_shafts_ps",
      "smaa_edge_low_ps","smaa_weight_low_ps","smaa_edge_medium_ps","smaa_weight_medium_ps","smaa_edge_high_ps",
      "smaa_weight_high_ps","smaa_edge_ultra_ps","smaa_weight_ultra_ps","smaa_neighborhood_ps","smaa_present_ps",
      "smaa_hardware_present_ps","smaa_hardware_neighborhood_ps"};
    for(auto name:names)Require(host.Resolve(name,error).function);
    auto vs=host.Resolve("fullscreen_cw_vs",error),ps=host.Resolve("gta4_native_hdr_present_ps",error);
    auto fixed=[MTLRenderPipelineDescriptor new];fixed.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
    auto pipeline=renderer.MakePipeline(vs,ps,fixed,nil,error);Require(bool(pipeline));
    Require(pipeline->constant_bytes==std::array<NSUInteger,3>{44,0,0});
    auto sampler=[renderer.Device() newSamplerStateWithDescriptor:[MTLSamplerDescriptor new]];Require(sampler);
    auto source=SourceTexture(false);
    rex::graphics::gta4_native::NativePresentConstants constants;constants.source_width=constants.source_height=2;
    constants.destination_width=W;constants.destination_height=H;constants.output_mode=4;
    auto bank=Buffer({reinterpret_cast<const uint8_t*>(&constants),sizeof(constants)},true);
    Draw draw;draw.pipeline=pipeline;draw.vertex_count=3;draw.viewport={0,0,W,H,0,1};draw.scissor={0,0,W,H};
    const HostInput input{0,source,sampler};Require(host.Bind("gta4_native_hdr_present_ps",{&input,1},bank,draw,error));
    auto before=draw;auto shortBank=bank;shortBank.length=43;
    Require(!host.Bind("gta4_native_hdr_present_ps",{&input,1},shortBank,draw,error));error.clear();
    Require(draw.constants[0].length==before.constants[0].length&&draw.textures.size()==before.textures.size());
    auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:W height:H mipmapped:NO];
    desc.storageMode=MTLStorageModePrivate;desc.usage=MTLTextureUsageRenderTarget;
    auto target=renderer.Texture(desc,error);Require(target);
    auto frame=renderer.BeginFrame(error);Require(bool(frame));Require(frame.BeginPass(Pass(target,nil,nil),error));
    Require(frame.Encode(draw,error));Require(frame.EndPass(error));auto receipt=frame.Submit(error);
    Require(bool(receipt));Require(receipt.Wait(error));auto pixels=renderer.ReadRGBA8(target,error);Require(pixels.size()==W*H*4);
    const std::array<std::array<uint8_t,4>,4> colors{{{255,0,0,64},{0,255,0,128},{0,0,255,192},{255,255,0,255}}};
    for(size_t y=0;y<H;++y)for(size_t x=0;x<W;++x)for(size_t ch=0;ch<4;++ch)
      Require(pixels[(y*W+x)*4+ch]==colors[(y>=H/2?2:0)+(x>=W/2?1:0)][ch]);
    // Copy an actual GPU depth attachment through the renderer's exact host
    // shader, then test its stored values through a regular game depth draw.
    auto depthDesc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:W height:H mipmapped:NO];
    depthDesc.storageMode=MTLStorageModePrivate;depthDesc.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    auto srcDepth=renderer.Texture(depthDesc,error),dstDepth=renderer.Texture(depthDesc,error);Require(srcDepth&&dstDepth);
    auto seed=[MTLRenderPassDescriptor renderPassDescriptor];seed.depthAttachment.texture=srcDepth;
    seed.depthAttachment.loadAction=MTLLoadActionClear;seed.depthAttachment.storeAction=MTLStoreActionStore;seed.depthAttachment.clearDepth=0.4;
    frame=renderer.BeginFrame(error);Require(bool(frame));Require(frame.BeginPass(seed,error));Require(frame.EndPass(error));
    MTLRenderPassDescriptor* depthPass=[seed copy];depthPass.depthAttachment.texture=dstDepth;depthPass.depthAttachment.clearDepth=1;
    Require(frame.BeginPass(depthPass,error));fixed=[MTLRenderPipelineDescriptor new];fixed.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;
    auto depthState=[MTLDepthStencilDescriptor new];depthState.depthCompareFunction=MTLCompareFunctionAlways;depthState.depthWriteEnabled=YES;
    auto depthPipeline=renderer.MakePipeline(vs,host.Resolve("gta4_native_depth_handoff_ps",error),fixed,depthState,error);
    Require(bool(depthPipeline));Draw handoff;handoff.pipeline=depthPipeline;handoff.vertex_count=3;
    handoff.viewport={0,0,W,H,0,1};handoff.scissor={0,0,W,H};const HostInput depthInput{0,srcDepth,sampler};
    Require(host.Bind("gta4_native_depth_handoff_ps",{&depthInput,1},{},handoff,error));Require(frame.Encode(handoff,error));Require(frame.EndPass(error));
    auto color=Color();auto pass=Pass(color,nil,dstDepth);pass.depthAttachment.loadAction=MTLLoadActionLoad;
    Require(frame.BeginPass(pass,error));Case test{};test.depth=true;auto gamePipeline=PipelineFor(test,1);
    auto nearDraw=Packet(test,gamePipeline,{1,0,0,1},0.2f),farDraw=Packet(test,gamePipeline,{0,1,0,1},0.6f);
    Require(frame.Encode(nearDraw,error));Require(frame.Encode(farDraw,error));Require(frame.EndPass(error));receipt=frame.Submit(error);
    Require(bool(receipt));Require(receipt.Wait(error));pixels=renderer.ReadRGBA8(color,error);Require(pixels.size()==W*H*4);
    const uint8_t red[]{255,0,0,255};for(size_t byte=0;byte<pixels.size();++byte)Require(pixels[byte]==red[byte%4]);
    const auto loaded=host.LoadedFunctions();Require(host.Resolve("gta4_native_hdr_present_ps",error).function==ps.function);
    Require(host.LoadedFunctions()==loaded&&loaded==24);
    [results addObject:@{@"case":@"native_host_shader_programs",@"passed":@YES,@"compiled_host_programs":@24,
      @"loaded_all_functions":@YES,@"present_orientation_color_and_alpha":@YES,@"gpu_depth_handoff":@YES,
      @"host_constant_abi":@YES,@"failed_binding_preserves_draw":@YES,@"function_cache_reused":@YES,
      @"synthetic_validation_geometry":@YES}];
  }
  void GameTexturePitchPlan() {
    namespace r=theft4::render;
    PlanAdapter adapter(renderer);Require(adapter.Open(libraries.UTF8String,error));
    auto capture=std::make_shared<r::Capture>();capture->width=W;capture->height=H;
    auto& d=capture->draw;
    d.pipeline.vertex.hash=0x048E49996734F6B5ull;
    d.pipeline.fragment.hash=0xB9589DA9F4B1770Full;
    d.pipeline.colors[0]=r::Format::RGBA8Unorm;
    d.pipeline.attributes={{0,0,0,r::VertexFormat::Float4},{17,0,16,r::VertexFormat::Float4},
                           {13,0,32,r::VertexFormat::Float4}};
    d.pipeline.streams[0]={sizeof(Vertex),false};
    d.viewport={0,0,double(W),double(H),0,1};d.scissor={0,0,W,H};d.vertex_count=6;
    const auto copy=[](std::span<const uint8_t> bytes) {
      auto source=std::make_shared<r::Bytes>();source->generation=1;
      source->value.assign(bytes.begin(),bytes.end());return r::Buffer{source,0,bytes.size()};
    };
    Case c{};c.texture=true;auto banks=Constants(c,1);
    for(size_t i=0;i<3;++i)d.constants[i]=copy({
      static_cast<const uint8_t*>(banks[i].buffer.contents)+banks[i].offset,banks[i].length});
    auto vertices=Quad({1,1,1,1});d.vertices[0]=copy(Bytes(vertices));
    const uint8_t expected[4][4]{{255,0,0,64},{0,255,0,128},{0,0,255,192},{255,255,0,255}};
    std::vector<uint8_t> storage(32,0xab);
    // Nonzero payload offset, padded rows, and a neutral plane pitch. Padding
    // must never become texels; this reproduces the real capture's contract.
    memcpy(storage.data()+4,expected[0],8);memcpy(storage.data()+20,expected[2],8);
    auto image=std::make_shared<r::Image>();image->source=copy(storage).source;
    image->format=r::Format::RGBA8Unorm;image->width=2;image->height=2;
    image->mips={{0,0,2,2,1,16,32,4,24}};
    d.fetches[0].image=image;d.fetches[0].sampler=std::make_shared<r::Sampler>();
    auto prepared=adapter.Realize(capture,error);Require(bool(prepared));
    auto target=Color();auto frame=renderer.BeginFrame(error);Require(bool(frame));
    Require(frame.BeginPass(Pass(target,nil,nil),error));Require(frame.Encode(*prepared,error));
    Require(frame.EndPass(error));auto receipt=frame.Submit(error);Require(bool(receipt));Require(receipt.Wait(error));
    auto pixels=renderer.ReadRGBA8(target,error);Require(pixels.size()==W*H*4);
    for(NSUInteger y=0;y<H;++y)for(NSUInteger x=0;x<W;++x)for(size_t channel=0;channel<4;++channel)
      Require(pixels[(y*W+x)*4+channel]==expected[(y>=H/2)*2+(x>=W/2)][channel]);
    [results addObject:@{@"case":@"game_texture_pitch_adapter",@"passed":@YES,
      @"padded_rows_and_payload_offset":@YES,@"neutral_plane_pitch":@YES}];
  }
  void GameDrawPlan() {
    namespace r=theft4::render;
    PlanAdapter adapter(renderer);Require(adapter.Open(libraries.UTF8String,error));
    auto source=std::make_shared<r::Capture>();source->width=W;source->height=H;
    auto& d=source->draw;
    d.pipeline.vertex.hash=0x048E49996734F6B5ull;
    d.pipeline.fragment.hash=0x949ED69300FB92B7ull;
    d.pipeline.colors[0]=r::Format::BGRA8Unorm;
    d.pipeline.attributes={{0,0,0,r::VertexFormat::Float4},{17,0,16,r::VertexFormat::Float4},
                           {13,0,32,r::VertexFormat::Float4}};
    d.pipeline.streams[0]={sizeof(Vertex),false};
    auto& b=d.pipeline.blends[0];b.enabled=true;b.source_rgb=r::BlendFactor::ConstantColor;
    b.source_alpha=r::BlendFactor::One;b.write_mask=9;
    d.blend_color={0.5f,0.25f,0.75f,1};
    d.viewport={0,0,double(W),double(H),0,1};d.scissor={0,0,W,H};
    const auto copy=[](std::span<const uint8_t> bytes) {
      auto s=std::make_shared<r::Bytes>();s->generation=1;s->value.assign(bytes.begin(),bytes.end());
      return r::Buffer{s,0,bytes.size()};
    };
    const Case c{};auto banks=Constants(c,1);
    for(size_t i=0;i<3;++i)
      d.constants[i]=copy({static_cast<const uint8_t*>(banks[i].buffer.contents)+banks[i].offset,banks[i].length});
    auto vertices=Quad({0.8f,0.6f,0.4f,1},0.5f,true);d.vertices[0]=copy(Bytes(vertices));
    const std::vector<uint16_t> indices{0,1,2,0,2,3};d.indices=copy(Bytes(indices));d.index_count=6;
    auto immutable=std::shared_ptr<const r::Capture>(source);source.reset();
    NSString* fixture=[output stringByAppendingPathComponent:@"PlanFixture/synthetic-plan.t4draw"];
    Require(r::WriteCapture(fixture.UTF8String,*immutable,error));
    auto reversed=*immutable;
    reversed.draw.pipeline.depth=r::Format::Depth32Float;
    reversed.draw.pipeline.depth_test=true;reversed.draw.pipeline.depth_write=true;
    reversed.draw.pipeline.depth_compare=r::Compare::GreaterEqual;
    NSString* reversedFixture=[output stringByAppendingPathComponent:@"ReverseDepthFixture/reversed-depth.t4draw"];
    Require(r::WriteCapture(reversedFixture.UTF8String,reversed,error));
    reversed={};
    auto prepared=adapter.Realize(immutable,error);Require(bool(prepared));
    const auto resources=adapter.ResourceStats();
    Require(adapter.Realize(immutable,error)==prepared);
    auto warm=adapter.ResourceStats();
    Require(adapter.PipelineCount()==1&&warm.buffer_creates==resources.buffer_creates&&
            warm.uploaded_bytes==resources.uploaded_bytes);
    auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:W height:H mipmapped:NO];descriptor.usage=MTLTextureUsageRenderTarget;
    descriptor.storageMode=MTLStorageModePrivate;
    auto target=renderer.Texture(descriptor,error);Require(target);
    auto frame=renderer.BeginFrame(error);Require(bool(frame));auto pass=Pass(target,nil,nil);
    pass.colorAttachments[0].clearColor=MTLClearColorMake(0.1,0.2,0.3,0.4);
    Require(frame.BeginPass(pass,error));Require(frame.Encode(*prepared,error));Require(frame.EndPass(error));
    immutable.reset();Require(adapter.RetireResources()>=5);prepared.reset();
    auto receipt=frame.Submit(error);Require(bool(receipt));Require(receipt.Wait(error));
    auto pixels=renderer.ReadRGBA8(target,error);Require(pixels.size()==W*H*4);
    const uint8_t expected[]{102,51,77,255};
    for(size_t i=0;i<pixels.size();++i)Require(std::abs(int(pixels[i])-int(expected[i%4]))<=1);
    [results addObject:@{@"case":@"immutable_game_draw_plan",@"passed":@YES,
        @"warm_pipeline_and_resource_reuse":@YES,@"indexed_range_derived":@YES,
        @"rgba_write_mask_on_bgra":@YES,@"constant_color_blend":@YES,
        @"retired_before_submission":@YES}];
    auto replay=ReplayDirectMetalCaptures(libraries,[output stringByAppendingPathComponent:@"PlanFixture"],
        [output stringByAppendingPathComponent:@"PlanReplayValidation"]);
    Require([replay[@"passed"] boolValue]&&[replay[@"visible_draws"] unsignedIntegerValue]==1);
    [results addObject:@{@"case":@"private_draw_serialization_replay",@"passed":@YES,
        @"synthetic_validation_geometry":@YES,@"repeat_pixels_identical":@YES,
        @"gpu_target_sampling":@YES}];
    auto reversedReplay=ReplayDirectMetalCaptures(libraries,[output stringByAppendingPathComponent:@"ReverseDepthFixture"],
        [output stringByAppendingPathComponent:@"ReverseDepthValidation"]);
    Require([reversedReplay[@"passed"] boolValue]&&[reversedReplay[@"visible_draws"] unsignedIntegerValue]==1);
    NSDictionary* reversedCase=[reversedReplay[@"cases"] firstObject];
    Require([reversedCase[@"visible_pixels"] unsignedIntegerValue]==W*H&&
            [reversedCase[@"isolated_depth_seed"] doubleValue]==0);
    [results addObject:@{@"case":@"reversed_depth_capture_replay",@"passed":@YES,
        @"full_target_visible":@YES,@"synthetic_validation_geometry":@YES}];
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
    probe.AdmissionAndLifetime();probe.CatalogAndCache();probe.ResourceGenerations();
    probe.GameDepthClip();probe.GamePipelineLayouts();probe.GameDrawPlan();probe.GameTexturePitchPlan();probe.OrderedGameFrame();probe.SampledGameRanges();probe.OrderedFrameOperations();probe.HostUtilityShaders();probe.OrderedHostUtilities();passed=true;
  } catch(const std::exception& error){failure=[NSString stringWithUTF8String:error.what()];}
  auto device=probe.renderer.Device();
  NSDictionary* report=@{@"schema":@2,@"passed":@(passed),@"failure":failure,@"cases":probe.results,
    @"source_revision":@THEFT4_METAL_SOURCE_REVISION,
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
      // Reuse the game's logical output while CAMetalLayer rotates its actual
      // textures. Exercise FrameAdapter presentation, beyond one raw frame.
      namespace r=theft4::render;
      FrameAdapter adapter(probe.renderer);probe.Require(adapter.Open(libraries.UTF8String,probe.error));
      auto output=std::make_shared<r::Surface>();output->key={1,1};
      output->width=output->height=256;output->format=r::Format::BGRA8Unorm;
      auto plan=std::make_shared<r::FramePlan>();plan->surfaces={output};
      plan->output=r::SurfaceView{output->key,0,0,r::Aspect::Color};
      r::Attachment attachment;attachment.view=*plan->output;
      attachment.load=r::Load::Clear;attachment.store=r::Store::Store;
      attachment.clear_color={0.1,0.0,0.3,1.0};
      r::Pass pass;pass.colors[0]=attachment;plan->commands={pass};
      std::vector<Receipt> rotating;
      for(unsigned i=0;i<3;++i) {
        id<CAMetalDrawable> next=[layer nextDrawable];probe.Require(next);
        auto submitted=adapter.SubmitAndPresent(plan,output->key,next,probe.error);
        probe.Require(bool(submitted));rotating.push_back(std::move(submitted));
      }
      for(auto& submitted:rotating)probe.Require(submitted.Wait(probe.error));
      rotating.clear();
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
