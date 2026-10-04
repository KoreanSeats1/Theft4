// A/B bandwidth experiment. This is synthetic GPU work, not game frame time.
#import <Metal/Metal.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>
#ifndef THEFT4_METAL_SOURCE_REVISION
#define THEFT4_METAL_SOURCE_REVISION "development"
#endif
namespace {
constexpr NSUInteger width=1920,height=1080,passes=64;
void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
double Median(std::vector<double> values){std::sort(values.begin(),values.end());return (values[(values.size()-1)/2]+values[values.size()/2])/2;}
void Wait(id<MTLCommandBuffer> command){[command commit];[command waitUntilCompleted];Require(command.status==MTLCommandBufferStatusCompleted,command.error?command.error.localizedDescription.UTF8String:"GPU command failed");}
id<MTLTexture> Texture(id<MTLDevice> device,MTLPixelFormat format,NSUInteger samples,bool pixel_view) {
  auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
  d.storageMode=MTLStorageModePrivate;d.hazardTrackingMode=MTLHazardTrackingModeTracked;
  d.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
  if(pixel_view)d.usage|=MTLTextureUsagePixelFormatView;
  if(samples>1){d.textureType=MTLTextureType2DMultisample;d.sampleCount=samples;}
  auto texture=[device newTextureWithDescriptor:d];Require(texture,"Texture allocation failed");return texture;
}
id<MTLRenderPipelineState> Pipeline(id<MTLDevice> device,id<MTLLibrary> library,NSString* fragment,MTLPixelFormat format,NSUInteger samples) {
  auto d=[MTLRenderPipelineDescriptor new];d.vertexFunction=[library newFunctionWithName:@"fullscreen"];
  d.fragmentFunction=[library newFunctionWithName:fragment];d.colorAttachments[0].pixelFormat=format;
  d.rasterSampleCount=samples;NSError* error=nil;auto p=[device newRenderPipelineStateWithDescriptor:d error:&error];
  Require(p,error?error.localizedDescription.UTF8String:"Pipeline failed");return p;
}
void Render(id<MTLCommandBuffer> command,id<MTLTexture> target,id<MTLRenderPipelineState> pipeline,id<MTLTexture> source=nil) {
  auto pass=[MTLRenderPassDescriptor renderPassDescriptor];auto a=pass.colorAttachments[0];
  a.texture=target;a.loadAction=MTLLoadActionDontCare;a.storeAction=MTLStoreActionStore;
  auto e=[command renderCommandEncoderWithDescriptor:pass];Require(e,"Encoder failed");[e setRenderPipelineState:pipeline];
  if(source)[e setFragmentTexture:source atIndex:0];
  [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
}
std::vector<uint8_t> Read(id<MTLDevice> device,id<MTLCommandQueue> queue,id<MTLTexture> texture,NSUInteger bytes_per_pixel) {
  auto command=[queue commandBuffer];
  if(texture.sampleCount>1) {
    auto resolved=Texture(device,texture.pixelFormat,1,false);
    auto pass=[MTLRenderPassDescriptor renderPassDescriptor];auto a=pass.colorAttachments[0];
    a.texture=texture;a.resolveTexture=resolved;a.loadAction=MTLLoadActionLoad;a.storeAction=MTLStoreActionMultisampleResolve;
    auto e=[command renderCommandEncoderWithDescriptor:pass];Require(e,"Resolve encoder failed");[e endEncoding];texture=resolved;
  }
  const NSUInteger row=(width*bytes_per_pixel+255)&~NSUInteger(255);
  auto buffer=[device newBufferWithLength:row*height options:MTLResourceStorageModeShared];Require(buffer,"Readback allocation failed");
  auto blit=[command blitCommandEncoder];[blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
    sourceSize:MTLSizeMake(width,height,1) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*height];
  [blit endEncoding];Wait(command);std::vector<uint8_t> bytes(width*height*bytes_per_pixel);
  for(NSUInteger y=0;y<height;++y)std::memcpy(bytes.data()+y*width*bytes_per_pixel,static_cast<uint8_t*>(buffer.contents)+y*row,width*bytes_per_pixel);
  return bytes;
}
}
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  try {@autoreleasepool {
    auto device=MTLCreateSystemDefaultDevice();Require(device,"Metal unavailable");auto queue=[device newCommandQueue];Require(queue,"Queue unavailable");
    NSString* source=@R"MSL(
      #include <metal_stdlib>
      using namespace metal;
      struct Vertex {float4 position [[position]];};
      vertex Vertex fullscreen(uint id [[vertex_id]]) {
        float2 p=id==0?float2(-1,-1):id==1?float2(3,-1):float2(-1,3);return {float4(p,0,1)};
      }
      fragment float4 seed(Vertex v [[stage_in]]) {
        float2 p=floor(v.position.xy/16.0);return float4(fract(p.x/32.0),fract(p.y/32.0),fract((p.x+p.y)/32.0),1);
      }
      float4 change(float4 c){return float4(c.b,c.r,c.g,c.a);}
      fragment float4 step(Vertex v [[stage_in]],texture2d<float> t [[texture(0)]]) {return change(t.read(uint2(v.position.xy)));}
      fragment float4 step_ms(Vertex v [[stage_in]],texture2d_ms<float> t [[texture(0)]]) {
        float4 c=0;for(uint i=0;i<t.get_num_samples();++i)c+=t.read(uint2(v.position.xy),i);return change(c/float(t.get_num_samples()));
      }
    )MSL";
    auto options=[MTLCompileOptions new];options.mathMode=MTLMathModeSafe;
    NSError* error=nil;auto library=[device newLibraryWithSource:source options:options error:&error];
    Require(library,error?error.localizedDescription.UTF8String:"Library failed");
    nlohmann::json report{{"source_revision",THEFT4_METAL_SOURCE_REVISION},{"gpu",device.name.UTF8String},
      {"scope","Alternating synthetic full-resolution render/store/sample passes; not complete game frame time"},
      {"width",width},{"height",height},{"passes_per_trial",passes},{"warmup_trials",4},
      {"cases",nlohmann::json::array()},{"passed",true}};
    for(auto format:{MTLPixelFormatRGBA8Unorm,MTLPixelFormatRGBA16Float})for(NSUInteger samples:{1ul,4ul}) {@autoreleasepool {
      if(![device supportsTextureSampleCount:samples])continue;
      auto seed=Pipeline(device,library,@"seed",format,samples);
      auto step=Pipeline(device,library,samples>1?@"step_ms":@"step",format,samples);
      std::array<std::array<id<MTLTexture>,2>,2> targets;
      for(size_t variant=0;variant<2;++variant)for(auto& target:targets[variant])target=Texture(device,format,samples,variant==0);
      std::array<std::vector<double>,2> times;
      for(size_t trial=0;trial<20;++trial)for(size_t order=0;order<2;++order) {@autoreleasepool {
        const auto variant=(trial+order)%2;auto command=[queue commandBuffer];
        Render(command,targets[variant][0],seed);
        for(NSUInteger pass=0;pass<passes;++pass)Render(command,targets[variant][1-pass%2],step,targets[variant][pass%2]);
        Wait(command);const auto ms=1000*(command.GPUEndTime-command.GPUStartTime);
        Require(ms>0,"GPU timestamps unavailable");if(trial>=4)times[variant].push_back(ms);
      }}
      const auto a=Read(device,queue,targets[0][0],format==MTLPixelFormatRGBA8Unorm?4:8);
      const auto b=Read(device,queue,targets[1][0],format==MTLPixelFormatRGBA8Unorm?4:8);
      Require(a==b,"Lossless usage change altered pixels");
      const auto pixel_bytes=format==MTLPixelFormatRGBA8Unorm?4:8;
      Require(a.size()>65*pixel_bytes&&std::memcmp(a.data(),a.data()+64*pixel_bytes,pixel_bytes)!=0,"Empty benchmark image");
      const auto baseline=Median(times[0]),candidate=Median(times[1]);
      report["cases"].push_back({{"format",format==MTLPixelFormatRGBA8Unorm?"RGBA8Unorm":"RGBA16Float"},{"samples",samples},
        {"baseline_pixel_format_view",true},{"candidate_pixel_format_view",false},{"byte_identical",true},
        {"baseline_median_gpu_ms",baseline},{"candidate_median_gpu_ms",candidate},
        {"reduction_percent",100*(baseline-candidate)/baseline},{"baseline_samples_ms",times[0]},{"candidate_samples_ms",times[1]}});
    }}
    std::ofstream output(argv[1]);Require(bool(output),"Report open failed");output<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';
  }}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
