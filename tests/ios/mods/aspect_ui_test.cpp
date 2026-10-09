#include "gta4_init.h"
#include "gta4_aspect_hooks.h"
#include <rex/memory.h>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sys/mman.h>
#undef REXLOG_INFO
#define REXLOG_INFO(...) ((void)0)
// Keep the original callback argument loaders; only their guest function-table
// lookup is replaced, with an explicit checked dispatcher below.
static void callback(uint32_t address,PPCContext& ctx,uint8_t* base);
#undef REX_CALL_INDIRECT_FUNC
#define REX_CALL_INDIRECT_FUNC(address) callback(address,ctx,base)
#include "aspect_ui_extracted.h"
using namespace gta4::aspect;
constexpr uint32_t viewport=0x10000, dc=0x20000, vertices=0x40000, font=0x82B9A118;
// GPU state calls do not touch CPU vertex positions or UVs.
extern "C" void sub_828C2140(PPCContext&,uint8_t*) {}
extern "C" void sub_828C21D0(PPCContext&,uint8_t*) {}
extern "C" void sub_828C2300(PPCContext&,uint8_t*) {}
extern "C" void sub_828C20C8(PPCContext&,uint8_t*) {}
extern "C" void sub_8227EE00(PPCContext&,uint8_t*) {}
extern "C" void sub_8227EA50(PPCContext&,uint8_t*) {}
extern "C" void sub_8227E948(PPCContext&,uint8_t*) {}
extern "C" void sub_821C3930(PPCContext&,uint8_t*) {}
extern "C" void sub_821C3C38(PPCContext&,uint8_t*) {}
extern "C" void sub_829FFE18(PPCContext& c,uint8_t*) {c.f1.f64=std::cos(c.f1.f64);}
extern "C" void sub_829FFD48(PPCContext& c,uint8_t*) {c.f1.f64=std::sin(c.f1.f64);}
extern "C" void sub_828C19C0(PPCContext&,uint8_t*) {}
extern "C" void sub_8227EE90(PPCContext&,uint8_t*) {}
extern "C" void sub_828C6568(PPCContext&,uint8_t*) {}
extern "C" void sub_828C64C8(PPCContext&,uint8_t*) {}
extern "C" void sub_828C6500(PPCContext&,uint8_t*) {}
extern "C" void sub_828C60A0(PPCContext&,uint8_t*) {}
extern "C" void sub_828BDAD8(PPCContext&,uint8_t*) {}
extern "C" void sub_828BFF18(PPCContext&,uint8_t*) {}
std::array<uint32_t,3> font_clip_seen{};
extern "C" void __imp__sub_821F5788(PPCContext& ctx,uint8_t* base) {
    // Already baked glyphs must never receive a second transform.
    ctx.f1.f64=.2;ctx.f2.f64=.4;sub_828C2290(ctx,base);
    NativeMenuClipScope clip(base,0x90000);
    font_clip_seen={Read(base,0x90000+10436),Read(base,0x90000+10440),Read(base,0x90000+11848)};
}
extern "C" void sub_8225CF80(PPCContext& c,uint8_t*) {c.r3.u32=0;}
extern "C" void sub_821C3138(PPCContext& c,uint8_t*) {c.r3.u32=0;}
extern "C" void sub_8239A8D8(PPCContext&,uint8_t*) {assert(false);}
constexpr uint32_t device=0x90000;
extern "C" void __imp__sub_821BCEE0(PPCContext& c,uint8_t* b) {
    sub_821F5788(c,b);
}
std::array<double,6> font_seen{};
UiContext compositor_seen{}, nested_body_seen{};
static void compositor(PPCContext& ctx,uint8_t* base) {
    compositor_seen=CurrentUi(base);FinalizeDc(base,ctx.r3.u32);
    {Scope body(MenuBodyUi(base));nested_body_seen=CurrentUi(base);}
    assert(CurrentUi(base).role==compositor_seen.role);
}
extern "C" void __imp__sub_8214DBD0(PPCContext& ctx,uint8_t* base) {compositor(ctx,base);}
extern "C" void __imp__sub_8239C468(PPCContext& ctx,uint8_t* base) {compositor(ctx,base);}
extern "C" void __imp__sub_821F6680(PPCContext& ctx,uint8_t* base) {
    font_seen={ctx.f1.f64,ctx.f2.f64,Float(base,font+4),Float(base,font+8),
               Float(base,font+12),Float(base,font+64)};
}
static PPCContext context() {PPCContext c{};c.r1.u32=0x80000;c.r3.u32=dc;return c;}
static void output(uint8_t* b,Extent e) {
    ConfigureDisplay(e);Publish(e,e);
    Write(b,viewport-16,0x820B92C4);
    Write(b,viewport+688,e.width);Write(b,viewport+692,e.height);
    Write(b,0x831C2200,viewport);
    Write(b,0x831C21F4,0);
    Float(b,device+12640,0);Float(b,device+12644,0);
    Float(b,device+12648,e.width);Float(b,device+12652,e.height);
}
static void resetVertices(uint8_t* b) {
    std::memset(b+vertices,0,4*36);
    Write(b,0x831C2D28,vertices); // generated emitter's current write pointer
    Write(b,0x831C2D38,0);Write(b,0x831C2D3C,0);
}
static void near(double a,double e) {if(std::abs(a-e)>=2e-4) std::fprintf(stderr,"value=%.9f expected=%.9f\n",a,e);assert(std::abs(a-e)<2e-4);}
static void queuedQuad(uint8_t* b,Extent e,bool componentScope) {
    output(b,e);Write(b,dc,0x82001418);Write(b,dc+4,0x12340000);
    constexpr std::array<Point,4> points={Point{.05,.72},{.15,.72},{.15,.89777778},{.05,.89777778}};
    for(unsigned i=0;i<4;++i) {Float(b,dc+8+i*8,points[i].x);Float(b,dc+12+i*8,points[i].y);}
    Write(b,dc+40,0);Write(b,dc+44,0xFF123456);
    Transform wanted;
    if(componentScope) {Scope radar(UiRole::kRadar);wanted=CurrentUi(b).transform;FinalizeDc(b,dc);}
    else {wanted=CurrentUi(b).transform;FinalizeDc(b,dc);}
    // Append publishes the size after construction, preserving the instance ID.
    Write(b,dc+4,Read(b,dc+4)|0x180);
    Write(b,0x831C2200,0);resetVertices(b);
    // Playback is on another thread without the producer's Scope or viewport.
    std::thread worker([&]{auto c=context();sub_821BD138(c,b);assert(!CurrentUi(b).active);});worker.join();
    constexpr std::array<Point,4> uvs={Point{0,1}, {0,0}, {1,1}, {1,0}};
    for(unsigned i=0;i<4;++i) {
        const auto p=wanted.Map(points[i]);const unsigned o=vertices+(3-i)*36;
        near(Float(b,o),p.x);near(Float(b,o+4),p.y);
        assert(Read(b,o+24)==0xFF123456);near(Float(b,o+28),uvs[i].x);near(Float(b,o+32),uvs[i].y);
    }
    const double width=std::abs(Float(b,vertices)-Float(b,vertices+36))*e.width;
    const double height=std::abs(Float(b,vertices+4)-Float(b,vertices+3*36+4))*e.height;
    near(width/height,1); // original 16:9-normalized square remains square on screen
    // Recycled command storage must not inherit the old radar context.
    Write(b,dc+4,0x56780000);auto c=context();DcScope reused(c,b);assert(!CurrentUi(b).active);
}
static void queuedPanel(uint8_t* b,Extent e) {
    output(b,e);Write(b,dc,0x820014C0);Write(b,dc+4,0x99A40000);
    Float(b,dc+8,e.width*.1);Float(b,dc+12,e.height*.3);
    Float(b,dc+16,e.width*.7);Float(b,dc+20,e.height*.8);Write(b,dc+24,0xAB765432);
    // Menu-independent header/footer commands have only the active UI viewport.
    const auto wanted=CurrentUi(b).transform.Pixels(e);FinalizeDc(b,dc);
    Write(b,0x831C2200,0);resetVertices(b);auto c=context();sub_821BD528(c,b);
    near(Float(b,vertices),wanted.Map(Point{e.width*.1,e.height*.3}).x);
    near(Float(b,vertices+4),wanted.Map(Point{e.width*.1,e.height*.3}).y);
    near(Float(b,vertices+3*36+4),wanted.Map(Point{e.width*.7,e.height*.8}).y);
    assert(Read(b,vertices+24)==0xAB765432);
}
static void ringConstants(uint8_t* b) {
    auto* base=b;
    for(auto [a,v] : std::array<std::pair<uint32_t,double>,8>{{
        {0x82000a34,0}, {0x82000d48,1}, {0x82000d74,.5}, {0x82019678,.1},
        {0x820becd8,.01}, {0x820becf4,-1}, {0x820bed0c,.99}, {0x820bef84,.02}}}) Float(b,a,v);
    for(auto [a,v] : std::array<std::pair<uint32_t,double>,4>{{
        {0x820016c0,-1.5707963705062866}, {0x820016c8,3.1415927410125732},
        {0x820016d0,1.5707963705062866}, {0x820bed10,0}}})
        REX_STORE_U64(a,std::bit_cast<uint64_t>(v));
    Float(b,0x820beed8,.98);
}
static void queuedRing(uint8_t* b,Extent e) {
    ringConstants(b);output(b,{1920,1080});
    Write(b,dc,0x820013C4);Write(b,dc+4,0x45640000);
    Float(b,dc+8,.1);Float(b,dc+12,.8);Float(b,dc+16,.1);Float(b,dc+20,.17777778);
    Write(b,dc+24,0xFF112233);Write(b,dc+28,0xFF778899);
    Write(b,dc+32,0xFFabcdef);Write(b,dc+36,4);Write(b,dc+40,8);
    resetVertices(b);auto c=context();__imp__sub_821BCFA0(c,b);
    const unsigned count=(Read(b,0x831C2D28)-vertices)/36;
    assert(count>=16 && count<64);
    std::vector<std::array<uint32_t,9>> authored(count);
    for(unsigned i=0;i<count;++i) for(unsigned j=0;j<9;++j) authored[i][j]=Read(b,vertices+i*36+j*4);
    output(b,e);Transform wanted;
    {Scope radar(UiRole::kRadar);wanted=CurrentUi(b).transform;FinalizeDc(b,dc);}
    Write(b,0x831C2200,0);resetVertices(b);c=context();sub_821BCFA0(c,b);
    assert((Read(b,0x831C2D28)-vertices)/36==count);
    for(unsigned i=0;i<count;++i) {
        const auto p=wanted.Map(Point{std::bit_cast<float>(authored[i][0]),std::bit_cast<float>(authored[i][1])});
        near(Float(b,vertices+i*36),p.x);near(Float(b,vertices+i*36+4),p.y);
        for(unsigned j=2;j<9;++j) assert(Read(b,vertices+i*36+j*4)==authored[i][j]);
    }
    Float(b,0x820becf4,1);
}
static void immediateRing(uint8_t* b,Extent e) {
    ringConstants(b);output(b,{1920,1080});
    Float(b,dc+8,.1);Float(b,dc+12,.8);Float(b,dc+16,.1);Float(b,dc+20,.17777778);
    const auto emit=[&] {
      auto c=context();c.r3.u32=dc+8;c.r4.u32=dc+16;
      c.r5.u32=0xFF112233;c.r6.u32=0xFF778899;c.r7.u32=8;c.r8.u32=0xFFabcdef;
      sub_821C4148(c,b);
    };
    resetVertices(b);emit();const unsigned count=(Read(b,0x831C2D28)-vertices)/36;
    assert(count>=16&&count<64);
    std::vector<std::array<uint32_t,9>> authored(count);
    for(unsigned i=0;i<count;++i)for(unsigned j=0;j<9;++j)authored[i][j]=Read(b,vertices+i*36+j*4);
    output(b,e);resetVertices(b);Transform wanted;
    {Scope radar(UiRole::kRadar);wanted=CurrentUi(b).transform;emit();}
    assert((Read(b,0x831C2D28)-vertices)/36==count);
    for(unsigned i=0;i<count;++i) {
      const auto p=wanted.Map(Point{std::bit_cast<float>(authored[i][0]),std::bit_cast<float>(authored[i][1])});
      near(Float(b,vertices+i*36),p.x);near(Float(b,vertices+i*36+4),p.y);
      for(unsigned j=2;j<9;++j)assert(Read(b,vertices+i*36+j*4)==authored[i][j]);
    }
    // A direct non-UI use remains in its original coordinate system.
    Write(b,0x831C2200,0);resetVertices(b);emit();
    for(unsigned i=0;i<count;++i)for(unsigned j=0;j<9;++j)
      assert(Read(b,vertices+i*36+j*4)==authored[i][j]);
    Float(b,0x820becf4,1);
}
static void radarConstants(uint8_t* b) {
    // Bit-exact scalar constants read from the supported TU8 image. No game
    // assets are required to run this fixture.
    for(auto [a,v]:std::array<std::pair<uint32_t,uint32_t>,19>{{
      {0x82000a34,0},{0x82000d48,0x3f800000},{0x82000d68,0x42c80000},
      {0x82000d74,0x3f000000},{0x8200c928,0x3e860a92},{0x8200c92c,0x4096cbe4},
      {0x820becf4,0xbf800000},{0x820bedd4,0x437f0000},{0x820bef94,0x3b808081},
      {0x82012284,0x405aef22},{0x82012288,0x3d00adfd},{0x8201228c,0x3eeb851f},
      {0x82012ea4,0x3fc90fdb},{0x820becd8,0x3c23d70a},{0x820becf8,0x41c80000},
      {0x820bedf4,0x4b000000},{0x820bedf8,0xcb000000},{0x820beeac,0x3d23d70a},
      {0x82B1B570,0}}}) Write(b,a,v);
}
static void callback(uint32_t address,PPCContext& ctx,uint8_t* b) {
    // These are the two drawing families used by the HUD callback commands.
    if(address==0x82339228 || address==0x82339288) {
      ctx.r3.u32=dc+64;ctx.r6.u32=dc+72;sub_82334C98(ctx,b);
    } else if(address==0x82335AB8) {
      ctx.r3.u32=0;ctx.r5.u32=0xff123456;ctx.r6.u32=1;ctx.r7.u32=1;
      ctx.f1.f64=.75;sub_823339E0(ctx,b);
    } else assert(false);
}
static void deferredRadar(uint8_t* b,Extent e,uint32_t vtable,uint32_t function) {
    radarConstants(b);output(b,{1920,1080});
    Write(b,dc,vtable);Write(b,dc+4,0x88740000);Write(b,dc+8,function);
    Float(b,dc+12,.1);Float(b,dc+16,.1777777778);
    Float(b,dc+64,.5);Float(b,dc+68,.5);Write(b,dc+72,0xff778899);
    auto emit=[&] {auto c=context();if(vtable==0x820009B0)sub_821FC590(c,b);else sub_823337E8(c,b);};
    Write(b,0x831C2200,0);resetVertices(b);emit();
    const unsigned count=(Read(b,0x831C2D28)-vertices)/36;
    if(count<4 || count>=256)std::fprintf(stderr,"radar %08x count=%u\n",function,count);
    assert(count>=4 && count<256);
    std::vector<std::array<uint32_t,9>> authored(count);
    for(unsigned i=0;i<count;++i)for(unsigned j=0;j<9;++j)authored[i][j]=Read(b,vertices+i*36+j*4);
    output(b,e);Transform wanted;
    {Scope radar(UiRole::kRadarLocal);FinalizeDc(b,dc);}
    Write(b,dc+4,Read(b,dc+4)|0x180);Write(b,0x831C2200,0);resetVertices(b);
    std::thread worker([&]{emit();assert(!CurrentUi(b).active);});worker.join();
    assert((Read(b,0x831C2D28)-vertices)/36==count);
    for(unsigned i=0;i<count;++i) {
      const auto p=wanted.Map(Point{std::bit_cast<float>(authored[i][0]),std::bit_cast<float>(authored[i][1])});
      near(Float(b,vertices+i*36),p.x);near(Float(b,vertices+i*36+4),p.y);
      for(unsigned j=2;j<9;++j)assert(Read(b,vertices+i*36+j*4)==authored[i][j]);
    }
    // Publishing this generic callback from a non-radar/offscreen producer
    // must erase the prior record; original primitive bytes are preserved.
    FinalizeDc(b,dc);resetVertices(b);emit();
    for(unsigned i=0;i<count;++i)for(unsigned j=0;j<9;++j)
      assert(Read(b,vertices+i*36+j*4)==authored[i][j]);
}
static Rect radar_window{.03,.69,.175,.9477777778};
constexpr uint32_t radar_pass=0xa0000, radar_view=radar_pass+176;
extern "C" void __imp__sub_8239C9B8(PPCContext& ctx,uint8_t* b) {
    // The original pass builder copies/rebuilds the authored source every run.
    // GPU submission is isolated; the real canonical viewport setter is used.
    Write(b,radar_view+688,Read(b,viewport+688));Write(b,radar_view+692,Read(b,viewport+692));
    PPCContext c=ctx;c.r3.u32=radar_view;
    c.r4.s32=std::lround(radar_window.left*Read(b,viewport+688));
    c.r5.s32=std::lround(radar_window.top*Read(b,viewport+692));
    c.r6.s32=std::lround((radar_window.right-radar_window.left)*Read(b,viewport+688));
    c.r7.s32=std::lround((radar_window.bottom-radar_window.top)*Read(b,viewport+692));
    c.f1.f64=0;c.f2.f64=1;__imp__sub_828BE238(c,b);
}
static void radarViewport(uint8_t* b,Extent e) {
    radar_window={.03,.69,.175,.9477777778};output(b,e);radarConstants(b);
    auto c=context();c.r3.u32=radar_pass;
    __imp__sub_8239C9B8(c,b);
    const Rect baseline{Float(b,radar_view+664),Float(b,radar_view+668),
      Float(b,radar_view+664)+Float(b,radar_view+672),Float(b,radar_view+668)+Float(b,radar_view+676)};
    const auto expected=SafeLayout(e,{0,1},{}).Map(baseline);
    sub_8239C9B8(c,b);
    near(Float(b,radar_view+664),std::lround(expected.left*e.width)/double(e.width));
    near(Float(b,radar_view+668),std::lround(expected.top*e.height)/double(e.height));
    const double w=Float(b,radar_view+672)*e.width,h=Float(b,radar_view+676)*e.height;
    assert(std::abs(w-h)<=2); // integer viewport boundary rounding
    const std::array<uint32_t,4> once{Read(b,radar_view+664),Read(b,radar_view+668),Read(b,radar_view+672),Read(b,radar_view+676)};
    sub_8239C9B8(c,b); // rebuilding from authored input never compounds correction
    for(unsigned i=0;i<4;++i)assert(Read(b,radar_view+664+i*4)==once[i]);
    const auto bounds=RadarScreenBounds();assert(bounds);
    near(bounds->left,Float(b,radar_view+664));near(bounds->top,Float(b,radar_view+668));
    // New display state cannot reuse a viewport/tap rectangle from an old frame.
    output(b,Extent{e.width+1,e.height});assert(!RadarScreenBounds());
    output(b,e);c.r3.u32=radar_pass;sub_8239C9B8(c,b);
    // Actual XYZ map command remains local; mask, rings and map are sized by
    // the same viewport, without touching UVs, depth, color or geometry count.
    Write(b,dc,0x820013FC);Write(b,dc+4,0x99140000);
    for(unsigned i=0;i<4;++i) {
      Float(b,dc+16+i*16,(i&1)?1:0);Float(b,dc+20+i*16,(i&2)?1:0);
      Float(b,dc+24+i*16,0);Float(b,dc+28+i*16,1);
      Float(b,dc+80+i*8,(i&1)?1:0);Float(b,dc+84+i*8,(i&2)?1:0);
    }
    Write(b,dc+112,0);Write(b,dc+116,0xffabcdef);
    resetVertices(b);auto m=context();__imp__sub_821BD0A0(m,b);
    std::array<uint8_t,144> authored{};std::memcpy(authored.data(),b+vertices,authored.size());
    {Scope local(UiRole::kRadarLocal);FinalizeDc(b,dc);}
    Write(b,0x831C2200,0);resetVertices(b);
    std::thread worker([&]{auto draw=context();sub_821BD0A0(draw,b);});worker.join();
    assert(std::memcmp(authored.data(),b+vertices,authored.size())==0);
    // Full-screen pause map uses its original viewport.
    output(b,e);radar_window={0,0,1,1};c.r3.u32=radar_pass;sub_8239C9B8(c,b);
    near(Float(b,radar_view+664),0);near(Float(b,radar_view+668),0);
    near(Float(b,radar_view+672),1);near(Float(b,radar_view+676),1);
    // An offscreen texture is never mapped into device display coordinates.
    Write(b,radar_view+688,256);Write(b,radar_view+692,256);
    Float(b,radar_view+664,.03);Float(b,radar_view+668,.69);
    Float(b,radar_view+672,.145);Float(b,radar_view+676,.25777778);
    PrepareRadarViewport(c,b,radar_view);near(Float(b,radar_view+668),.69);
    Write(b,radar_view+688,e.width);Write(b,radar_view+692,e.height);
    Float(b,radar_view+664,1e20f);
    const auto corrupt=Read(b,radar_view+664);
    PrepareRadarViewport(c,b,radar_view);assert(Read(b,radar_view+664)==corrupt);
    PrepareRadarViewport(c,b,UINT32_MAX-100); // no wrapped guest address reads
}
static void menuClip(uint8_t* b,Extent e) {
    output(b,e);Write(b,device+10432,0);Write(b,device+10436,0x80000000);
    Write(b,device+10440,e.width|(e.height<<16));Write(b,device+11848,0);
    const auto panel=Layout(e).Pixels(e).Map(Rect{0,0,double(e.width),double(e.height)});
    Write(b,dc,0x8200138C);Write(b,dc+4,0x66540000);
    {Scope menu(UiRole::kFixed);FinalizeDc(b,dc);}
    Write(b,0x831C2200,0);resetVertices(b);auto c=context();sub_821BCEE0(c,b);
    if(Layout(e).identity()) assert(font_clip_seen[2]==0);
    else {
        assert(font_clip_seen[2]==1);
        assert((font_clip_seen[0]&0x3fff)==unsigned(std::ceil(panel.left)));
        assert(((font_clip_seen[0]>>16)&0x3fff)==unsigned(std::ceil(panel.top)));
        assert((font_clip_seen[1]&0x3fff)==unsigned(std::floor(panel.right)));
        assert(((font_clip_seen[1]>>16)&0x3fff)==unsigned(std::floor(panel.bottom)));
    }
    assert(Read(b,device+10436)==0x80000000 && Read(b,device+11848)==0);
    assert(Read(b,device+10440)==(e.width|(e.height<<16)));
    near(Float(b,vertices),.2);near(Float(b,vertices+4),.4); // baked text isn't mapped twice
    // Existing narrower clipping and signed window offsets remain honored.
    {Scope menu(UiRole::kMenuBody);Write(b,device+10432,5|(7<<16));
     Write(b,device+10436,10|(20<<16));Write(b,device+10440,300|(350<<16));
     NativeMenuClipScope clip(b,device);
     if(!Layout(e).identity()) {
       assert((Read(b,device+10440)&0x3fff)==unsigned(std::max(int(std::ceil(panel.left)),305)));
       assert(((Read(b,device+10440)>>16)&0x3fff)==unsigned(std::max(int(std::ceil(panel.top)),357)));
     }}
    assert(Read(b,device+10436)==(10|(20<<16)));
    // An offscreen pause-map target is not clipped in screen coordinates.
    {Scope menu(UiRole::kFixed);Float(b,device+12648,256);Float(b,device+12652,256);
     NativeMenuClipScope clip(b,device);
     assert(Read(b,device+10436)==(10|(20<<16)));}
    // Restoring the scissor marks fixed state, without dirtying uploads or constants.
    output(b,e);Write(b,device+16,0);Write(b,device+20,0);
    Write(b,device+24,0x12345678);Write(b,device+28,0x12345678);
    Write(b,device+10436,0x80000000);Write(b,device+10440,e.width|(e.height<<16));
    {Scope menu(UiRole::kFixed);NativeMenuClipScope clip(b,device);}
    if(!Layout(e).identity()) assert(Read(b,device+16)||Read(b,device+20));
    assert(Read(b,device+24)==0x12345678 && Read(b,device+28)==0x12345678);
    Write(b,device+10436,10|(20<<16));
    // Gameplay and radar draws bypass the pause clip, even with native aspect.
    {Scope radar(UiRole::kRadar);NativeMenuClipScope clip(b,device);
     assert(Read(b,device+10436)==(10|(20<<16)));}
}
int main() {
    auto* b=static_cast<uint8_t*>(mmap(nullptr,UINT64_C(1)<<32,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(b!=MAP_FAILED);Float(b,0x82000A34,0);Float(b,0x82000D48,1);Float(b,0x820BECF4,1);
    for(Extent e : {Extent{2421,1668},{2736,1260},{1920,1080}}) {
        radarViewport(b,e);
        deferredRadar(b,e,0x82012234,0x82339228);
        deferredRadar(b,e,0x82012234,0x82339288);
        deferredRadar(b,e,0x820009B0,0x82335AB8);
        immediateRing(b,e);queuedRing(b,e);menuClip(b,e);queuedQuad(b,e,true);queuedQuad(b,e,false);queuedPanel(b,e);
        // Top-level compositors capture layout even while the queue's producer
        // has no display viewport. Menu rows retain their divider-anchored body.
        output(b,e);Write(b,0x831C2200,0);Write(b,dc,0x820014C0);
        auto root=context();sub_8214DBD0(root,b);
        assert(compositor_seen.active && compositor_seen.role==UiRole::kFixed);
        near(compositor_seen.transform.oy,Layout(e).oy);
        near(nested_body_seen.transform.oy,MenuBodyLayout(e).oy);
        assert(!CurrentUi(b).active);
        {DcScope playback(root,b);assert(CurrentUi(b).role==UiRole::kFixed);}
        sub_8239C468(root,b);
        assert(compositor_seen.active && compositor_seen.role==UiRole::kRadarLocal);
        assert(compositor_seen.transform.identity());
        assert(!CurrentUi(b).active);
        output(b,e);Write(b,0x82A935A4,0);
        for(auto o : kFontScaledOffsets) Float(b,font+o,1);
        auto c=context();c.f1.f64=.3;c.f2.f64=.4;
        {Scope menu(MenuBodyUi(b));const auto expected=CurrentUi(b).transform;
         sub_821F6680(c,b);const auto p=expected.Map(Point{.3,.4});
         near(font_seen[0],p.x);near(font_seen[1],p.y);
         near(font_seen[2],expected.sx);near(font_seen[3],expected.sy);
         near(font_seen[4],expected.sx);near(font_seen[5],expected.sx);
         resetVertices(b);sub_821F5788(c,b);near(Float(b,vertices),.2);near(Float(b,vertices+4),.4);}
        for(auto o : kFontScaledOffsets) assert(Float(b,font+o)==1);
    }
    // A command published outside screen UI must discard old captured state.
    Write(b,dc,0x82001418);Write(b,0x831C2200,0);FinalizeDc(b,dc);
    auto c=context();{DcScope scope(c,b);assert(!CurrentUi(b).active);}
    // Offscreen/world cameras do not acquire a UI layout merely by being full-size.
    output(b,{2421,1668});Write(b,viewport-16,0x820B9284);assert(!CurrentUi(b).active);
    munmap(b,UINT64_C(1)<<32);
    std::puts("Queued curved radar, UI panels, scissor restoration, UV/color, baked fonts, worker playback and reuse contracts passed");
}
