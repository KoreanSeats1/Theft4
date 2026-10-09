// Actual game-shader preparation, exact sampler keys and escaped GPU ownership.
// Synthetic inputs: CPU timings must not be presented as saved gameplay time.
#include "theft4_metal_frame.h"
#include <algorithm>
#include <cassert>
#include <bit>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace theft4;
static render::Buffer Bytes(std::span<const uint8_t> data,uint64_t generation) {
  auto b=std::make_shared<render::Bytes>();b->generation=generation;b->value.assign(data.begin(),data.end());
  return {b,0,data.size()};
}
static render::Capture Fixture() {
  render::Capture c;c.width=c.height=8;auto& d=c.draw;
  d.pipeline.vertex.hash=0x048E49996734F6B5ull;d.pipeline.fragment.hash=0xB9589DA9F4B1770Full;
  d.pipeline.colors[0]=render::Format::RGBA8Unorm;
  d.pipeline.attributes={{0,0,0,render::VertexFormat::Float4},{17,0,16,render::VertexFormat::Float4},
                         {13,0,32,render::VertexFormat::Float4}};d.pipeline.streams[0]={48,false};
  d.vertex_count=3;d.viewport={0,0,8,8,0,1};d.scissor={0,0,8,8};
  const std::array<float,36> vertices{-1,-1,0.5,1,1,1,1,1,0,0,0,0,
    3,-1,0.5,1,1,1,1,1,0,0,0,0,-1,3,0.5,1,1,1,1,0,0,0,0};
  d.vertices[0]=Bytes({reinterpret_cast<const uint8_t*>(vertices.data()),sizeof(vertices)},100);
  for(size_t bank=0;bank<3;++bank) {
    std::vector<uint8_t> data(bank==0?4096:bank==1?3584:1056);
    if(bank==2)for(size_t target=0;target<4;++target) {
      const std::array<float,12> p{1,1,1,1,0,0,0,0,1,1,1,1};std::memcpy(data.data()+0x360+target*48,p.data(),48);
    }
    d.constants[bank]=Bytes(data,101+bank);
  }
  const std::array<uint8_t,4> texel{193,71,29,255};
  auto image=std::make_shared<render::Image>();image->format=render::Format::RGBA8Unorm;
  image->width=image->height=1;image->source=Bytes(texel,1000).source;image->mips={{0,0,1,1,1,4,4,0,4}};
  auto sampler=std::make_shared<render::Sampler>();sampler->max_lod_bits=std::bit_cast<uint32_t>(16.f);
  d.fetches[0]={image,sampler};return c;
}
static std::vector<uint8_t> Render(metal::Renderer& r,const metal::Draw& d,std::string& error) {
  auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:8 height:8 mipmapped:NO];
  td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageRenderTarget;
  auto target=r.Texture(td,error);assert(target);
  auto pd=[MTLRenderPassDescriptor renderPassDescriptor];pd.colorAttachments[0].texture=target;
  pd.colorAttachments[0].loadAction=MTLLoadActionClear;pd.colorAttachments[0].storeAction=MTLStoreActionStore;
  auto frame=r.BeginFrame(error);assert(frame&&frame.BeginPass(pd,error));
  assert(frame.Encode(d,error)&&frame.EndPass(error));auto receipt=frame.Submit(error);assert(receipt&&receipt.Wait(error));
  return r.ReadRGBA8(target,error);
}
int main(int argc,char** argv) {
  if(argc!=3)return 2;
  setenv("THEFT4_RETAIL_MODE","1",1);
  @autoreleasepool {
    metal::Renderer renderer;assert(renderer.Ready());std::string error;auto capture=Fixture();
    metal::Draw escaped;std::vector<uint8_t> expected;
    std::vector<double> repeated_ms,mixed_ms,sampler_repeated_ms,sampler_mixed_ms;
    {
      metal::PlanAdapter adapter(renderer);assert(adapter.Open(argv[1],error));
      assert(adapter.Prepare(capture,escaped,error));assert(!escaped.textures.empty()&&!escaped.samplers.empty());
      expected=Render(renderer,escaped,error);assert(expected.size()==256);
      assert(std::any_of(expected.begin(),expected.end(),[](uint8_t b){return b!=0;}));
      // Alternate shader interfaces through consecutive hits, ordinary cache
      // hits and depth-only draws. A previous textured interface must never
      // supply bindings to a different pipeline or survive a catalog reload.
      auto plain=capture;plain.draw.pipeline.fragment.hash=0x949ED69300FB92B7ull;
      auto depth=plain;depth.draw.pipeline.fragment={};depth.draw.pipeline.colors={};
      depth.draw.pipeline.depth=render::Format::Depth32Float;depth.draw.pipeline.depth_write=true;
      for(size_t round=0;round<3;++round) {
        for(size_t repeat=0;repeat<2;++repeat) {
          metal::Draw p;assert(adapter.Prepare(plain,p,error));assert(p.textures.empty()&&p.samplers.empty());
        }
        metal::Draw z;assert(adapter.Prepare(depth,z,error));assert(z.textures.empty()&&z.samplers.empty());
        auto missing=capture;missing.draw.pipeline.vertex.hash=0xfeedfeedfeedfeedull;
        assert(!adapter.Prepare(missing,z,error));
        metal::Draw textured;assert(adapter.Prepare(capture,textured,error));
        assert(Render(renderer,textured,error)==expected);
        assert(adapter.Open(argv[1],error));
        assert(adapter.Prepare(capture,textured,error));assert(Render(renderer,textured,error)==expected);
      }
      const render::Sampler base=*capture.draw.fetches[0].sampler;std::vector<render::Sampler> samplers{base};
      for(size_t field=0;field<10;++field) {
        auto s=base;
        switch(field) {
          case 0:s.min_linear=!s.min_linear;break;case 1:s.mag_linear=!s.mag_linear;break;
          case 2:s.mip_linear=!s.mip_linear;break;
          case 3:case 4:case 5:s.address[field-3]=base.address[field-3]==render::Address::Repeat?render::Address::ClampEdge:render::Address::Repeat;break;
          case 6:s.anisotropy=2;break;case 7:s.min_lod_bits=std::bit_cast<uint32_t>(0.5f);break;
          case 8:s.max_lod_bits=std::bit_cast<uint32_t>(8.f);break;case 9:s.opaque_white_border=!s.opaque_white_border;break;
        }
        assert(render::ValidateSampler(s,error));
        if(std::find(samplers.begin(),samplers.end(),s)==samplers.end())samplers.push_back(s);
      }
      std::vector<id<MTLSamplerState>> states;
      for(const auto& s:samplers){auto state=adapter.SamplerFor(s,error);assert(state);states.push_back(state);}
      assert(adapter.SamplerCount()==samplers.size());
      for(size_t i=0;i<samplers.size()*4;++i)assert(adapter.SamplerFor(samplers[i%samplers.size()],error)==states[i%samplers.size()]);
      assert(adapter.Open(argv[1],error));
      for(size_t i=0;i<samplers.size();++i)assert(adapter.SamplerFor(samplers[i],error)==states[i]);
      std::vector<render::Capture> captures;
      for(size_t i=0;i<samplers.size();++i) {
        auto c=capture;c.draw.fetches[0].sampler=std::make_shared<render::Sampler>(samplers[i]);
        captures.push_back(std::move(c));metal::Draw d;assert(adapter.Prepare(captures.back(),d,error));
      }
      for(size_t batch=0;batch<36;++batch) {@autoreleasepool {
        for(bool mixed:{false,true}) {
          const auto start=std::chrono::steady_clock::now();adapter.BeginUploadBatch();
          for(size_t i=0;i<4096;++i) {
            metal::Draw d;assert(adapter.Prepare(captures[mixed?i%captures.size():0],d,error));
            assert(d.textures.size()==escaped.textures.size()&&d.samplers.size()==escaped.samplers.size());
          }
          adapter.EndUploadBatch();const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
          if(batch>=4)(mixed?mixed_ms:repeated_ms).push_back(ms);
          const auto lookup_start=std::chrono::steady_clock::now();
          for(size_t i=0;i<65536;++i)assert(adapter.SamplerFor(samplers[mixed?i%samplers.size():0],error)==states[mixed?i%samplers.size():0]);
          const double lookup_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-lookup_start).count();
          if(batch>=4)(mixed?sampler_mixed_ms:sampler_repeated_ms).push_back(lookup_ms);
        }
      }}
      // Public validation and transaction semantics stay strict.
      auto bad=capture;auto s=std::make_shared<render::Sampler>(base);s->min_lod_bits=0x7f800000;
      bad.draw.fetches[0].sampler=s;const auto texture=escaped.textures.front().texture;
      assert(!adapter.Prepare(bad,escaped,error)&&escaped.textures.front().texture==texture);
      auto image=std::make_shared<render::Image>(*capture.draw.fetches[0].image);bad=capture;bad.draw.fetches[0].image=image;
      metal::Draw temp;assert(adapter.Prepare(bad,temp,error));image->width=0;
      assert(!adapter.Prepare(bad,temp,error));
      assert(adapter.Prepare(capture,escaped,error));capture={};captures.clear();
      adapter.RetireResources(false);
    }
    // The draw, then the command buffer, must retain bindings after all
    // adapter caches and original CPU sources have gone away.
    assert(Render(renderer,escaped,error)==expected);
    auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:8 height:8 mipmapped:NO];
    td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageRenderTarget;auto target=renderer.Texture(td,error);assert(target);
    auto pd=[MTLRenderPassDescriptor renderPassDescriptor];pd.colorAttachments[0].texture=target;
    pd.colorAttachments[0].loadAction=MTLLoadActionClear;pd.colorAttachments[0].storeAction=MTLStoreActionStore;
    auto frame=renderer.BeginFrame(error);assert(frame&&frame.BeginPass(pd,error)&&frame.Encode(escaped,error)&&frame.EndPass(error));
    escaped={};auto receipt=frame.Submit(error);assert(receipt&&receipt.Wait(error));
    assert(renderer.ReadRGBA8(target,error)==expected);
    // Exercise private admitted reuse through its only caller, including
    // more than 512 images, owner aliases and submission cleanup. A late
    // texture substitution must alter a visible pixel and fail this check.
    metal::FrameAdapter ordered(renderer);assert(ordered.Open(argv[1],error));
    auto surface=std::make_shared<render::Surface>();surface->key={9999,1};surface->width=surface->height=8;
    surface->format=render::Format::RGBA8Unorm;
    for(size_t round=0;round<3;++round) {@autoreleasepool {
      auto plan=std::make_shared<render::FramePlan>();plan->sequence=round+1;plan->surfaces={surface};
      render::Pass pass;render::Attachment a;a.view={surface->key,0,0,render::Aspect::Color};
      a.load=render::Load::Clear;a.store=render::Store::Store;pass.colors[0]=a;plan->output=a.view;
      auto prototype=Fixture();std::vector<std::shared_ptr<const render::Image>> images;
      for(size_t i=0;i<1024;++i) {
        auto image=std::make_shared<render::Image>(*prototype.draw.fetches[0].image);
        const std::array<uint8_t,4> pixel{uint8_t(i+round*23),uint8_t((i/4)+round*19),uint8_t((i/7)+round*11),255};
        image->source=Bytes(pixel,100000+round*1024+i).source;images.push_back(image);
      }
      for(size_t i=0;i<128;++i) {
        const auto image=images[i];auto owner=std::make_shared<std::shared_ptr<const render::Image>>(image);
        images.push_back(std::shared_ptr<const render::Image>(owner,image.get()));
      }
      std::array<std::array<uint8_t,3>,64> last_colors{};
      // Repeat the complete working set, in reverse, to exercise all slots
      // and the bounded collision/overflow fallback with no missing draws.
      for(size_t iteration=0;iteration<2;++iteration)for(size_t i=0;i<images.size();++i) {
        const auto index=iteration?images.size()-1-i:i;const auto& image=images[index];
        auto c=std::make_shared<render::Capture>(prototype);c->draw.fetches[0].image=image;
        c->draw.scissor={uint32_t(i%8),uint32_t((i/8)%8),1,1};
        std::copy_n(image->source->value.begin(),3,last_colors[i%64].begin());
        pass.commands.push_back(render::FrameDraw{c,{}});
      }
      plan->commands={std::move(pass)};
      auto submitted=ordered.Submit(plan,error);assert(submitted&&submitted.Wait(error));
      const auto pixels=renderer.ReadRGBA8(ordered.Output(*plan,error),error);assert(pixels.size()==256);
      for(size_t i=0;i<64;++i)for(size_t component=0;component<3;++component)
        assert(pixels[i*4+component]==last_colors[i][component]);
      // Rejection must not poison an already prepared, subsequent batch.
      auto invalid=std::make_shared<render::FramePlan>(*plan);auto& first=std::get<render::Pass>(invalid->commands[0]);
      first.colors[0]->resolve=first.colors[0]->view;assert(!ordered.Submit(invalid,error));
      submitted=ordered.Submit(plan,error);assert(submitted&&submitted.Wait(error));
      assert(renderer.ReadRGBA8(ordered.Output(*plan,error),error)==pixels);
      auto retained_output=ordered.Output(*plan,error);
      submitted=ordered.Submit(plan,error);assert(submitted);
      images.clear();plan.reset();invalid.reset();prototype={};ordered.RetireResources(false);
      assert(submitted.Wait(error)&&renderer.ReadRGBA8(retained_output,error)==pixels);
      if(round==1)assert(ordered.Open(argv[1],error));
    }}
    const auto summary=[](std::vector<double> s){std::sort(s.begin(),s.end());return nlohmann::json{{"median_ms",s[s.size()/2]},{"p95_ms",s[size_t(s.size()*0.95)]}};};
    nlohmann::json report{{"scope","Synthetic game-shader public admission/preparation, not gameplay FPS; strong escaped draw/GPU binding ownership"},
      {"gpu",renderer.Device().name.UTF8String},{"draws_per_batch",4096},{"sampler_lookups",65536},
      {"repeated_draws",summary(repeated_ms)},{"mixed_draws",summary(mixed_ms)},
      {"repeated_sampler",summary(sampler_repeated_ms)},{"mixed_sampler",summary(sampler_mixed_ms)},
      {"pixels",expected},{"escaped_draw_and_gpu_pixels_equal",true},{"invalid_inputs_rejected",true},
      {"admitted_image_working_set",1152},{"admitted_image_draws_per_round",2304},
      {"admitted_image_rounds",3},{"admitted_pixels_and_rejected_batch_recovery",true}};
    std::ofstream(argv[2])<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';
  }
}
