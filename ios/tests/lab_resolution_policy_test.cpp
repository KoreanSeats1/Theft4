#include "theft4_output_policy.h"
#include "gta4_aspect_resolution.h"

#include <algorithm>
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
    for (const unsigned scene_height : {540u, 720u, 900u, 1080u}) {
        for (bool fsr : {false, true}) {
            for (const auto display : {std::pair<unsigned,unsigned>{2420,1668}, {2752,2064},
                                       {0,0}, {1024,768}, {1668,2420}, {7680,4320}}) {
                const auto p = theft4_output_policy_for_lab(
                    scene_height, fsr, display.first, display.second);
                const unsigned scene_width = theft4_width_for_native_aspect(
                    scene_height, display.first, display.second);
                assert(p.render_width == scene_width && p.render_height == scene_height);
                assert(p.fsr1 == fsr);
                const auto logical = gta4::aspect::resolution::Select(
                    {p.video_width,p.video_height}, "auto", {display.first,display.second});
                const int resolved_width = int(std::round(logical.width / (fsr ? 1.5 : 1.0)));
                const int resolved_height = int(std::round(logical.height / (fsr ? 1.5 : 1.0)));
                // Integer render targets can differ by one pixel when the
                // physical device ratio has no exact representation at this
                // height. Never tolerate a material budget change.
                assert(std::abs(resolved_width - int(scene_width)) <= 1);
                assert(std::abs(resolved_height - int(scene_height)) <= 1);
                if (fsr) {
                    if (display.first && display.second) {
                        assert(p.output_width == display.first);
                        assert(p.output_height == display.second);
                    } else {
                        assert(p.output_width == scene_width * 3 / 2);
                        assert(p.output_height == scene_height * 3 / 2);
                    }
                } else {
                    assert(p.output_width == scene_width && p.output_height == scene_height);
                }
            }
        }
    }
    for (uint32_t invalid : {0u, 1u, 899u, 901u, 2160u, 2161u}) {
        const auto fallback = theft4_output_policy_for_lab(invalid, true, 2420, 1668);
        assert(fallback.render_width == theft4_width_for_native_aspect(720, 2420, 1668));
        assert(fallback.render_height == 720);
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
        for (bool fsr : {false, true}) {
            const auto selected = theft4_output_policy_for_a19_lab_selected(height, fsr);
            assert(selected.render_height == height && selected.render_width == height * 16 / 9);
            assert(selected.fsr1 == fsr);
            assert(selected.output_height == (fsr ? 1080u : height));
        }
    }

    const auto capped_phone = theft4_output_policy_for_fixed_1080_lab_selected(900, true);
    assert(capped_phone.render_width == 1600 && capped_phone.render_height == 900);
    assert(capped_phone.output_width == 1920 && capped_phone.output_height == 1080);
    assert(capped_phone.fsr1);

    const auto full_phone = theft4_output_policy_for_fixed_1080_lab_selected_aspect(
        900, true, 2736, 1260);
    assert(full_phone.render_height == 900);
    assert(full_phone.render_width == theft4_width_for_native_aspect(900, 2736, 1260));
    assert(full_phone.output_height == 1080);
    assert(full_phone.output_width == theft4_width_for_native_aspect(1080, 2736, 1260));
    assert(full_phone.fsr1);

    const auto full_ipad = theft4_output_policy_for_fixed_1080_lab_selected_aspect(
        900, true, 2420, 1668);
    assert(full_ipad.render_height == 900);
    assert(full_ipad.output_height == 1080);
    assert(full_ipad.output_width == theft4_width_for_native_aspect(1080, 2420, 1668));

    for (const auto display : {std::pair<unsigned,unsigned>{2420,1668},
                               {2736,1260}, {0,0}}) {
        const auto native = theft4_output_policy_for_lab(
            THEFT4_LAB_NATIVE_16_9, true, display.first, display.second);
        const unsigned expected_width = display.first ? display.first : 1920;
        const unsigned expected_height = display.second ? display.second : 1080;
        assert(native.render_width == expected_width);
        assert(native.render_height == expected_height);
        assert(native.output_width == native.render_width);
        assert(native.output_height == native.render_height);
        assert(native.video_width == native.render_width);
        assert(native.video_height == native.render_height);
        assert(!native.fsr1);
    }

    puts("Lab 540p/720p/900p/1080p/native-pixel full-aspect + FSR policies passed");
}
