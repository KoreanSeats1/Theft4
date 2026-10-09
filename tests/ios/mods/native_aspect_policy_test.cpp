#include "theft4_output_policy.h"
#include "gta4_aspect_policy.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace gta4::aspect;
int main() {
    unsigned cases = 0;
    // Real iPad/phone shapes, rotation, multitasking, missing dimensions, and
    // future high-resolution displays. Check the real graph's integer selection.
    for (const Extent display : {Extent{2420,1668}, {2421,1668}, {2752,2064}, {2736,1260},
             {2556,1179}, {1334,750}, {1668,2420}, {1260,2736}, {800,1668},
             {7680,4320}, {4320,7680}, {16384,16384}, {0,0}}) {
        for (uint32_t height : {540u,720u,900u,1080u,THEFT4_LAB_NATIVE_16_9}) {
            for (bool fsr : {false,true}) for (bool capped : {false,true}) {
                const auto p = theft4_output_policy_for_native_aspect_lab(
                    height,fsr,display.width,display.height,capped);
                if (!display.valid()) { assert(!p.native_aspect); continue; }
                assert(p.native_aspect && p.aspect_width==display.width && p.aspect_height==display.height);
                const auto selected = SelectExtent(LimitExtent(
                    {std::max(640u,p.video_width),std::max(480u,p.video_height)},4095),"auto",display);
                const unsigned width = unsigned(std::round(selected.width / (p.fsr1 ? 1.5 : 1)));
                const unsigned h = unsigned(std::round(selected.height / (p.fsr1 ? 1.5 : 1)));
                assert(width==p.render_width && h==p.render_height);
                assert(p.render_width<=4095 && p.render_height<=4095);
                assert(p.output_width<=4095 && p.output_height<=4095);
                if (height==THEFT4_LAB_NATIVE_16_9) assert(!p.fsr1);
                if (!p.fsr1) assert(p.render_width==p.output_width && p.render_height==p.output_height);
                const double rounding = 2.0/std::min(p.render_width,p.render_height);
                assert(std::abs(double(p.render_width)/p.render_height/display.aspect()-1)<rounding);
                const double wantedWidth=std::max(double(height*16/9),double(height)*display.aspect());
                const double wantedHeight=std::max(double(height),double(height*16/9)/display.aspect());
                const unsigned maximum=fsr ? 2730 : 4095;
                if (height!=THEFT4_LAB_NATIVE_16_9 && wantedWidth<=maximum && wantedHeight<=maximum) {
                    // Expands one axis at the selected baseline pixel density.
                    assert(p.render_width+2 >= height*16/9);
                    assert(p.render_height+2 >= height);
                }
                ++cases;
            }
        }
    }
    for (const Extent display : {Extent{2420,1668}, {2752,2064}, {2736,1260}, {1260,2736}}) {
        for (double fov : {30.0,45.0,60.0,90.0,110.0}) {
            const double expanded=ExpandVerticalFov(fov,display.aspect());
            const double tangent=std::tan(expanded*3.141592653589793/360);
            const double original=std::tan(fov*3.141592653589793/360);
            assert(tangent+1e-12>=original);
            assert(tangent*display.aspect()+1e-12>=original*kReferenceAspect);
            if (display.aspect()<kReferenceAspect)
                assert(std::abs(tangent*display.aspect()-original*kReferenceAspect)<1e-10);
            else assert(expanded==fov);
        }
        const SafeInsets safe{0.04,0.01,0.04,0.025};
        const auto center=Layout(display);
        const auto centeredSafe=SafeLayout(display,{.5,.5},safe);
        assert(center.sx==centeredSafe.sx && center.sy==centeredSafe.sy);
        assert(center.ox==centeredSafe.ox && center.oy==centeredSafe.oy);
        for (Point anchor : {Point{0,0},{0,1},{1,0},{1,1}}) {
            const auto layout=SafeLayout(display,anchor,safe);
            const Point point{.15,.22};
            const auto restored=layout.Unmap(layout.Map(point));
            assert(std::abs(restored.x-point.x)<1e-12 && std::abs(restored.y-point.y)<1e-12);
            // An authored square remains square in physical pixels.
            assert(std::abs(layout.sx*display.width/(layout.sy*display.height)-kReferenceAspect)<1e-12);
            std::array<float,16> matrix={1,0,0,0, 0,2,0,0, 0,0,3,1, 0,0,4,0};
            const auto before=matrix;
            TransformProjection(matrix,layout);
            for (unsigned row=0;row<4;++row) {
                assert(matrix[row*4+2]==before[row*4+2]);
                assert(matrix[row*4+3]==before[row*4+3]);
            }
        }
    }
    // No extreme cross-product may wrap before the dimension bound is applied.
    uint32_t w=1920,h=1080;
    theft4_expand_extent(&w,&h,UINT32_MAX,1,4095);
    assert(w==4095 && h>=1 && h<=4095);
    std::printf("Native aspect: %u render/FSR/display policies plus framing, safe UI and depth contracts passed\n",cases);
}
