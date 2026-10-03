#include "theft4_metal_backend.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <cassert>
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
    std::cout<<"Metal worker admission, ownership, drain, clears, readback and layer contracts passed\n";
  }
}
