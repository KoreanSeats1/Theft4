#include "theft4_metal_backend.h"
#include "theft4_postfx_plan.h"
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
    render::SurfaceContents published;
    assert(backend->Submit(first,false,error,&published));assert(published.contains(*first->output));assert(backend->Drain(error));
    std::vector<uint8_t> pixels;assert(backend->ReadRGBA8(*first,*first->output,pixels,error));
    assert(pixels.size()==32*16*4);
    for(size_t i=0;i<pixels.size();i+=4){assert(pixels[i]==32);assert(pixels[i+1]==64);assert(pixels[i+2]==128);assert(pixels[i+3]==255);}
    // A rejected batch must not make its newly declared undefined allocation
    // available for a following LOAD, nor damage already submitted content.
    auto undefined=std::make_shared<render::Surface>(*surface);undefined->key={2,1};
    auto invalid=Clear(undefined,2,1);
    std::get<render::Pass>(invalid->commands[0]).colors[0]->load=render::Load::Load;
    const auto admitted=published;
    assert(!backend->Submit(invalid,false,error,&published));assert(!error.empty());assert(published==admitted);
    assert(backend->ReadRGBA8(*first,*first->output,pixels,error));assert(pixels[0]==32);
    // Ordinary submission releases the CPU command plan immediately while
    // Metal retains GPU resources and the backend retains surface identities.
    // Completion still produces correct pixels after all plan owners expire.
    std::vector<std::weak_ptr<const render::FramePlan>> owners;
    for(uint32_t frame=3;frame<35;++frame) {
      auto next=Clear(surface,frame,double(frame%4)/4);
      owners.push_back(next);assert(backend->Submit(next,false,error));next.reset();
      for(const auto& owner:owners)assert(owner.expired());
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
    {
      // Admission succeeds, then shader realization fails after a clear has
      // already been encoded. Uncommitted streaming work must leave both the
      // previous GPU bytes and published contents untouched.
      const auto before_pixels=pixels;
      auto aborted=Clear(surface,37,1);auto bad=std::make_shared<render::Capture>();
      bad->width=32;bad->height=16;auto& d=bad->draw;d.pipeline.vertex.hash=0xfedcba9876543210ull;
      d.pipeline.fragment.hash=0x949ed69300fb92b7ull;
      d.pipeline.colors[0]=surface->format;d.vertex_count=3;d.viewport={0,0,32,16,0,1};d.scissor={0,0,32,16};
      for(size_t bank=0;bank<3;++bank) {
        auto bytes=std::make_shared<render::Bytes>();bytes->generation=37+bank;
        bytes->value.resize(bank==0?4096:bank==1?3584:1056);d.constants[bank]={bytes,0,bytes->value.size()};
      }
      std::get<render::Pass>(aborted->commands[0]).commands.push_back(render::FrameDraw{bad,{}});
      render::SurfaceContents admitted_final;
      if(!render::ValidateFrame(*aborted,{},admitted_final,error)){std::cerr<<error<<'\n';return 1;}
      const auto before_published=published;
      assert(!backend->Submit(aborted,false,error,&published));assert(error.find("shader")!=std::string::npos);
      assert(published==before_published);
      assert(backend->ReadRGBA8(*first,*first->output,pixels,error));assert(pixels==before_pixels);
    }
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
    // The title resolves floor(height/2) 4x views from odd-height 1x
    // writers. A distinct final row must not stretch or bleed into the crop.
    {
      auto input=std::make_shared<render::Surface>(*surface);input->key={590,1};input->height=17;
      auto output=std::make_shared<render::Surface>(*surface);output->key={591,1};output->width=16;output->height=8;
      auto f=Clear(input,79,0);f->surfaces.push_back(output);
      auto& producer=std::get<render::Pass>(f->commands[0]);producer.colors[0]->clear_color={0,1,0,1};
      render::RectClear last;last.colors=1;last.rectangle={0,16,32,1};last.color={1,0,1,1};producer.commands={last};
      auto pass=std::get<render::Pass>(Clear(output,79,0)->commands[0]);
      const std::array<uint32_t,16> constants{0,0,0,0,0,2,0,6,0,0,0,4,32,16,16,8};
      auto bytes=std::make_shared<render::Bytes>();bytes->generation=79;bytes->value.resize(sizeof(constants));
      std::memcpy(bytes->value.data(),constants.data(),sizeof(constants));
      render::HostDraw draw;draw.program=render::HostProgram::Resolve;draw.pipeline.colors[0]=output->format;
      draw.constants={bytes,0,sizeof(constants)};draw.scissor={0,0,16,8};
      draw.fetches[0].produced=*f->output;draw.fetches[0].sampler=std::make_shared<render::Sampler>();
      pass.commands={draw};f->commands.push_back(pass);f->output=pass.colors[0]->view;
      assert(backend->Submit(f,false,error));assert(backend->ReadRGBA8(*f,*f->output,pixels,error));
      for(size_t i=0;i<pixels.size();i+=4)assert(pixels[i]==0&&pixels[i+1]==255&&pixels[i+2]==0&&pixels[i+3]==255);
      std::cout<<"Odd-height sample-plane resolve excluded the unwritten crop row.\n";
    }
    // Placement materialization uses mode 1 of the stock conversion ABI. Check
    // spatially distinct pixels across physical resizes and coherent sample
    // families, then load that allocation for a partial following write.
    uint64_t placement_case=0;
    for(uint32_t source_samples:{1u,2u,4u})for(uint32_t destination_samples:{1u,2u,4u}) {
      if(!(caps.sample_counts&(1u<<source_samples))||!(caps.sample_counts&(1u<<destination_samples)))continue;
      for(bool resized:{false,true}) {
        // Different guest views describe the same sample-space footprint.
        // A physical resize uses the existing area filter; identical physical
        // layouts retain each sample through direct materialization.
        if(!resized&&source_samples!=destination_samples)continue;
        const uint32_t source_type=source_samples==4?2:source_samples==2?1:0;
        const uint32_t destination_type=destination_samples==4?2:destination_samples==2?1:0;
        const uint32_t destination_width=resized?(source_samples==4?64u:32u)/(destination_samples==4?2u:1u):32u;
        const uint32_t destination_height=resized?(source_samples>1?32u:16u)/(destination_samples>1?2u:1u):16u;
        const bool scaled=destination_width!=32||destination_height!=16;
        const uint64_t id=600+4*++placement_case;
        auto input=std::make_shared<render::Surface>(*surface);input->key={id,1};input->samples=source_samples;
        auto placed=std::make_shared<render::Surface>(*surface);placed->key={id+1,1};placed->samples=destination_samples;
        placed->width=destination_width;placed->height=destination_height;
        auto output=std::make_shared<render::Surface>(*placed);output->key={id+2,1};output->samples=1;
        auto f=Clear(input,80+placement_case,0);f->surfaces.push_back(placed);
        auto& producer=std::get<render::Pass>(f->commands[0]);producer.colors[0]->clear_color={0,0,1,1};
        render::RectClear mark;mark.colors=1;mark.rectangle={0,0,16,16};mark.color={1,0,0,1};producer.commands={mark};
        mark.rectangle={0,8,32,8};mark.color={0,1,0,1};producer.commands.push_back(mark);
        const std::array<uint32_t,16> constants{0,0,0,0,source_type,destination_type,destination_type,0,1,
            source_type,destination_type,scaled?4u:2u,32,16,destination_width,destination_height};
        auto bytes=std::make_shared<render::Bytes>();bytes->generation=f->sequence;bytes->value.resize(sizeof(constants));
        std::memcpy(bytes->value.data(),constants.data(),sizeof(constants));
        render::HostDraw materialize;materialize.program=source_samples==1?render::HostProgram::Resolve:render::HostProgram::ResolveMSAA;
        materialize.pipeline.colors[0]=placed->format;materialize.pipeline.samples=destination_samples;
        materialize.constants={bytes,0,sizeof(constants)};materialize.scissor={0,0,destination_width,destination_height};
        materialize.fetches[0].produced=*f->output;materialize.fetches[0].sampler=std::make_shared<render::Sampler>();
        auto transfer=std::get<render::Pass>(Clear(placed,f->sequence,0)->commands[0]);transfer.commands={materialize};
        f->commands.push_back(transfer);
        render::Pass following;following.colors[0]=transfer.colors[0];following.colors[0]->load=render::Load::Load;
        mark.rectangle={0,0,destination_width/4,destination_height/4};mark.color={1,1,0,1};following.commands={mark};
        if(destination_samples>1) {
          f->surfaces.push_back(output);following.colors[0]->store=render::Store::StoreAndResolve;
          following.colors[0]->resolve=render::SurfaceView{output->key,0,0,render::Aspect::Color};
        }
        render::AppendPass(*f,std::move(following));
        f->output=render::SurfaceView{destination_samples>1?output->key:placed->key,0,0,render::Aspect::Color};
        assert(backend->Submit(f,false,error));assert(backend->ReadRGBA8(*f,*f->output,pixels,error));
        for(size_t y=0;y<destination_height;++y)for(size_t x=0;x<destination_width;++x) {
          const bool yellow=x<destination_width/4&&y<destination_height/4;
          const bool green=y>=destination_height/2,red=x<destination_width/2&&!green;
          const std::array<uint8_t,4> expected{uint8_t(yellow||red?255:0),uint8_t(yellow||green?255:0),
              uint8_t(!yellow&&!red&&!green?255:0),255};
          for(size_t c=0;c<4;++c)assert(pixels[(y*destination_width+x)*4+c]==expected[c]);
        }
      }
    }
    std::cout<<"Placement mode-1 materialization and following partial LOAD passed: "<<placement_case<<" cases\n";
    // A title-shader triangle with a half-pixel vertical edge creates different covered samples at its
    // edge. Direct materialization must preserve those samples, rather than
    // broadcasting source sample zero. Compare independently resolved pixels.
    for(uint32_t samples:{2u,4u}) {
      if(!(caps.sample_counts&(1u<<samples)))continue;
      const uint64_t id=800+samples*4;
      auto input=std::make_shared<render::Surface>(*surface);input->key={id,1};input->samples=samples;
      auto placed=std::make_shared<render::Surface>(*input);placed->key={id+1,1};
      auto baseline=std::make_shared<render::Surface>(*surface);baseline->key={id+2,1};
      auto output=std::make_shared<render::Surface>(*surface);output->key={id+3,1};
      auto f=Clear(input,110+samples,0);f->surfaces={input,placed,baseline,output};
      auto c=std::make_shared<render::Capture>();c->width=32;c->height=16;auto& d=c->draw;
      d.pipeline.vertex.hash=0x048E49996734F6B5ull;d.pipeline.fragment.hash=0x949ED69300FB92B7ull;
      d.pipeline.colors[0]=input->format;d.pipeline.samples=samples;
      d.pipeline.attributes={{0,0,0,render::VertexFormat::Float4},{17,0,16,render::VertexFormat::Float4},
                             {13,0,32,render::VertexFormat::Float4}};d.pipeline.streams[0]={48,false};
      const auto buffer=[](std::span<const uint8_t> data,uint64_t generation) {
        auto bytes=std::make_shared<render::Bytes>();bytes->generation=generation;bytes->value.assign(data.begin(),data.end());
        return render::Buffer{bytes,0,data.size()};
      };
      for(size_t bank=0;bank<3;++bank) {
        std::vector<uint8_t> data(bank==0?4096:bank==1?3584:1056);
        if(bank==2) {
          const float one=1;std::memcpy(data.data()+744,&one,4);std::memcpy(data.data()+748,&one,4);
          std::memcpy(data.data()+740,&samples,4);
          for(size_t target=0;target<4;++target) {
            const std::array<float,12> output_parameters{1,1,1,1,0,0,0,0,1,1,1,1};
            std::memcpy(data.data()+0x360+target*48,output_parameters.data(),48);
          }
        }
        d.constants[bank]=buffer(data,id*10+bank);
      }
      const std::array<float,36> vertices{-1,-1,0.5,1, 1,1,1,1, 0,0,0,0,
          -0.34375,-1,0.5,1, 1,1,1,1, 0,0,0,0, -0.34375,1,0.5,1, 1,1,1,1, 0,0,0,0};
      d.vertices[0]=buffer({reinterpret_cast<const uint8_t*>(vertices.data()),sizeof(vertices)},id*10+3);
      d.vertex_count=3;d.viewport={0,0,32,16,0,1};d.scissor={0,0,32,16};
      if(samples==2) {
        auto attachmentless=std::make_shared<render::FramePlan>();attachmentless->sequence=119;
        auto draw=std::make_shared<render::Capture>(*c);draw->draw.pipeline.colors={};
        draw->draw.pipeline.samples=1;
        render::Pass pass;pass.attachmentless_extent={32,16};pass.commands={render::FrameDraw{draw,{}}};
        attachmentless->commands={pass};
        assert(backend->Submit(attachmentless,false,error));assert(backend->Drain(error));
        std::cout<<"Attachmentless game-shader draw completed without surface allocations.\n";
      }
      auto& producer=std::get<render::Pass>(f->commands[0]);producer.colors[0]->clear_color={0,0,0,1};
      producer.colors[0]->store=render::Store::StoreAndResolve;
      producer.colors[0]->resolve=render::SurfaceView{baseline->key,0,0,render::Aspect::Color};
      producer.commands={render::FrameDraw{c,{}}};
      const uint32_t sample_type=samples==4?2:1;
      const std::array<uint32_t,16> constants{0,0,0,0,sample_type,sample_type,sample_type,0,1,sample_type,sample_type,2,32,16,32,16};
      render::HostDraw copy;copy.program=render::HostProgram::ResolveMSAA;copy.pipeline.colors[0]=placed->format;
      copy.pipeline.samples=samples;copy.scissor={0,0,32,16};
      copy.constants=buffer({reinterpret_cast<const uint8_t*>(constants.data()),sizeof(constants)},id*10+4);
      copy.fetches[0].produced=render::SurfaceView{input->key,0,0,render::Aspect::Color};
      copy.fetches[0].sampler=std::make_shared<render::Sampler>();
      auto transfer=std::get<render::Pass>(Clear(placed,f->sequence,0)->commands[0]);transfer.commands={copy};
      transfer.colors[0]->store=render::Store::StoreAndResolve;
      transfer.colors[0]->resolve=render::SurfaceView{output->key,0,0,render::Aspect::Color};f->commands.push_back(transfer);
      f->output=render::SurfaceView{output->key,0,0,render::Aspect::Color};
      assert(backend->Submit(f,false,error));assert(backend->ReadRGBA8(*f,*f->output,pixels,error));
      std::vector<uint8_t> expected;assert(backend->ReadRGBA8(*f,{baseline->key,0,0,render::Aspect::Color},expected,error));
      assert(expected==pixels);size_t partial_samples=0;
      for(size_t i=0;i<expected.size();i+=4)partial_samples+=expected[i]>0&&expected[i]<255;
      assert(partial_samples>0);
      // Release every CPU draw/payload owner immediately after encoding. The
      // driver-owned immutable buffers must remain valid until GPU completion.
      auto ephemeral=std::make_shared<render::FramePlan>(*f);
      auto transient=std::make_shared<render::Capture>(*c);
      transient->draw.vertices[0].source=std::make_shared<render::Bytes>(*c->draw.vertices[0].source);
      for(size_t bank=0;bank<3;++bank)
        transient->draw.constants[bank].source=std::make_shared<render::Bytes>(*c->draw.constants[bank].source);
      std::weak_ptr<const render::Capture> transientOwner=transient;
      std::weak_ptr<const render::Bytes> payloadOwner=transient->draw.vertices[0].source;
      std::get<render::Pass>(ephemeral->commands[0]).commands={render::FrameDraw{transient,{}}};
      transient.reset();assert(backend->Submit(ephemeral,false,error));ephemeral.reset();
      assert(transientOwner.expired()&&payloadOwner.expired());
      assert(backend->ReadRGBA8(*f,*f->output,pixels,error));assert(expected==pixels);
      // Live frontend may discard a malformed title draw, but the GPU backend
      // must never admit it or alter already-presented content. Classification
      // is not permission to bypass immutable buffer bounds.
      auto invalid=std::make_shared<render::Capture>(*c);invalid->draw.vertices[0].length=16;
      render::DrawValidationIssue issue;
      assert(!render::Validate(*invalid,error,&issue)&&issue==render::DrawValidationIssue::VertexRange);
      auto rejected=Clear(output,120+samples,1);auto& invalid_pass=std::get<render::Pass>(rejected->commands[0]);
      invalid->draw.pipeline.samples=1;invalid_pass.commands={render::FrameDraw{invalid,{}}};
      assert(!backend->Submit(rejected,false,error));
      std::vector<uint8_t> unchanged;assert(backend->ReadRGBA8(*f,*f->output,unchanged,error));assert(unchanged==pixels);
      std::cout<<"Direct per-sample materialization preserved "<<samples<<"x title-shader edges: "<<partial_samples<<" partially covered pixels\n";
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
    // Exercise the exact utility builder used by the live title producer. Odd
    // scene dimensions require rounded-up half targets; every SMAA quality
    // uses the shipped lookup bytes and its actual Metal host programs.
    uint64_t utility_id=10000,utility_generation=10000;
    for(bool dof:{false,true})for(int quality=-1;quality<4;++quality) {
      const auto allocate=[&](std::string_view,uint32_t w,uint32_t h,render::Format format) {
        auto result=std::make_shared<render::Surface>();result->key={++utility_id,1};
        result->width=w;result->height=h;result->format=format;
        return render::SurfaceOwner(result);
      };
      auto scene=allocate("scene",33,17,render::Format::RGBA8Unorm);
      auto mask=allocate("mask",33,17,render::Format::RGBA8Unorm);
      auto depth=allocate("depth",33,17,render::Format::RGBA8Unorm);
      auto output=allocate("output",19,11,render::Format::RGBA8Unorm);
      auto frame=Clear(scene,++utility_generation,0.25);frame->surfaces.push_back(mask);frame->surfaces.push_back(depth);
      frame->commands.push_back(Clear(mask,frame->sequence,0)->commands[0]);
      frame->commands.push_back(Clear(depth,frame->sequence,0)->commands[0]);
      render::SurfaceContents contents,validated;assert(render::ValidateFrame(*frame,{},contents,error));
      const auto fetch=[](render::SurfaceOwner owner) {
        render::HostFetch result;result.produced=render::SurfaceView{owner->key,0,0,render::Aspect::Color};
        result.sampler=std::make_shared<render::Sampler>();return result;
      };
      render::SplitConstants split;split.distance={1,1,1,0.5};if(dof)split.blur={1,0.25,1,0};
      assert(render::AppendSplitPostFx(*frame,contents,scene,fetch(depth),fetch(mask),split,allocate,utility_generation,error));
      size_t split_count=0;
      for(const auto& command:frame->commands)if(auto pass=std::get_if<render::Pass>(&command))
        for(const auto& draw:pass->commands)if(auto host=render::GetHostDraw(draw)) {
          if(host->program!=render::HostProgram::SplitPostFx)continue;++split_count;
          render::SplitConstants c;std::memcpy(&c,host->constants.source->value.data(),sizeof(c));
          if(c.pass==1||c.pass==2)assert(c.destination[0]==17&&c.destination[1]==9);
        }
      assert(split_count==(dof?4:1));
      render::SunConstants sun;sun.density=0.9;sun.decay=0.95;sun.screen={0.5,0.5,1,1};
      sun.color_and_sky_start={1,1,1,0.9};sun.sky_end={1,0,0,0};
      assert(render::AppendSunShafts(*frame,contents,scene,fetch(depth),sun,allocate,utility_generation,error));
      render::PresentConstants present;present.source_width=33;present.source_height=17;
      present.destination_width=19;present.destination_height=11;present.output_mode=4;
      const auto lookups=render::MakeSmaaLookups(utility_generation);
      auto presentation_source=fetch(scene);
      if(quality==0)presentation_source.produced->swizzle={render::Swizzle::Blue,render::Swizzle::Green,render::Swizzle::Red,render::Swizzle::Alpha};
      assert(render::AppendPresentation(*frame,contents,presentation_source,output,present,quality,lookups,allocate,utility_generation,error));
      assert(render::ValidateFrame(*frame,{},validated,error));
      if(!backend->Submit(frame,false,error)){std::cerr<<"Utility submission: "<<error<<"\n";assert(false);}
      assert(backend->ReadRGBA8(*frame,*frame->output,pixels,error));assert(pixels.size()==19*11*4);
      for(size_t i=0;i<pixels.size();i+=4)for(size_t channel=0;channel<4;++channel) {
        const int expected=channel==3?255:channel==size_t(quality==0?0:2)?128:64;
        assert(std::abs(int(pixels[i+channel])-expected)<=1);
      }
    }
    // A bright isolated pixel is filtered only when the real stipple mask
    // authorizes it. This checks non-uniform image work, not just pass counts.
    for(bool enabled:{false,true}) {
      const auto allocate=[&](std::string_view,uint32_t w,uint32_t h,render::Format format) {
        auto result=std::make_shared<render::Surface>();result->key={++utility_id,1};
        result->width=w;result->height=h;result->format=format;return render::SurfaceOwner(result);
      };
      auto scene=allocate("scene",33,17,render::Format::RGBA8Unorm);
      auto mask=allocate("mask",33,17,render::Format::RGBA8Unorm);
      auto frame=Clear(scene,++utility_generation,0.25);frame->surfaces.push_back(mask);
      render::RectClear impulse;impulse.colors=1;impulse.rectangle={16,8,1,1};impulse.color={1,1,1,1};
      std::get<render::Pass>(frame->commands[0]).commands.push_back(impulse);
      auto mask_pass=std::get<render::Pass>(Clear(mask,frame->sequence,0)->commands[0]);
      mask_pass.colors[0]->clear_color[3]=enabled?1:0;frame->commands.push_back(mask_pass);
      render::HostFetch input;input.produced=render::SurfaceView{mask->key,0,0,render::Aspect::Color};
      input.sampler=std::make_shared<render::Sampler>();render::SurfaceContents contents;
      assert(render::ValidateFrame(*frame,{},contents,error));
      assert(render::AppendSplitPostFx(*frame,contents,scene,input,input,{},allocate,utility_generation,error));
      assert(backend->Submit(frame,false,error));assert(backend->ReadRGBA8(*frame,*frame->output,pixels,error));
      const size_t center=(8*33+16)*4;
      assert(pixels[center]==(enabled?64:255));assert(pixels[center+3]==255);
    }
    std::cout<<"Live effects builder: zero/full DOF, odd extents, sun chain, all SMAA qualities, scaled presentation and masked stipple passed\n";
    // Guest readback preserves native storage (BGRA, packed 10-bit and float)
    // instead of passing through the display-oriented RGBA8 conversion.
    for(auto format:{render::Format::BGRA8Unorm,render::Format::R16Unorm,
                    render::Format::R32Float,render::Format::RGBA16Float,render::Format::RGB10A2Unorm}) {
      auto owner=std::make_shared<render::Surface>();owner->key={++utility_id,1};
      owner->width=33;owner->height=17;owner->levels=3;owner->format=format;
      auto frame=Clear(owner,++utility_generation,0.25);
      auto& clear=std::get<render::Pass>(frame->commands[0]);clear.colors[0]->view.level=2;
      frame->output=clear.colors[0]->view;
      assert(backend->Submit(frame,false,error));render::ColorReadback raw;
      assert(backend->ReadColor(*frame,*frame->output,raw,error));
      assert(raw.width==8&&raw.height==4&&raw.format==format&&raw.bytes.size()==raw.row_bytes*4);
      const auto pixel_bytes=raw.row_bytes/8;
      for(size_t at=0;at<raw.bytes.size();at+=pixel_bytes) {
        const auto* p=raw.bytes.data()+at;
        if(format==render::Format::BGRA8Unorm)assert(p[0]==128&&p[1]==64&&p[2]==64&&p[3]==255);
        if(format==render::Format::R16Unorm){uint16_t v;std::memcpy(&v,p,2);assert(v==16384);}
        if(format==render::Format::R32Float){float v;std::memcpy(&v,p,4);assert(v==0.25f);}
        if(format==render::Format::RGBA16Float){std::array<uint16_t,4> v;std::memcpy(v.data(),p,8);assert((v==std::array<uint16_t,4>{0x3400,0x3400,0x3800,0x3c00}));}
        if(format==render::Format::RGB10A2Unorm){uint32_t v;std::memcpy(&v,p,4);assert(v==(256u|(256u<<10)|(512u<<20)|(3u<<30)));}
      }
      const auto saved=raw;auto undefined=*frame->output;undefined.level=1;
      assert(!backend->ReadColor(*frame,undefined,raw,error));assert(raw.bytes==saved.bytes&&raw.format==saved.format);
      if(format==render::Format::BGRA8Unorm) {
        assert(backend->ReadRGBA8(*frame,*frame->output,pixels,error));
        assert(pixels[0]==64&&pixels[1]==64&&pixels[2]==128&&pixels[3]==255);
      }
    }
    std::cout<<"Native color storage readback: BGRA, R16, R32F, RGBA16F, RGB10A2, mip selection and undefined rejection passed\n";
    auto noncolor=*partial->output;noncolor.aspect=render::Aspect::Depth;
    const auto saved=pixels;assert(!backend->ReadRGBA8(*partial,noncolor,pixels,error));assert(pixels==saved);
    // Earlier clear stores are dead only when overwritten before a read. The
    // final source store must survive its copy, and final destination survives.
    auto a=std::make_shared<render::Surface>(*surface);a->key={9001,1};
    auto b=std::make_shared<render::Surface>(*a);b->key={9002,1};
    auto dead=Clear(a,9000,0.125);dead->surfaces.push_back(b);
    dead->commands.push_back(Clear(b,1,0.875)->commands[0]);
    dead->commands.push_back(Clear(a,1,0.5)->commands[0]);
    render::ImageCopy overwrite;overwrite.source={a->key,0,0,render::Aspect::Color};
    overwrite.destination={b->key,0,0,render::Aspect::Color};overwrite.extent={b->width,b->height};
    dead->commands.push_back(overwrite);dead->output=overwrite.destination;
    const auto masks=render::DeadAttachmentStores(*dead);assert(masks[0]==1&&masks[1]==1&&masks[2]==0);
    assert(backend->Submit(dead,false,error));assert(backend->ReadRGBA8(*dead,*dead->output,pixels,error));
    for(size_t n=0;n<pixels.size();n+=4){assert(pixels[n]==128&&pixels[n+1]==64&&pixels[n+2]==128&&pixels[n+3]==255);}
    assert(backend->ReadRGBA8(*dead,overwrite.source,pixels,error));assert(pixels[0]==128);
    std::cout<<"Dead attachment stores: overwritten clears discarded; copied and final contents preserved\n";
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
