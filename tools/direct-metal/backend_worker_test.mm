#include "theft4_metal_backend.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <cassert>
#include <cstring>
#include <thread>
#include <iostream>
using namespace theft4;
std::shared_ptr<render::FramePlan> Clear(std::shared_ptr<const render::Surface> surface,
                                       uint64_t sequence,double red) {
  auto frame=std::make_shared<render::FramePlan>();frame->sequence=sequence;
  frame->surfaces.push_back(surface);render::Pass pass;render::Attachment attachment;
  attachment.view={surface->key,0,0,render::Aspect::Color};
  attachment.load=render::Load::Clear;attachment.store=render::Store::Store;
  attachment.clear_color={red,0.25,0.5,1};pass.colors[0]=attachment;
  frame->commands.push_back(pass);frame->output=attachment.view;return frame;
}
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  @autoreleasepool {
    auto backend=metal::CreateFrameBackend(nullptr,argv[1],2);
    auto caps=backend->Capabilities();assert(caps.max_image_dimension_2d>=8192);
    std::cout<<"GPU: "<<MTLCreateSystemDefaultDevice().name.UTF8String<<", sample mask="<<caps.sample_counts<<"\n";
    assert(caps.sample_counts&(1u<<1));assert(!backend->HasPresentation());
    std::string error;assert(!backend->Drain(error));assert(!error.empty());
    auto surface=std::make_shared<render::Surface>();surface->key={1,1};
    surface->width=32;surface->height=16;surface->format=render::Format::RGBA8Unorm;
    auto first=Clear(surface,1,0.125);assert(!backend->Submit(first,false,error));
    assert(backend->Open(error));assert(backend->Open(error));
    bool foreign_open=true,foreign_submit=true,foreign_drain=true;
    std::thread foreign([&] {
      std::string local;foreign_open=backend->Open(local);
      foreign_submit=backend->Submit(first,false,local);foreign_drain=backend->Drain(local);
    });foreign.join();assert(!foreign_open && !foreign_submit && !foreign_drain);
    assert(!backend->Submit(nullptr,false,error));assert(!backend->Submit(first,true,error));
    assert(backend->Submit(first,false,error));assert(backend->Drain(error));
    std::vector<uint8_t> pixels;assert(backend->ReadRGBA8(*first,*first->output,pixels,error));
    assert(pixels.size()==32*16*4);
    for(size_t i=0;i<pixels.size();i+=4){assert(pixels[i]==32);assert(pixels[i+1]==64);assert(pixels[i+2]==128);assert(pixels[i+3]==255);}
    // A rejected batch must not make its newly declared undefined allocation
    // available for a following LOAD, nor damage already submitted content.
    auto undefined=std::make_shared<render::Surface>(*surface);undefined->key={2,1};
    auto invalid=Clear(undefined,2,1);
    std::get<render::Pass>(invalid->commands[0]).colors[0]->load=render::Load::Load;
    assert(!backend->Submit(invalid,false,error));assert(!error.empty());
    assert(backend->ReadRGBA8(*first,*first->output,pixels,error));assert(pixels[0]==32);
    // Exact CPU owners stay alive for accepted work; submission admission
    // bounds retained plans and Drain retires all of them, even after rejections.
    std::vector<std::weak_ptr<const render::FramePlan>> owners;
    for(uint32_t frame=3;frame<35;++frame) {
      auto next=Clear(surface,frame,double(frame%4)/4);
      owners.push_back(next);assert(backend->Submit(next,false,error));next.reset();
      size_t retained=0;for(const auto& owner:owners)retained+=!owner.expired();assert(retained<=2);
    }
    assert(backend->Drain(error));for(const auto& owner:owners)assert(owner.expired());
    assert(backend->ReadRGBA8(*first,*first->output,pixels,error));assert(pixels[0]==128);
    // Internal flushes preserve previous attachment bytes for later partial clears.
    auto partial=Clear(surface,35,0);
    auto& pass=std::get<render::Pass>(partial->commands[0]);pass.colors[0]->load=render::Load::Load;
    render::RectClear rectangle;rectangle.colors=1;rectangle.rectangle={8,4,8,4};rectangle.color={1,0,0,1};
    pass.commands.push_back(rectangle);assert(backend->Submit(partial,false,error));
    assert(backend->ReadRGBA8(*partial,*partial->output,pixels,error));
    for(size_t y=0;y<16;++y)for(size_t x=0;x<32;++x) {
      size_t i=(y*32+x)*4;bool inside=x>=8&&x<16&&y>=4&&y<8;
      assert(pixels[i]==(inside?255:128));assert(pixels[i+1]==(inside?0:64));assert(pixels[i+2]==(inside?0:128));
    }
    auto joined=Clear(surface,36,0.5);
    render::Pass following;following.colors[0]=std::get<render::Pass>(joined->commands[0]).colors[0];
    following.colors[0]->load=render::Load::Load;
    rectangle.rectangle={0,0,16,16};rectangle.color={1,0,0,1};following.commands={rectangle};
    render::AppendPass(*joined,following);
    rectangle.rectangle={8,0,8,16};rectangle.color={0,1,0,1};following.commands={rectangle};
    render::AppendPass(*joined,std::move(following));assert(joined->commands.size()==1);
    assert(backend->Submit(joined,false,error));assert(backend->ReadRGBA8(*joined,*joined->output,pixels,error));
    for(size_t y=0;y<16;++y)for(size_t x=0;x<32;++x) {
      const size_t i=(y*32+x)*4;
      assert(pixels[i]==(x<8?255:x<16?0:128));
      assert(pixels[i+1]==(x<8?0:x<16?255:64));
      assert(pixels[i+2]==(x<16?0:128));assert(pixels[i+3]==255);
    }
    // Exercise the same offline host programs and 64-byte resolve ABI used by
    // the live title producer, including scaled color, exponent and MSAA.
    for(uint32_t samples:{1u,4u}) {
      if(!(caps.sample_counts&(1u<<samples)))continue;
      auto input=std::make_shared<render::Surface>(*surface);input->key={100+samples,1};input->samples=samples;
      auto output=std::make_shared<render::Surface>(*surface);output->key={200+samples,1};output->width=16;output->height=8;
      auto resolved=Clear(input,40+samples,0.25);resolved->surfaces.push_back(output);
      auto initialization=std::get<render::Pass>(Clear(output,40+samples,0)->commands[0]);
      resolved->commands.push_back(initialization);
      const std::array<uint32_t,16> constants{0,0,0,0,samples==1?0u:2u,samples==1?0u:2u,0,6,0,
          samples==1?0u:2u,0,4u|(1u<<8),32,16,16,8};
      auto bytes=std::make_shared<render::Bytes>();bytes->generation=40+samples;
      bytes->value.resize(sizeof(constants));std::memcpy(bytes->value.data(),constants.data(),sizeof(constants));
      render::HostDraw conversion;conversion.program=samples==1?render::HostProgram::Resolve:render::HostProgram::ResolveMSAA;
      conversion.pipeline.colors[0]=render::Format::RGBA8Unorm;conversion.constants={bytes,0,sizeof(constants)};
      conversion.scissor={0,0,16,8};conversion.fetches[0].produced=*resolved->output;
      conversion.fetches[0].sampler=std::make_shared<render::Sampler>();
      initialization.colors[0]->load=render::Load::Load;initialization.commands={conversion};
      render::AppendPass(*resolved,std::move(initialization));
      resolved->output=render::SurfaceView{output->key,0,0,render::Aspect::Color};
      assert(backend->Submit(resolved,false,error));assert(backend->ReadRGBA8(*resolved,*resolved->output,pixels,error));
      assert(pixels.size()==16*8*4);
      for(size_t i=0;i<pixels.size();i+=4) {
        assert(pixels[i]==128&&pixels[i+1]==128&&pixels[i+2]==255&&pixels[i+3]==255);
      }
    }
    const auto depth_surface=[&](uint64_t id,uint32_t samples=1) {
      auto s=std::make_shared<render::Surface>(*surface);s->key={id,1};s->samples=samples;
      s->format=render::Format::Depth32FloatStencil8;return s;
    };
    const auto depth_clear=[](const std::shared_ptr<render::Surface>& s,double depth,uint32_t stencil) {
      render::Attachment a;a.view={s->key,0,0,render::Aspect::Depth};a.load=render::Load::Clear;
      a.store=render::Store::Store;a.clear_depth=depth;render::Pass pass;pass.depth=a;
      a.view.aspect=render::Aspect::Stencil;a.clear_stencil=stencil;pass.stencil=a;return pass;
    };
    const auto inspect_depth=[&](std::shared_ptr<render::FramePlan> f,const std::shared_ptr<render::Surface>& s,
                                  const auto& expected,uint32_t mode=1,uint32_t swizzle=0x688) {
      auto rgba=std::make_shared<render::Surface>(*surface);rgba->key={s->key.id+1000,1};f->surfaces.push_back(rgba);
      auto clear=Clear(rgba,f->sequence,0);auto pass=std::get<render::Pass>(clear->commands[0]);
      const std::array<uint32_t,16> constants{0,0,0,0,0,0,0,0,mode,0,0,swizzle,0,0,0,0};
      auto bytes=std::make_shared<render::Bytes>();bytes->generation=f->sequence;bytes->value.resize(sizeof(constants));
      std::memcpy(bytes->value.data(),constants.data(),sizeof(constants));
      render::HostDraw pack;pack.program=render::HostProgram::PackedDepthAlias;
      pack.pipeline.colors[0]=rgba->format;pack.constants={bytes,0,sizeof(constants)};pack.scissor={0,0,32,16};
      pack.fetches[0].produced=render::SurfaceView{s->key,0,0,render::Aspect::Depth};
      pack.fetches[1].produced=render::SurfaceView{s->key,0,0,render::Aspect::Stencil};
      pack.fetches[0].sampler=pack.fetches[1].sampler=std::make_shared<render::Sampler>();
      pass.commands={pack};f->commands.push_back(pass);f->output=pass.colors[0]->view;
      assert(backend->Submit(f,false,error));assert(backend->ReadRGBA8(*f,*f->output,pixels,error));
      for(size_t y=0;y<16;++y)for(size_t x=0;x<32;++x) {
        const auto value=expected(x,y);for(size_t c=0;c<4;++c)assert(pixels[(y*32+x)*4+c]==value[c]);
      }
    };
    auto input_depth=depth_surface(300),copied_depth=depth_surface(301);
    auto ds=std::make_shared<render::FramePlan>();ds->sequence=50;ds->surfaces={input_depth,copied_depth};
    ds->commands={depth_clear(input_depth,0.25,37),depth_clear(copied_depth,1,19)};
    render::ImageCopy dsCopy{{input_depth->key,0,0,render::Aspect::Depth},
        {copied_depth->key,0,0,render::Aspect::Depth},{0,0},{0,0},{16,16},true};ds->commands.push_back(dsCopy);
    inspect_depth(ds,copied_depth,[](size_t x,size_t){return x<16?std::array<uint8_t,4>{37,0,0,208}:std::array<uint8_t,4>{19,0,0,240};});
    // The title's 0x60A fetch swizzle places stencil in blue; UNORM24 packing
    // must remain distinct from the float24 path above.
    ds=std::make_shared<render::FramePlan>();ds->sequence=51;ds->surfaces={input_depth,copied_depth};
    ds->commands={depth_clear(input_depth,0.25,37),depth_clear(copied_depth,1,19),dsCopy};
    inspect_depth(ds,copied_depth,[](size_t x,size_t){return x<16?std::array<uint8_t,4>{0,0,37,64}:
        std::array<uint8_t,4>{255,255,19,255};},0,0x60a);
    for(uint32_t samples:{1u,4u}) {
      if(!(caps.sample_counts&(1u<<samples)))continue;
      auto scene=depth_surface(310+samples,samples),resolved=depth_surface(320+samples);
      ds=std::make_shared<render::FramePlan>();ds->sequence=51+samples;ds->surfaces={scene,resolved};
      auto producer=depth_clear(scene,0.25,37);
      for(auto* aspect:{&*producer.depth,&*producer.stencil})if(samples>1) {
        aspect->store=render::Store::StoreAndResolve;aspect->filter=render::ResolveFilter::Sample0;
        aspect->resolve=render::SurfaceView{resolved->key,0,0,aspect->view.aspect};
      }
      ds->commands={producer};
      if(samples==1)ds->commands.push_back(render::ImageCopy{{scene->key,0,0,render::Aspect::Depth},
          {resolved->key,0,0,render::Aspect::Depth},{0,0},{0,0},{32,16},true});
      inspect_depth(ds,resolved,[](size_t,size_t){return std::array<uint8_t,4>{37,0,0,208};});
      for(bool rebuild:{false,true}) {
        auto forward=depth_surface(400+samples*2+rebuild,samples),result=depth_surface(420+samples*2+rebuild);
        ds=std::make_shared<render::FramePlan>();ds->sequence=60+samples*2+rebuild;ds->surfaces={resolved,forward,result};
        ds->commands={depth_clear(resolved,0.25,37)};
        render::Pass zero;zero.depth=depth_clear(resolved,0,0).depth;zero.depth->load=render::Load::Load;
        render::RectClear rectangle;rectangle.depth=true;rectangle.rectangle={0,0,16,16};rectangle.depth_value=0;
        zero.commands={rectangle};ds->commands.push_back(zero);
        auto handoff=depth_clear(forward,rebuild?0:1,rebuild?128:19);
        render::HostDraw transfer;transfer.program=rebuild?render::HostProgram::SceneDepthHandoff:render::HostProgram::DepthHandoff;
        transfer.pipeline.depth=transfer.pipeline.stencil=forward->format;transfer.pipeline.samples=samples;
        transfer.pipeline.depth_test=transfer.pipeline.depth_write=true;transfer.pipeline.depth_compare=render::Compare::Always;
        transfer.scissor={0,0,32,16};transfer.fetches[0].produced=render::SurfaceView{resolved->key,0,0,render::Aspect::Depth};
        transfer.fetches[0].sampler=std::make_shared<render::Sampler>();
        if(rebuild) {
          auto bytes=std::make_shared<render::Bytes>();bytes->generation=ds->sequence;bytes->value={1,0,0,0};
          transfer.constants={bytes,0,4};transfer.pipeline.stencil_test=true;
          transfer.pipeline.front.pass=transfer.pipeline.back.pass=render::StencilOp::Replace;
          transfer.stencil_front_reference=transfer.stencil_back_reference=255;
        }
        handoff.commands={transfer};
        if(samples>1)for(auto* aspect:{&*handoff.depth,&*handoff.stencil}) {
          aspect->store=render::Store::StoreAndResolve;aspect->filter=render::ResolveFilter::Sample0;
          aspect->resolve=render::SurfaceView{result->key,0,0,aspect->view.aspect};
        }
        ds->commands.push_back(handoff);
        if(samples==1)ds->commands.push_back(render::ImageCopy{{forward->key,0,0,render::Aspect::Depth},
            {result->key,0,0,render::Aspect::Depth},{0,0},{0,0},{32,16},true});
        inspect_depth(ds,result,[&](size_t x,size_t){return std::array<uint8_t,4>{uint8_t(rebuild?(x<16?128:255):19),0,0,uint8_t(x<16?0:208)};});
      }
    }
    auto noncolor=*partial->output;noncolor.aspect=render::Aspect::Depth;
    const auto saved=pixels;assert(!backend->ReadRGBA8(*partial,noncolor,pixels,error));assert(pixels==saved);
    backend->Close();assert(!backend->Submit(first,false,error));assert(!backend->Open(error));
    // Constructor may run on the UI thread; encoding/open/drain/close belong
    // to the render worker. The immutable capabilities remain available to HLE.
    auto second=metal::CreateFrameBackend(nullptr,argv[1],1);
    bool worker_ok=false;std::thread worker([&] {
      @autoreleasepool {
        std::string local;worker_ok=second->Open(local)&&second->Submit(first,false,local)&&second->Drain(local);
        second->Close();
      }
    });worker.join();assert(worker_ok);
    auto missing=metal::CreateFrameBackend(nullptr,"/nonexistent/theft4-metal-libraries");
    assert(!missing->Open(error));assert(!missing->Submit(first,false,error));missing->Close();
    CAMetalLayer* layer=[CAMetalLayer layer];layer.drawableSize=CGSizeMake(48,24);
    layer.pixelFormat=MTLPixelFormatBGRA8Unorm;layer.framebufferOnly=YES;
    auto presentation=metal::CreateFrameBackend((__bridge void*)layer,argv[1]);
    assert(presentation->HasPresentation());assert(presentation->Open(error));
    render::PresentationTarget target;assert(presentation->Target(target,error));
    assert(target.width==48&&target.height==24&&target.format==render::Format::BGRA8Unorm);
    assert(!presentation->Submit(first,true,error)); // Rejected before nextDrawable.
    layer.pixelFormat=MTLPixelFormatBGR10_XR;const auto target_saved=target;
    assert(!presentation->Target(target,error));assert(target.width==target_saved.width);
    presentation->Close();
    std::cout<<"Metal worker admission, ownership, joined passes, color/MSAA resolves, combined depth/stencil copy, sample-zero resolve, preserved/rebuilt handoff coverage and readback passed\n";
  }
}
