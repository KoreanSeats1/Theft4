#include "gta4_init.h"
#include "gta4_aspect_hooks.h"
#include <rex/memory.h>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sys/mman.h>
#undef REXLOG_INFO
#define REXLOG_INFO(...) ((void)0)
#include "aspect_camera_extracted.h"
// Only the math-library boundary and GPU-only publication are doubled.
extern "C" void sub_82A02158(PPCContext& ctx,uint8_t*) {ctx.f1.f64=std::tan(ctx.f1.f64);}
extern "C" void sub_828BCFA8(PPCContext&,uint8_t*) {assert(false && "CPU fixture cannot publish GPU constants");}
using namespace gta4::aspect;
constexpr uint32_t viewport=0x10000;
static void setup(uint8_t* base,Extent extent,uint32_t owner,unsigned reverse) {
    std::memset(base+viewport-16,0,1040);
    Write(base,viewport-16,owner);
    Write(base,viewport+688,extent.width); Write(base,viewport+692,extent.height);
    Float(base,viewport+672,1); Float(base,viewport+676,1);
    Float(base,viewport+700,extent.aspect());
    Float(base,viewport+696,60); Float(base,viewport+704,0.1); Float(base,viewport+708,1000);
    Float(base,viewport+720,1); Float(base,viewport+724,1);
    base[viewport+992]=reverse;
    for (auto offset : {128u,384u,576u})
        for (unsigned i=0;i<4;++i) Float(base,viewport+offset+i*20,1);
}
static std::array<float,16> matrix(uint8_t* base,uint32_t offset) {
    std::array<float,16> m{};
    for(unsigned i=0;i<16;++i) m[i]=Float(base,viewport+offset+4*i);
    return m;
}
static PPCContext context() {PPCContext ctx{};ctx.r1.u32=0x80000;ctx.r3.u32=viewport;return ctx;}
static bool inside(uint8_t* base,double x,double y,double z) {
    for(unsigned i=0;i<6;++i) {
        const auto p=viewport+768+16*i;
        const double distance=Float(base,p)*x+Float(base,p+4)*y+Float(base,p+8)*z+Float(base,p+12);
        assert(std::isfinite(distance));
        if(distance<-1e-4) return false;
    }
    return true;
}
int main() {
    const auto size=UINT64_C(1)<<32;
    auto* base=static_cast<uint8_t*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(base!=MAP_FAILED);
    // Mathematical constants used by the generated projection/plane normalizer.
    Float(base,0x82000D48,1); Float(base,0x82000A34,0);
    Float(base,0x82018970,3.141592653589793/360);
    Float(base,0x820BECF4,-1);
    // rsqrt's refinement constants are loaded from this small literal block.
    Write(base,0x820BED18,0x3ff00000); Write(base,0x820BED1C,0); // 1.0 double
    Write(base,0x820BED20,0x3fe00000); Write(base,0x820BED24,0); // 0.5 double
    Write(base,0x820BED10,0); Write(base,0x820BED14,0); // 0.0 double
    unsigned cases=0;
    for(uint32_t screenOwner : {0x820B9284u,0x820212F4u,0x820BCB40u})
    for(unsigned reverse : {0u,1u}) {
        ConfigureDisplay({}); Publish({1280,720},{1280,720});
        setup(base,{1280,720},screenOwner,reverse); auto ctx=context(); sub_828BDAD8(ctx,base);
        const auto original=matrix(base,448);
        assert(inside(base,0,0,-10));
        const double tan_y=1/original[5], tan_x=1/original[0];
        for(Extent display : {Extent{2421,1668}, {2420,1668}, {2736,1260}, {1260,2736}, {1920,1080}}) {
            ConfigureDisplay(display); Publish(display,display);
            setup(base,display,screenOwner,reverse); ctx=context(); sub_828BDAD8(ctx,base);
            const auto expanded=matrix(base,448), combined=matrix(base,256);
            assert(std::abs(Float(base,viewport+696)-60)<1e-7); // input FOV retained
            assert(inside(base,0,0,-10));
            for(unsigned i=0;i<16;++i) {
                if(i%4>=2) assert(expanded[i]==original[i]); // exact depth/W retention
                if(!(std::abs(combined[i]-expanded[i])<1e-6)) {
                    std::fprintf(stderr,"Combined mismatch reverse=%u aspect=%f i=%u value=%f expected=%f\n",reverse,display.aspect(),i,combined[i],expanded[i]);
                }
                assert(std::abs(combined[i]-expanded[i])<1e-6); // derived view-projection refreshed
            }
            assert(expanded[0]<=original[0]+1e-6 && expanded[5]<=original[5]+1e-6);
            // Primary shader consumers use the cached tangents. Auxiliary
            // rendering ALSO rebuilds projections from authored FOV below.
            assert(std::abs(Float(base,viewport+712)*expanded[5]-1)<1e-6);
            assert(std::abs(Float(base,viewport+716)*expanded[0]-1)<1e-6);
            const bool narrow=display.aspect()<kReferenceAspect;
            if(narrow) assert(std::abs(expanded[0]-original[0])<1e-6);
            else assert(std::abs(expanded[5]-original[5])<1e-6);
            // The expanded edge must be inside the ACTUAL rebuilt culling planes.
            if(narrow) assert(inside(base,0,10*(tan_y+1/expanded[5])/2,-10));
            else assert(inside(base,10*(tan_x+1/expanded[0])/2,0,-10));
            assert(!inside(base,10*1/expanded[0]*1.1,0,-10));
            assert(!inside(base,0,10*1/expanded[5]*1.1,-10));
            const auto stable=matrix(base,448); ctx=context(); sub_828BDAD8(ctx,base);
            assert(matrix(base,448)==stable); // no cumulative FOV expansion
            // Exercise the original PPC setter/builders used by both auxiliary
            // call sites; a correct primary matrix alone is insufficient.
            constexpr uint32_t derived = viewport + 4096;
            for(uint32_t caller : {0x827BCE90u,0x827BD198u}) {
              // Actual render-thread copies have no main-camera owner at -16.
              for(uint32_t sourceOwner : {screenOwner,0x82001150u,0u,0xCDCDCDCDu}) {
                Write(base,viewport-16,sourceOwner);
                std::memcpy(base+derived-16,base+viewport-16,1040);
                Write(base,derived-16,0xDEADBEEF); // independent offscreen owner
                ctx=context(); ctx.lr=caller; ctx.r3.u32=derived;
                ctx.r30.u32=viewport; ctx.r31.u32=viewport;
                ctx.f1.f64=Float(base,viewport+696);
                ctx.f2.f64=display.aspect(); ctx.f3.f64=.1; ctx.f4.f64=1000;
                // Suppress the new copied-camera rebuild guard only for the
                // legacy control. Its source projection was uninitialized.
                Float(base,derived+712,0);
                auto uncorrected=ctx; __imp__sub_828BE580(uncorrected,base);
                if(narrow) assert(std::abs(Float(base,derived+468)-stable[5])>.01);
                PrepareDerivedProjection(ctx,base); __imp__sub_828BE580(ctx,base);
                for(unsigned i=0;i<16;++i)
                    assert(std::abs(Float(base,derived+448+i*4)-stable[i])<1e-6);
                assert(Float(base,viewport+696)==60); // shared camera untouched
              }
            }
            // Auxiliary frustum fitting recomputes all three trig values.
            for(uint32_t caller : {0x827BD3D0u,0x827BD3E8u,0x827BD3F8u}) {
                ctx=context(); ctx.lr=caller; ctx.r30.u32=viewport;
                ctx.f1.f64=double(float(60*Float(base,0x82018970)));
                PrepareDerivedHalfAngle(ctx,base,caller);
                assert(std::abs(std::tan(ctx.f1.f64)-Float(base,viewport+712))<1e-6);
            }
            Write(base,viewport-16,screenOwner);
            // Lighting publishes a standalone snapshot and subsequently rebuilds
            // it. The original rebuild loses the expanded field of view; the
            // shipping adapter must preserve geometry's projection/tangents.
            std::memcpy(base+derived,base+viewport,1000);
            Write(base,derived-16,0x82001150);
            ctx=context(); ctx.r3.u32=derived;
            __imp__sub_828BDAD8(ctx,base);
            if(narrow) assert(std::abs(Float(base,derived+468)-stable[5])>.01);
            std::memcpy(base+derived,base+viewport,1000);
            ctx=context(); ctx.r3.u32=derived; sub_828BDAD8(ctx,base);
            for(unsigned i=0;i<16;++i) {
                if(std::abs(Float(base,derived+448+4*i)-stable[i])>=1e-6)
                    std::fprintf(stderr,"Copied rebuild aspect=%f depth=%u i=%u got=%f expected=%f tangent=%f fov=%f\n",display.aspect(),reverse,i,Float(base,derived+448+4*i),stable[i],Float(base,derived+712),Float(base,derived+696));
                assert(std::abs(Float(base,derived+448+4*i)-stable[i])<1e-6);
            }
            assert(Float(base,derived+696)==60);
            for(double ndc : {-.95,-.5,0.,.5,.95}) {
                assert(std::abs(ndc*Float(base,derived+712)-ndc/stable[5])<1e-6);
                assert(std::abs(ndc*Float(base,derived+716)-ndc/stable[0])<1e-6);
            }
            ctx=context(); ctx.lr=0x82522644; ctx.r30.u32=viewport;
            ctx.f1.f64=60; PrepareDerivedProjection(ctx,base);
            assert(ctx.f1.f64==60); // reflection/light projection not expanded
            // A late screen owner receives both projection and frustum updates.
            setup(base,display,0,reverse); ctx=context(); sub_828BDAD8(ctx,base);
            Write(base,viewport-16,screenOwner); ctx=context(); PrepareViewport(ctx,base);
            for(unsigned i=0;i<16;++i) assert(std::abs(matrix(base,448)[i]-stable[i])<1e-6);
            assert(inside(base,0,0,-10));
            // A source owner assigned after construction must be fixed before
            // it is copied, rather than waiting until geometry's viewport bind.
            setup(base,display,0,reverse); ctx=context(); sub_828BDAD8(ctx,base);
            Write(base,viewport-16,screenOwner); ctx=context(); ctx.r4.u32=viewport;
            const auto beforeCopy=ctx; PrepareCameraCopy(ctx,base);
            assert(ctx.r3.u64==beforeCopy.r3.u64 && ctx.r4.u64==beforeCopy.r4.u64);
            for(unsigned i=0;i<16;++i) assert(std::abs(matrix(base,448)[i]-stable[i])<1e-6);
            ++cases;
        }
    }
    // Radar, reflection, shadow and other offscreen owners must not be expanded.
    ConfigureDisplay({2420,1668}); Publish({2420,1668},{2420,1668});
    for(uint32_t owner : {0u,0x820B92C4u,0xDEADBEEFu}) {
        setup(base,{2420,1668},owner,0); auto ctx=context(); __imp__sub_828BDAD8(ctx,base);
        const auto retail=matrix(base,448); ctx=context(); sub_828BDAD8(ctx,base);
        assert(matrix(base,448)==retail);
    }
    // Phone projection includes safe-area translation, including a 16:9 window
    // where scale alone cannot reveal an owner assigned after base construction.
    const SafeInsets safe{.04,.01,.04,.025};
    for(Extent display : {Extent{2420,1668},{2736,1260},{1920,1080}}) {
        ConfigureDisplay(display,safe); Publish(display,display);
        setup(base,display,0,0); auto ctx=context(); __imp__sub_828BDAD8(ctx,base);
        const auto retail=matrix(base,448);
        auto expected=retail; TransformProjection(expected,SafeLayout(display,{1,1},safe));
        Write(base,viewport-16,0x820B95F0); ctx=context(); PrepareViewport(ctx,base);
        for(unsigned i=0;i<16;++i) assert(std::abs(matrix(base,448)[i]-expected[i])<1e-6);
        const auto stable=matrix(base,448); ctx=context(); sub_828BDAD8(ctx,base);
        assert(matrix(base,448)==stable);
    }
    ConfigureDisplay({},safe); assert(!ConfiguredDisplay({}).valid());
    ConfigureDisplay({1920,1080},{NAN,0,0,0}); assert(ConfiguredDisplay({}).valid());
    assert(Output().safe.left==0);
    munmap(base,size);
    std::printf("Actual PPC camera/frustum: %u expanded views, both depth modes, late owners, offscreen isolation and safe phone passed\n",cases);
}
