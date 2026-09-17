#include "theft4_motion_blur.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <utility>

int main() {
    namespace blur = theft4::motion_blur;
    assert(blur::ParseSetting(nullptr) == 1);
    assert(blur::ParseSetting("1") == 1);
    assert(blur::ParseSetting("0") == 0);
    for (auto value : {"", "off", "false", "true", "2", "01"})
        assert(blur::ParseSetting(value) == -1);
    // Explicit pairs, not the implementation's arithmetic as the test oracle.
    constexpr std::pair<uint32_t, uint32_t> pairs[] = {
        {11, 10}, {13, 12}, {25, 24}, {27, 26}, {29, 28}};
    for (uint32_t pass = 0; pass < 128; ++pass) {
        uint32_t expected = pass;
        for (auto [source, target] : pairs)
            if (pass == source) expected = target;
        for (bool enabled : {false, true}) {
            for (bool valid : {false, true}) {
                for (uint32_t caller : {0u, blur::kCompositeCaller, 0x822CF9D0u}) {
                    for (uint32_t target : {0u, 1u, 0xFFFFFFFFu}) {
                        const auto selected = blur::SelectPass(pass, enabled, caller, target, valid);
                        assert(selected == (!enabled && valid && caller == blur::kCompositeCaller &&
                                            target == 0 ? expected : pass));
                        assert(blur::SelectPass(selected, enabled, caller, target, valid) == selected);
                    }
                }
            }
        }
    }
    assert(blur::SelectPass(0xFFFFFFFFu, false, blur::kCompositeCaller, 0, true) == 0xFFFFFFFFu);
    puts("Motion blur pass pairs, default-on, validation and no-op guards passed");
}
