#include "direct_metal_probe.h"
#include "theft4_native_metal.h"
#include "theft4_metal_shader_store.h"
#include "theft4_metal_resources.h"
#include "theft4_metal_plan.h"
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
    probe.GamePipelineLayouts();probe.GameDrawPlan();passed=true;
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
