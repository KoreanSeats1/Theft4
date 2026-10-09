// Real Metal objects deliberately collide in the preceding low-bit cache.
// The same draw also checks bounds on cache hits and public descriptor isolation.
#include "theft4_native_metal.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
using namespace theft4::metal;
template<class Object> static auto Colliders(const std::vector<Object>& objects) {
  std::map<size_t,std::vector<Object>> bins;
  for(const auto& o:objects)bins[(reinterpret_cast<uintptr_t>((__bridge void*)o)>>4)%64].push_back(o);
  for(const auto& [_,bin]:bins)if(bin.size()>=4)return std::array<Object,4>{bin[0],bin[1],bin[2],bin[3]};
  assert(false&&"No four-object collision found");return std::array<Object,4>{};
}
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  setenv("THEFT4_RETAIL_MODE","0",1);
  @autoreleasepool {
    Renderer renderer;assert(renderer.Ready());std::string error;
    const char* source=R"(#include <metal_stdlib>
using namespace metal;
struct V {float4 position [[position]];float2 uv;};
vertex V vs(uint i [[vertex_id]],constant float4* vertices [[buffer(0)]]) {
  V v;v.position=vertices[i];v.uv=vertices[i].xy*0.5+0.5;return v;
}
fragment float4 ps(V v [[stage_in]],texture2d<float> image [[texture(0)]],sampler s [[sampler(0)]]) {
  return image.sample(s,v.uv);
})";
    NSError* compile_error=nil;
    auto library=[renderer.Device() newLibraryWithSource:[NSString stringWithUTF8String:source] options:nil error:&compile_error];
    assert(library);
    ShaderInterface vi,pi;vi.constant_bytes={48,0,0};pi.constant_bytes={0,0,0};
    pi.textures=pi.samplers=1;pi.texture_types[0]=MTLTextureType2D;
    auto vs=renderer.LoadShader(library,Stage::Vertex,vi,0,error,"vs");
    auto ps=renderer.LoadShader(library,Stage::Fragment,pi,0,error,"ps");
    auto fixed=[MTLRenderPipelineDescriptor new];fixed.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
    auto pipeline=renderer.MakePipeline(vs,ps,fixed,nil,error);assert(pipeline);
    const std::array<float,12> triangle{-1,-1,0,1,3,-1,0,1,-1,3,0,1};
    std::vector<id<MTLBuffer>> buffers;std::vector<id<MTLTexture>> images;
    for(size_t i=0;i<256;++i) {
      buffers.push_back(renderer.ImmutableBuffer({reinterpret_cast<const uint8_t*>(triangle.data()),sizeof(triangle)},error));assert(buffers.back());
      auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
      d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;
      auto texture=renderer.Texture(d,error);assert(texture);const std::array<uint8_t,4> color{uint8_t(i),uint8_t(255-i),71,255};
      [texture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:color.data() bytesPerRow:4];images.push_back(texture);
    }
    const auto input_buffers=Colliders(buffers);
    const auto input_images=Colliders(images);
    auto sd=[MTLSamplerDescriptor new];auto sampler=[renderer.Device() newSamplerStateWithDescriptor:sd];assert(sampler);
    auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:4 height:4 mipmapped:NO];
    td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageRenderTarget;
    auto target=renderer.Texture(td,error);assert(target);
    auto pd=[MTLRenderPassDescriptor renderPassDescriptor];pd.colorAttachments[0].texture=target;
    pd.colorAttachments[0].loadAction=MTLLoadActionClear;pd.colorAttachments[0].storeAction=MTLStoreActionStore;
    Draw draw;draw.pipeline=pipeline;draw.vertex_count=3;draw.viewport={0,0,4,4,0,1};draw.scissor={0,0,4,4};
    draw.samplers={{Stage::Fragment,0,sampler}};draw.textures={{Stage::Fragment,0,input_images[0]}};
    auto frame=renderer.BeginFrame(error);assert(frame&&frame.BeginPass(pd,error));
    for(size_t i=0;i<1024;++i) {
      draw.constants[0]={input_buffers[i%4],0,48};draw.textures[0].texture=input_images[i%4];
      assert(frame.Encode(draw,error));
    }
    const auto stats=frame.Stats();
#ifdef THEFT4_EXPECT_ASSOCIATIVE_TRAITS
    assert(stats.buffer_extent_queries==4&&stats.texture_shape_queries==4);
#endif
    auto bad=draw;bad.constants[0].offset=16;bad.constants[0].length=32;assert(!frame.Encode(bad,error));
    bad=draw;auto wrong=std::make_shared<Pipeline>(*pipeline);wrong->fragment.texture_types[0]=MTLTextureTypeCube;
    bad.pipeline=wrong;assert(!frame.Encode(bad,error));assert(frame.Encode(draw,error));
    assert(frame.EndPass(error));auto receipt=frame.Submit(error);assert(receipt&&receipt.Wait(error));
    std::array<uint8_t,4> expected{};
    [input_images[3] getBytes:expected.data() bytesPerRow:4 fromRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0];
    const auto pixels=renderer.ReadRGBA8(target,error);assert(pixels.size()==64);
    for(size_t i=0;i<pixels.size();++i)assert(pixels[i]==expected[i%4]);
    // A caller can edit its descriptor after public BeginPass. Clears must
    // still use the isolated attachment state retained by that frame.
    auto isolated=renderer.BeginFrame(error);assert(isolated&&isolated.BeginPass(pd,error));
    pd.colorAttachments[0].texture=nil;pd.colorAttachments[0].storeAction=MTLStoreActionDontCare;
    assert(isolated.ClearRectangle({1,false,false,{0,0,4,4},{0,0,1,1}},error));
    assert(isolated.EndPass(error));auto completed=isolated.Submit(error);assert(completed&&completed.Wait(error));
    const auto clear_pixels=renderer.ReadRGBA8(target,error);
    for(size_t i=0;i<clear_pixels.size();i+=4)assert(clear_pixels[i]==0&&clear_pixels[i+1]==0&&clear_pixels[i+2]==255&&clear_pixels[i+3]==255);
    nlohmann::json report{{"draws",1024},{"colliding_buffers",4},{"colliding_textures",4},
      {"buffer_extent_queries",stats.buffer_extent_queries},{"texture_shape_queries",stats.texture_shape_queries},
      {"cached_bounds_and_shape_rejections",true},{"GPU_pixels_equal",true},{"public_descriptor_isolated",true}};
    std::ofstream(argv[1])<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';
  }
}
