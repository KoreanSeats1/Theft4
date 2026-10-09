// Actual renderer, real Metal pixels and a persisted/reopened binary archive.
// Descriptor mutation after admission checks isolation of pipeline state.
#include "theft4_native_metal.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
using namespace theft4::metal;
static std::vector<uint8_t> Render(Renderer& renderer,const std::shared_ptr<const Pipeline>& pipeline) {
  std::string error;
  auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:4 height:4 mipmapped:NO];
  td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageRenderTarget;
  auto target=renderer.Texture(td,error);assert(target);
  auto pass=[MTLRenderPassDescriptor new];pass.colorAttachments[0].texture=target;
  pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
  auto frame=renderer.BeginFrame(error);assert(frame&&frame.BeginPass(pass,error));
  Draw draw;draw.pipeline=pipeline;draw.vertex_count=3;draw.viewport={0,0,4,4,0,1};draw.scissor={0,0,4,4};
  assert(frame.Encode(draw,error));assert(frame.EndPass(error));auto receipt=frame.Submit(error);assert(receipt&&receipt.Wait(error));
  auto pixels=renderer.ReadRGBA8(target,error);assert(pixels.size()==64);
  for(size_t i=0;i<pixels.size();i+=4)assert(pixels[i]==255&&pixels[i+1]==0&&pixels[i+2]==0&&pixels[i+3]==255);
  return pixels;
}
int main(int argc,char** argv) {
  if(argc!=2)return 2;setenv("THEFT4_RETAIL_MODE","0",1);
  @autoreleasepool {
    const auto root=std::filesystem::path(argv[1]);std::filesystem::create_directories(root);
    const auto path=(root/"pipeline.metalarc").string();std::filesystem::remove(path);
    std::string error;Renderer renderer;assert(renderer.Ready());renderer.ConfigurePipelineArchive(path);
    NSError* e=nil;
    auto library=[renderer.Device() newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal; vertex float4 vs(uint i [[vertex_id]]) {const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[i],0,1);} fragment float4 ps(){return float4(1,0,0,1);}" options:nil error:&e];assert(library);
    ShaderInterface interface;interface.constant_bytes={0,0,0};
    auto vs=renderer.LoadShader(library,Stage::Vertex,interface,0,error,"vs");
    auto ps=renderer.LoadShader(library,Stage::Fragment,interface,0,error,"ps");
    auto descriptor=[MTLRenderPipelineDescriptor new];descriptor.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
    auto pipeline=renderer.MakePipeline(vs,ps,descriptor,nil,error);assert(pipeline);
    const auto before=renderer.CompilationStats();assert(renderer.PipelineArchiveNeedsFlush());
    // Public descriptor is no longer authoritative after MakePipeline.
    descriptor.colorAttachments[0].pixelFormat=MTLPixelFormatInvalid;
    assert(Render(renderer,pipeline)==Render(renderer,pipeline));renderer.FlushPipelineArchive();
    assert(!renderer.PipelineArchiveNeedsFlush()&&std::filesystem::file_size(path)>0);
    const auto after=renderer.CompilationStats();renderer.FlushPipelineArchive();assert(renderer.CompilationStats().archive_ms==after.archive_ms);
    Renderer reopened;assert(reopened.Ready());reopened.ConfigurePipelineArchive(path);
    descriptor.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
    auto warm=reopened.MakePipeline(vs,ps,descriptor,nil,error);assert(warm&&reopened.PipelineArchiveHits()==1);
    assert(!reopened.PipelineArchiveNeedsFlush()&&Render(renderer,pipeline)==Render(reopened,warm));
    // Corrupt optional archive is discarded; normal rendering remains usable.
    const auto bad=(root/"corrupt.metalarc").string();std::ofstream(bad)<<"invalid archive";
    Renderer fallback;fallback.ConfigurePipelineArchive(bad);
    auto valid=fallback.MakePipeline(vs,ps,descriptor,nil,error);assert(valid);assert(Render(fallback,valid)==Render(renderer,pipeline));
    fallback.FlushPipelineArchive();
    nlohmann::json report{{"cold_pipeline_ms",before.pipeline_ms},{"archive_work_during_pipeline_ms",before.archive_ms},
      {"archive_work_at_flush_ms",after.archive_ms-before.archive_ms},{"warm_pipeline_ms",reopened.CompilationStats().pipeline_ms},
      {"warm_archive_hits",reopened.PipelineArchiveHits()},{"pixel_parity",true},{"pipeline_descriptor_isolated",true},
      {"corrupt_archive_fallback",true},{"scope","Host Metal fixture, not gameplay FPS"}};
    std::ofstream(root/"ARCHIVE.json")<<report.dump(2)<<'\n';
  }
}
