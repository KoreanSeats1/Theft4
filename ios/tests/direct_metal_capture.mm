#include "direct_metal_capture.h"
#include "theft4_metal_plan.h"
#include "native_color_output.h"
#import <ImageIO/ImageIO.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifndef THEFT4_METAL_SOURCE_REVISION
#define THEFT4_METAL_SOURCE_REVISION "development"
#endif
using namespace theft4::metal;
namespace {
struct Targets {
  MTLRenderPassDescriptor* pass;
  std::array<id<MTLTexture>,4> readable{};
};
class Replay {
 public:
  Renderer renderer{1};
  PlanAdapter adapter{renderer};
  ShaderStore preview_shaders{renderer};
  std::string error;
  void Require(bool okay) {
    if (!okay) throw std::runtime_error(error.empty() ? "Captured Metal draw validation failed" : error);
  }
  void Open(NSString* libraries) {
    Require(renderer.Ready()); Require(adapter.Open(libraries.UTF8String,error));
    Require(preview_shaders.Open(libraries.UTF8String,error));
  }
  std::shared_ptr<const theft4::render::Capture> Read(NSString* file) {
    auto capture=std::make_shared<theft4::render::Capture>();
    Require(theft4::render::ReadCapture(file.UTF8String,*capture,error));return capture;
  }
  Targets MakeTargets(const theft4::render::Capture& capture) {
    const auto& p=capture.draw.pipeline;
    size_t attachments=0;for(auto f:p.colors)if(f!=theft4::render::Format::Invalid)++attachments;
    if(p.depth!=theft4::render::Format::Invalid)++attachments;
    if(p.stencil!=theft4::render::Format::Invalid&&p.stencil!=p.depth)++attachments;
    // Conservative upper bound includes every MSAA attachment and resolve.
    if(uint64_t(capture.width)*capture.height*16*attachments*(p.samples+1)>512ull*1024*1024) {
      error="Captured pass exceeds the bounded replay attachment budget";Require(false);
    }
    const auto texture=[&](MTLPixelFormat format,NSUInteger samples,bool sampled) {
      auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
          width:capture.width height:capture.height mipmapped:NO];
      d.usage=MTLTextureUsageRenderTarget|(sampled ? MTLTextureUsageShaderRead : 0);
      d.storageMode=MTLStorageModePrivate;
      if(samples>1){d.textureType=MTLTextureType2DMultisample;d.sampleCount=samples;}
      auto t=renderer.Texture(d,error);Require(t);return t;
    };
    Targets result;result.pass=[MTLRenderPassDescriptor renderPassDescriptor];
    for(size_t i=0;i<4;++i)if(p.colors[i]!=theft4::render::Format::Invalid) {
      auto a=result.pass.colorAttachments[i];auto format=PlanAdapter::PixelFormat(p.colors[i]);
      a.texture=texture(format,p.samples,p.samples==1);a.loadAction=MTLLoadActionClear;
      a.clearColor=MTLClearColorMake(0.03125,0.0625,0.125,0);
      if(p.samples>1){a.resolveTexture=texture(format,1,true);a.storeAction=MTLStoreActionMultisampleResolve;
        result.readable[i]=a.resolveTexture;}else{a.storeAction=MTLStoreActionStore;result.readable[i]=a.texture;}
    }
    if(p.depth!=theft4::render::Format::Invalid) {
      auto a=result.pass.depthAttachment;a.texture=texture(PlanAdapter::PixelFormat(p.depth),p.samples,false);
      a.loadAction=MTLLoadActionClear;
      // The private draw capture has no preceding attachment contents. Seed
      // an empty depth target according to its comparison direction, keeping
      // the actual draw state unchanged. GTA IV uses reversed depth.
      a.clearDepth=(p.depth_compare==theft4::render::Compare::Greater ||
                    p.depth_compare==theft4::render::Compare::GreaterEqual) ? 0 : 1;
      a.storeAction=MTLStoreActionStore;
    }
    if(p.stencil!=theft4::render::Format::Invalid) {
      auto a=result.pass.stencilAttachment;
      a.texture=p.stencil==p.depth ? result.pass.depthAttachment.texture : texture(PlanAdapter::PixelFormat(p.stencil),p.samples,false);
      a.loadAction=MTLLoadActionClear;a.clearStencil=0;a.storeAction=MTLStoreActionStore;
    }
    return result;
  }
  double RunDraw(const std::shared_ptr<const theft4::render::Capture>& capture,Targets& targets) {
    auto draw=adapter.Realize(capture,error);Require(bool(draw));
    auto frame=renderer.BeginFrame(error);Require(bool(frame));Require(frame.BeginPass(targets.pass,error));
    Require(frame.Encode(*draw,error));Require(frame.EndPass(error));
    auto receipt=frame.Submit(error);Require(bool(receipt));Require(receipt.Wait(error));return receipt.GpuMilliseconds();
  }
  // Reuse tested stock fullscreen shaders to sample the rendered game target.
  // The preview stays on the GPU; no Vulkan driver or CPU image upload is used.
  void Copy(id<MTLTexture> source,id<MTLTexture> target,id<CAMetalDrawable> drawable=nil) {
    auto vertex=preview_shaders.Resolve({0x048E49996734F6B5ull,false},Stage::Vertex,0,error);Require(vertex.function);
    auto pixel=preview_shaders.Resolve({0xB9589DA9F4B1770Full,false},Stage::Fragment,0,error);Require(pixel.function);
    auto descriptor=[MTLRenderPipelineDescriptor new];descriptor.colorAttachments[0].pixelFormat=target.pixelFormat;
    auto declaration=[MTLVertexDescriptor new];
    for(auto [location,offset]:{std::pair{0u,0u},std::pair{17u,16u},std::pair{13u,32u}}) {
      auto a=declaration.attributes[location];a.format=MTLVertexFormatFloat4;a.offset=offset;a.bufferIndex=8;
    }
    declaration.layouts[8].stride=48;declaration.layouts[8].stepFunction=MTLVertexStepFunctionPerVertex;
    declaration.layouts[8].stepRate=1;descriptor.vertexDescriptor=declaration;
    auto pipeline=renderer.MakePipeline(vertex,pixel,descriptor,nil,error);Require(bool(pipeline));
    const float vertices[]{
      -1,-1,0.5f,1, 1,1,1,1, 0,1,0,0,  1,-1,0.5f,1, 1,1,1,1, 1,1,0,0,
       1, 1,0.5f,1, 1,1,1,1, 1,0,0,0, -1,-1,0.5f,1, 1,1,1,1, 0,1,0,0,
       1, 1,0.5f,1, 1,1,1,1, 1,0,0,0, -1, 1,0.5f,1, 1,1,1,1, 0,0,0,0};
    const auto buffer=[&](std::span<const uint8_t> bytes) {
      auto result=renderer.ImmutableBuffer(bytes,error);Require(result);return BufferView{result,0,bytes.size()};
    };
    theft4::metal::Draw draw;draw.pipeline=pipeline;draw.vertex_count=6;
    draw.vertices[0]=buffer({reinterpret_cast<const uint8_t*>(vertices),sizeof(vertices)});
    std::vector<uint8_t> vs(4096),ps(3584),shared(1056);
    const float scale=1;memcpy(shared.data()+744,&scale,4);memcpy(shared.data()+748,&scale,4);
    for(size_t i=0;i<4;++i) {
      rex::graphics::gta4_native::NativeColorOutputParameters output{};output.minimum.fill(0);output.maximum.fill(1);
      memcpy(shared.data()+0x360+i*sizeof(output),&output,sizeof(output));
    }
    draw.constants={buffer(vs),buffer(ps),buffer(shared)};
    auto sampler=[MTLSamplerDescriptor new];sampler.minFilter=MTLSamplerMinMagFilterNearest;
    sampler.magFilter=MTLSamplerMinMagFilterNearest;sampler.sAddressMode=MTLSamplerAddressModeClampToEdge;
    sampler.tAddressMode=MTLSamplerAddressModeClampToEdge;
    std::array<FetchResources,26> fetches{};fetches[0].images[0]=source;
    fetches[0].sampler=[renderer.Device() newSamplerStateWithDescriptor:sampler];Require(fetches[0].sampler);
    Require(preview_shaders.Bind({0xB9589DA9F4B1770Full,false},Stage::Fragment,fetches,draw,error));
    draw.viewport={0,0,double(target.width),double(target.height),0,1};draw.scissor={0,0,target.width,target.height};
    auto pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=target;
    pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
    auto frame=renderer.BeginFrame(error);Require(bool(frame));Require(frame.BeginPass(pass,error));
    Require(frame.Encode(draw,error));Require(frame.EndPass(error));if(drawable)Require(frame.Present(drawable,error));
    auto receipt=frame.Submit(error);Require(bool(receipt));Require(receipt.Wait(error));
  }
  std::vector<uint8_t> Pixels(id<MTLTexture> source) {
    auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:source.width height:source.height mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageRenderTarget;
    auto target=renderer.Texture(d,error);Require(target);Copy(source,target);
    auto bytes=renderer.ReadRGBA8(target,error);Require(!bytes.empty());return bytes;
  }
};
void SavePNG(NSString* path,const std::vector<uint8_t>& bytes,size_t width,size_t height) {
  auto color=CGColorSpaceCreateDeviceRGB();auto provider=CGDataProviderCreateWithData(nullptr,bytes.data(),bytes.size(),nullptr);
  auto image=CGImageCreate(width,height,8,32,width*4,color,CGBitmapInfo(kCGBitmapByteOrderDefault)|CGBitmapInfo(kCGImageAlphaLast),
      provider,nullptr,false,kCGRenderingIntentDefault);
  auto destination=CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path],CFSTR("public.png"),1,nullptr);
  if(image&&destination){CGImageDestinationAddImage(destination,image,nullptr);CGImageDestinationFinalize(destination);}
  if(destination)CFRelease(destination);if(image)CGImageRelease(image);CGDataProviderRelease(provider);CGColorSpaceRelease(color);
}
}
NSDictionary* ReplayDirectMetalCaptures(NSString* libraries,NSString* captures,NSString* output) {
  auto files=[NSFileManager.defaultManager subpathsAtPath:captures];NSMutableArray* paths=[NSMutableArray new];
  for(NSString* file in files)if([file.pathExtension isEqualToString:@"t4draw"])[paths addObject:[captures stringByAppendingPathComponent:file]];
  [paths sortUsingSelector:@selector(compare:)];NSMutableArray* results=[NSMutableArray new];size_t visible=0,passed=0;
  [NSFileManager.defaultManager createDirectoryAtPath:output withIntermediateDirectories:YES attributes:nil error:nil];
  for(NSString* path in paths) {
    if(results.count>=32)break;
    @autoreleasepool {
      NSMutableDictionary* item=[@{@"file":path.lastPathComponent,@"passed":@NO} mutableCopy];
      try {
        Replay replay;replay.Open(libraries);auto capture=replay.Read(path);
        auto targets=replay.MakeTargets(*capture);
        id<MTLTexture> color=nil;for(auto texture:targets.readable)if(texture){color=texture;break;}
        auto clear=replay.renderer.BeginFrame(replay.error);replay.Require(bool(clear));
        replay.Require(clear.BeginPass(targets.pass,replay.error));replay.Require(clear.EndPass(replay.error));
        auto cleared=clear.Submit(replay.error);replay.Require(bool(cleared));replay.Require(cleared.Wait(replay.error));
        auto baseline=color ? replay.Pixels(color) : std::vector<uint8_t>{};
        const auto first=replay.RunDraw(capture,targets);
        const auto cold=replay.adapter.ResourceStats();const auto pipelines=replay.adapter.PipelineCount();
        auto a=color ? replay.Pixels(color) : std::vector<uint8_t>{};
        const auto second=replay.RunDraw(capture,targets);const auto warm=replay.adapter.ResourceStats();
        auto b=color ? replay.Pixels(color) : std::vector<uint8_t>{};replay.Require(a==b);
        replay.Require(cold.buffer_creates==warm.buffer_creates&&cold.texture_creates==warm.texture_creates&&
                       cold.uploaded_bytes==warm.uploaded_bytes&&pipelines==replay.adapter.PipelineCount());
        size_t changed=0;for(size_t i=0;i<a.size();i+=4)
          if(std::abs(int(a[i])-int(baseline[i]))>1||std::abs(int(a[i+1])-int(baseline[i+1]))>1||
             std::abs(int(a[i+2])-int(baseline[i+2]))>1||std::abs(int(a[i+3])-int(baseline[i+3]))>1)++changed;
        if(changed){++visible;SavePNG([output stringByAppendingPathComponent:[path.lastPathComponent stringByAppendingString:@".png"]],a,capture->width,capture->height);}
        item[@"passed"]=@YES;item[@"visible_pixels"]=@(changed);item[@"width"]=@(capture->width);item[@"height"]=@(capture->height);
        item[@"indices"]=@(capture->draw.index_count);item[@"vertices"]=@(capture->draw.vertex_count);
        item[@"isolated_depth_seed"]=targets.pass.depthAttachment.texture ? @(targets.pass.depthAttachment.clearDepth) : [NSNull null];
        item[@"vertex_shader"]=[NSString stringWithFormat:@"%016llx",(unsigned long long)capture->draw.pipeline.vertex.hash];
        item[@"fragment_shader"]=[NSString stringWithFormat:@"%016llx",(unsigned long long)capture->draw.pipeline.fragment.hash];
        item[@"first_gpu_ms"]=@(first);item[@"repeat_gpu_ms"]=@(second);item[@"warm_resource_and_pipeline_reuse"]=@YES;
        if(changed)item[@"preview_capture"]=path;
        ++passed;
      }catch(const std::exception& e){item[@"failure"]=[NSString stringWithUTF8String:e.what()];}
      [results addObject:item];
    }
  }
  NSDictionary* report=@{@"schema":@1,@"available":@(paths.count>0),@"passed":@(passed>0&&passed==results.count),
    @"visible_draws":@(visible),@"cases":results,@"source_revision":@THEFT4_METAL_SOURCE_REVISION,
    @"vulkan_linked":@NO,@"moltenvk_linked":@NO,
    @"scope":@"Isolated real draw on cleared attachments. Depth seed follows comparison direction; original attachment contents are unavailable. Preceding pass contents and GPU-produced aliases require ordered frame integration."};
  auto json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil];
  [json writeToFile:[output stringByAppendingPathComponent:@"GAME_DRAW_REPLAY.json"] atomically:YES];return report;
}
bool PresentDirectMetalCapture(CAMetalLayer* layer,NSString* libraries,NSString* path,NSString** failure) {
  try {
    Replay replay;replay.Open(libraries);auto capture=replay.Read(path);auto targets=replay.MakeTargets(*capture);replay.RunDraw(capture,targets);
    id<MTLTexture> color=nil;for(auto t:targets.readable)if(t){color=t;break;}
    replay.Require(color);layer.device=replay.renderer.Device();layer.pixelFormat=MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly=YES;layer.drawableSize=CGSizeMake(512,512.0*capture->height/capture->width);
    auto drawable=[layer nextDrawable];replay.Require(drawable);replay.Copy(color,drawable.texture,drawable);return true;
  }catch(const std::exception& e){if(failure)*failure=[NSString stringWithUTF8String:e.what()];return false;}
}
