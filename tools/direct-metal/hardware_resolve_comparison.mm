// Exhaustive two-sample byte pairs and four-sample rounding boundaries.
// A mismatch disqualifies hardware averaging; it is not a renderer failure.
#import <Metal/Metal.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Json=nlohmann::json;
static void Require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static void Wait(id<MTLCommandBuffer> b){[b commit];[b waitUntilCompleted];Require(b.status==MTLCommandBufferStatusCompleted,"GPU execution failed");}
int main(int argc,char** argv){@autoreleasepool{try {
  Require(argc==2,"Report path required");auto device=MTLCreateSystemDefaultDevice();Require(device,"No GPU");
  auto queue=[device newCommandQueue];NSError* error=nil;auto options=[MTLCompileOptions new];options.fastMathEnabled=NO;
  NSString* source=@R"METAL(
#include <metal_stdlib>
using namespace metal;
struct V {float4 position [[position]];};
vertex V fullscreen(uint i [[vertex_id]]){return {float4(i==2?3.0:-1.0,i==1?3.0:-1.0,0,1)};}
fragment float4 pattern(V v [[stage_in]],uint sample [[sample_id]]) {
  uint a=uint(v.position.x)&255,b=uint(v.position.y)&255;
  uint value=sample==0?a:sample==1?b:sample==2?255-a:255-b;
  return float4(value,(value+1)&255,(value+127)&255,(value+255)&255)/255.0;
}
fragment float4 average(V v [[stage_in]],texture2d_ms<float> source [[texture(0)]]) {
  uint2 p=uint2(v.position.xy);
  if(source.get_num_samples()==2)return (source.read(p,0)+source.read(p,1))*0.5;
  return ((source.read(p,0)+source.read(p,1))+source.read(p,2)+source.read(p,3))*0.25;
}
)METAL";
  auto library=[device newLibraryWithSource:source options:options error:&error];Require(library,error.localizedDescription.UTF8String);
  const auto texture=[&](MTLPixelFormat format,NSUInteger samples) {
    auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:256 height:256 mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    if(samples>1){d.textureType=MTLTextureType2DMultisample;d.sampleCount=samples;}
    return [device newTextureWithDescriptor:d];
  };
  const auto pipeline=[&](MTLPixelFormat format,NSUInteger samples,NSString* fragment) {
    auto d=[MTLRenderPipelineDescriptor new];d.vertexFunction=[library newFunctionWithName:@"fullscreen"];
    d.fragmentFunction=[library newFunctionWithName:fragment];d.rasterSampleCount=samples;d.colorAttachments[0].pixelFormat=format;
    auto p=[device newRenderPipelineStateWithDescriptor:d error:&error];Require(p,error.localizedDescription.UTF8String);return p;
  };
  Json cases=Json::array();bool all_exact=true;
  for(auto format:{MTLPixelFormatRGBA8Unorm,MTLPixelFormatBGRA8Unorm})for(NSUInteger samples:{2u,4u}) {
    auto ms=texture(format,samples),hardware=texture(format,1),shader=texture(format,1);
    Require(ms&&hardware&&shader,"Resolve textures unavailable");
    auto b=[queue commandBuffer];auto pass=[MTLRenderPassDescriptor renderPassDescriptor];auto a=pass.colorAttachments[0];
    a.texture=ms;a.resolveTexture=hardware;a.loadAction=MTLLoadActionDontCare;a.storeAction=MTLStoreActionStoreAndMultisampleResolve;
    auto e=[b renderCommandEncoderWithDescriptor:pass];[e setRenderPipelineState:pipeline(format,samples,@"pattern")];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];Wait(b);
    b=[queue commandBuffer];pass=[MTLRenderPassDescriptor renderPassDescriptor];a=pass.colorAttachments[0];
    a.texture=shader;a.loadAction=MTLLoadActionDontCare;a.storeAction=MTLStoreActionStore;
    e=[b renderCommandEncoderWithDescriptor:pass];[e setRenderPipelineState:pipeline(format,1,@"average")];
    [e setFragmentTexture:ms atIndex:0];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];Wait(b);
    auto h=[device newBufferWithLength:256*256*4 options:MTLResourceStorageModeShared];auto s=[device newBufferWithLength:h.length options:MTLResourceStorageModeShared];
    b=[queue commandBuffer];auto blit=[b blitCommandEncoder];
    [blit copyFromTexture:hardware sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(256,256,1) toBuffer:h destinationOffset:0 destinationBytesPerRow:1024 destinationBytesPerImage:h.length];
    [blit copyFromTexture:shader sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(256,256,1) toBuffer:s destinationOffset:0 destinationBytesPerRow:1024 destinationBytesPerImage:s.length];[blit endEncoding];Wait(b);
    size_t differences=0;unsigned maximum=0;auto hp=static_cast<const uint8_t*>(h.contents),sp=static_cast<const uint8_t*>(s.contents);
    for(size_t i=0;i<h.length;++i){differences+=hp[i]!=sp[i];maximum=std::max(maximum,unsigned(std::abs(int(hp[i])-int(sp[i]))));}
    all_exact&=!differences;cases.push_back({{"format",uint32_t(format)},{"samples",samples},{"compared_bytes",h.length},{"different_bytes",differences},{"maximum_byte_difference",maximum},{"byte_identical",differences==0}});
  }
  Json report={{"device",device.name.UTF8String},{"scope","Synthetic UNORM averaging qualification; not game frame time"},{"cases",cases},{"all_byte_identical",all_exact},{"policy","Keep programmable resolves when hardware changes rounding"}};
  std::ofstream(argv[1])<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}}
