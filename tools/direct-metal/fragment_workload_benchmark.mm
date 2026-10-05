// Exercise the actual expensive game fragment programs with deterministic
// synthetic inputs. This checks shader lowering, not whole-game performance.
#import <Metal/Metal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>
namespace {
constexpr NSUInteger W=1280,H=720,repetitions=128;
void Require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void Wait(id<MTLCommandBuffer> c){[c commit];[c waitUntilCompleted];Require(c.status==MTLCommandBufferStatusCompleted,c.error?c.error.localizedDescription.UTF8String:"GPU failed");}
double Median(std::vector<double> v){std::sort(v.begin(),v.end());return (v[(v.size()-1)/2]+v[v.size()/2])/2;}
id<MTLLibrary> Library(id<MTLDevice> d,const std::filesystem::path& path) {
  NSError* e=nil;auto l=[d newLibraryWithURL:[NSURL fileURLWithPath:@(path.c_str())] error:&e];
  Require(l,e?e.localizedDescription.UTF8String:"Library missing");return l;
}
std::vector<uint8_t> Read(id<MTLDevice> d,id<MTLCommandQueue> q,id<MTLTexture> t,NSUInteger stride) {
  const auto row=W*stride;auto b=[d newBufferWithLength:row*H options:MTLResourceStorageModeShared];Require(b,"Readback allocation");
  auto c=[q commandBuffer];auto e=[c blitCommandEncoder];
  [e copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(W,H,1)
    toBuffer:b destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*H];
  [e endEncoding];Wait(c);auto p=static_cast<const uint8_t*>(b.contents);return {p,p+row*H};
}
}
int main(int argc,char** argv) {
  if(argc!=4)return 2;
  nlohmann::json report{{"scope","Actual game fragment programs, synthetic uniforms/textures; not game frame time"},
    {"width",W},{"height",H},{"repetitions",repetitions},{"cases",nlohmann::json::array()},{"passed",false}};
  try {@autoreleasepool {
    auto d=MTLCreateSystemDefaultDevice();Require(d,"Metal unavailable");auto q=[d newCommandQueue];Require(q,"Queue unavailable");
    report["gpu"]=d.name.UTF8String;
    NSError* error=nil;auto options=[MTLCompileOptions new];options.mathMode=MTLMathModeSafe;
    auto vertex=[d newLibraryWithSource:@R"MSL(
      #include <metal_stdlib>
      using namespace metal;
      struct V {float4 p [[position]];float4 a [[user(locn0)]];float4 b [[user(locn1)]];
        float4 c [[user(locn2)]];float4 d [[user(locn3)]];float4 e [[user(locn4)]];float4 f [[user(locn16)]];};
      vertex V fullscreen(uint i [[vertex_id]]) {
        float2 p=i==0?float2(-1,-1):i==1?float2(3,-1):float2(-1,3);float2 uv=(p+1)*0.5;
        return {float4(p,0.5,1),float4(uv,1-uv),float4(.4,.6,.7,.8),float4(.3,.6,.2,.4),
          float4(.2,.4,.6,.8),float4(.6,.8,.2,.4),float4(.7,.6,.5,.4)};
      })MSL" options:options error:&error];Require(vertex,error?error.localizedDescription.UTF8String:"Vertex library");
    auto vs=[vertex newFunctionWithName:@"fullscreen"];
    std::array<id<MTLBuffer>,3> banks;for(auto& b:banks){b=[d newBufferWithLength:8192 options:MTLResourceStorageModeShared];Require(b,"Constant allocation");}
    std::array<id<MTLTexture>,7> textures;
    for(size_t t=0;t<textures.size();++t) {
      auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:64 height:64 mipmapped:NO];
      td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageShaderRead;textures[t]=[d newTextureWithDescriptor:td];Require(textures[t],"Input texture");
      std::vector<float> data(64*64*4);
      for(size_t y=0;y<64;++y)for(size_t x=0;x<64;++x)for(size_t c=0;c<4;++c)
        data[(y*64+x)*4+c]=.125f+float((x*3+y*5+c*11+t*7)%61)/96.f;
      [textures[t] replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:data.data() bytesPerRow:64*16];
    }
    auto sd=[MTLSamplerDescriptor new];sd.minFilter=sd.magFilter=MTLSamplerMinMagFilterLinear;
    sd.sAddressMode=sd.tAddressMode=MTLSamplerAddressModeClampToEdge;auto sampler=[d newSamplerStateWithDescriptor:sd];Require(sampler,"Sampler");
    struct Case{const char* name;const char* hash;uint32_t outputs;size_t textures;};
    for(auto test:{Case{"lit_sprite","fc2024b04fbb69bf",1,2},Case{"deferred_lighting","ff4b1ba4b364bcd3",1,7},
                  Case{"deferred_light_edges","a11692b372c58d98",4,2}}) {
      std::array<id<MTLFunction>,2> ps;
      for(size_t variant=0;variant<2;++variant) {
        auto lib=Library(d,std::filesystem::path(argv[variant+1])/(std::string(test.hash)+".metallib"));
        auto constants=[MTLFunctionConstantValues new];uint32_t spec=0;[constants setConstantValue:&spec type:MTLDataTypeUInt atIndex:0];
        ps[variant]=[lib newFunctionWithName:@"theft4_shader" constantValues:constants error:&error];Require(ps[variant],error?error.localizedDescription.UTF8String:"Fragment function");
      }
      // Match the observed game's attachment mask/format, plus a float32
      // output to detect arithmetic differences hidden by format conversion.
      for(auto format:{test.outputs==4?MTLPixelFormatRGBA8Unorm:MTLPixelFormatRGBA16Float,MTLPixelFormatRGBA32Float}) {
        const NSUInteger pixel_bytes=format==MTLPixelFormatRGBA8Unorm?4:format==MTLPixelFormatRGBA16Float?8:16;
        std::array<id<MTLRenderPipelineState>,2> pipelines;std::array<std::array<id<MTLTexture>,3>,2> targets;
        for(size_t v=0;v<2;++v) {
          auto p=[MTLRenderPipelineDescriptor new];p.vertexFunction=vs;p.fragmentFunction=ps[v];
          for(size_t a=0;a<3;++a)if(test.outputs&(1u<<a)) {
            p.colorAttachments[a].pixelFormat=format;
            auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:W height:H mipmapped:NO];
            td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageRenderTarget;targets[v][a]=[d newTextureWithDescriptor:td];Require(targets[v][a],"Target allocation");
          }
          pipelines[v]=[d newRenderPipelineStateWithDescriptor:p error:&error];Require(pipelines[v],error?error.localizedDescription.UTF8String:"Pipeline");
        }
        for(size_t seed=0;seed<3;++seed) {
          for(size_t b=0;b<3;++b) {
            auto f=static_cast<float*>(banks[b].contents);
            for(size_t i=0;i<2048;++i)f[i]=.125f+float((i*7+b*11+seed*13)%47)/64.f;
          }
          auto shared=static_cast<float*>(banks[2].contents);
          shared[744/4]=1.f/W;shared[748/4]=1.f/H;
          for(size_t i=752/4;i<856/4;++i)shared[i]=0; // Neutral mip biases.
          for(size_t target=0;target<4;++target)for(size_t i=0;i<4;++i){
            shared[(864+target*48)/4+i]=1;shared[(880+target*48)/4+i]=-65504;shared[(896+target*48)/4+i]=65504;
          }
          // Cover both particle premultiplication branches and different light constants.
          static_cast<float*>(banks[1].contents)[2080/4]=seed==0?0:1;
          std::array<std::vector<double>,2> times;
          for(size_t trial=0;trial<12;++trial)for(size_t order=0;order<2;++order) {@autoreleasepool {
            const size_t v=(trial+order)%2;auto c=[q commandBuffer];
            for(size_t repeat=0;repeat<repetitions;++repeat) {
              auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
              for(size_t a=0;a<3;++a)if(test.outputs&(1u<<a)){pass.colorAttachments[a].texture=targets[v][a];pass.colorAttachments[a].loadAction=MTLLoadActionClear;
                pass.colorAttachments[a].clearColor=MTLClearColorMake(.03125,.0625,.09375,.125);pass.colorAttachments[a].storeAction=MTLStoreActionStore;}
              auto e=[c renderCommandEncoderWithDescriptor:pass];Require(e,"Render encoder");[e setRenderPipelineState:pipelines[v]];
              for(size_t b=0;b<3;++b)[e setFragmentBuffer:banks[b] offset:0 atIndex:b];
              for(size_t t=0;t<test.textures;++t){[e setFragmentTexture:textures[t] atIndex:t];[e setFragmentSamplerState:sampler atIndex:t];}
              [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
            }
            Wait(c);double ms=1000*(c.GPUEndTime-c.GPUStartTime)/repetitions;Require(ms>0,"GPU timing missing");if(trial>=4)times[v].push_back(ms);
          }}
          bool varying=false;
          for(size_t a=0;a<3;++a)if(test.outputs&(1u<<a)) {
            auto first=Read(d,q,targets[0][a],pixel_bytes);
            auto second=Read(d,q,targets[1][a],pixel_bytes);Require(first==second,"Shader lowering changed output bytes");
            const size_t pixel=pixel_bytes;
            for(size_t i=pixel;i<first.size();i+=pixel)if(std::memcmp(first.data(),first.data()+i,pixel)){varying=true;break;}
          }
          Require(varying,"Fixture produced no spatial variation");
          auto a=Median(times[0]),b=Median(times[1]);report["cases"].push_back({{"shader",test.name},{"hash",test.hash},{"seed",seed},
            {"format",uint32_t(format)},{"pixels_equal",true},{"spatial_variation",varying},{"baseline_ms",a},{"candidate_ms",b},
            {"reduction_percent",100*(a-b)/a},{"baseline_trials_ms",times[0]},{"candidate_trials_ms",times[1]}});
          std::cout<<test.name<<" seed="<<seed<<" format="<<uint32_t(format)<<" baseline="<<a<<" candidate="<<b<<std::endl;
        }
      }
    }
    report["passed"]=true;
  }}catch(const std::exception& e){report["error"]=e.what();std::cerr<<e.what()<<'\n';}
  std::ofstream(argv[3])<<report.dump(2)<<'\n';return report["passed"].get<bool>()?0:1;
}
