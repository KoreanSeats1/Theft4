#pragma once

#include <cstdint>
#include <string_view>

namespace theft4::motion_blur {
inline constexpr uint32_t kCompositeCaller = 0x822D0C48;
inline constexpr uint32_t kTechniqueOffset = 732;

// TU8 sub_822CFC00 selects base+11 with directional blur, base+10
// without it. The base preserves DOF (0/2), noise (14/16), or the
// alternate composite (18). Never clear arbitrary odd-numbered passes.
constexpr uint32_t WithoutBlur(uint32_t pass) noexcept {
    switch (pass) {
        case 11: case 13: case 25: case 27: case 29: return pass - 1;
        default: return pass;
    }
}

constexpr uint32_t SelectPass(uint32_t requested, bool enabled, uint32_t caller,
                              uint32_t target, bool valid_effect) noexcept {
    if (enabled || caller != kCompositeCaller || target != 0 || !valid_effect)
        return requested;
    return WithoutBlur(requested);
}

// Launcher persists the choice; absent environment keeps the stock appearance.
// -1 rejects malformed diagnostic overrides instead of silently disabling blur.
constexpr int ParseSetting(const char* value) noexcept {
    if (!value || std::string_view(value) == "1") return 1;
    if (std::string_view(value) == "0") return 0;
    return -1;
}

void Configure(bool enabled, bool trace) noexcept;
}  // namespace theft4::motion_blur
