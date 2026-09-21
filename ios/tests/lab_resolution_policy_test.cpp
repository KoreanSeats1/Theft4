#include "theft4_output_policy.h"
#include "gta4_aspect_resolution.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

int main() {
    // Exercise the same aspect fitting and 1.5x calculation used by the native
    // resolution hooks, not just the policy's declared render dimensions.
    // Changing the display, FSR or an old Boost preference must not shrink the
    // selected scene budget back to 720p.
    for (const auto scene : {std::pair<unsigned,unsigned>{960,540}, {1280,720},
                             {1600,900}, {1920,1080}}) {
        for (bool fsr : {false, true}) {
            for (const auto display : {std::pair<unsigned,unsigned>{2420,1668}, {2752,2064},
                                       {0,0}, {1024,768}, {1668,2420}, {7680,4320}}) {
                const auto p = theft4_output_policy_for_lab(scene.second, fsr, display.first, display.second);
                assert(p.render_width == scene.first && p.render_height == scene.second);
                assert(p.fsr1 == fsr);
                const auto logical = gta4::aspect::resolution::Select(
                    {p.video_width,p.video_height}, "16:9", {display.first,display.second});
                assert(uint32_t(std::round(logical.width / (fsr ? 1.5 : 1.0))) == scene.first);
                assert(uint32_t(std::round(logical.height / (fsr ? 1.5 : 1.0))) == scene.second);
                assert(p.output_width * 9 == p.output_height * 16);
                if (fsr) {
                    assert(p.output_width >= p.render_width && p.output_height >= p.render_height);
                    assert(p.output_width <= 3840 && p.output_height <= 2160);
                    if (display.first == 2420)
                        assert(p.output_width == 2416 && p.output_height == 1359);
                    if (!display.first || display.first == 1024)
                        assert(p.output_width == 1920 && p.output_height == 1080);
                } else {
                    assert(p.output_width == scene.first && p.output_height == scene.second);
                }
            }
        }
    }
    for (uint32_t invalid : {0u, 1u, 899u, 901u, 2160u, UINT32_MAX}) {
        const auto fallback = theft4_output_policy_for_lab(invalid, true, 2420, 1668);
        assert(fallback.render_width == 1280 && fallback.render_height == 720);
    }

    for (uint32_t height : {540u, 720u, 900u, 1080u}) {
        const auto p = theft4_output_policy_for_a19_lab(height);
        assert(p.render_height == height);
        assert(p.output_width == 1920 && p.output_height == 1080);
        assert(p.fsr1 == (height < 1080));
        const auto logical = gta4::aspect::resolution::Select(
            {p.video_width,p.video_height}, "16:9", {2240,1260});
        assert(uint32_t(std::round(logical.width / (p.fsr1 ? 1.5 : 1.0))) == p.render_width);
        assert(uint32_t(std::round(logical.height / (p.fsr1 ? 1.5 : 1.0))) == p.render_height);
    }

    puts("Lab 540p/720p/900p/1080p + FSR policies and native-hook extents passed");
}
