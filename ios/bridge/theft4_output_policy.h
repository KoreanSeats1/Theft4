#pragma once

#include <stdbool.h>
#include <stdint.h>

// Shared by the Objective-C launcher/Metal layer and C++ runtime startup.
// All modes preserve 16:9. Legacy modes retain their fixed 720p scene budget.
typedef enum theft4_output_mode {
    THEFT4_OUTPUT_720P,
    THEFT4_OUTPUT_FSR_1080P,
    THEFT4_OUTPUT_FSR_BOOST
} theft4_output_mode;

typedef struct theft4_output_policy {
    uint32_t render_width;
    uint32_t render_height;
    uint32_t output_width;
    uint32_t output_height;
    bool fsr1;
    // Logical game video mode, NOT the physical swapchain size. FSR Quality
    // hooks divide these by 1.5 to select the internal scene resolution.
    uint32_t video_width;
    uint32_t video_height;
} theft4_output_policy;

static inline theft4_output_policy theft4_output_policy_for_mode(
    theft4_output_mode mode, uint32_t native_width, uint32_t native_height) {
    const bool enhanced = mode != THEFT4_OUTPUT_720P;
    theft4_output_policy policy = {
        1280, 720, enhanced ? 1920u : 1280u, enhanced ? 1080u : 720u, enhanced,
        enhanced ? 1920u : 1280u, enhanced ? 1080u : 720u
    };
    if (mode == THEFT4_OUTPUT_FSR_BOOST) {
        uint32_t units = native_width / 16;
        if (native_height / 9 < units) units = native_height / 9;
        // Exact integer 16:9; retain 1080p in small windows/unavailable sizes.
        // Bound memory and upscaling work on future ultra-high-res displays.
        if (units < 120) units = 120;
        if (units > 240) units = 240;
        policy.output_width = units * 16;
        policy.output_height = units * 9;
    }
    return policy;
}

static inline theft4_output_policy theft4_output_policy_for_enhanced(bool enhanced) {
    return theft4_output_policy_for_mode(
        enhanced ? THEFT4_OUTPUT_FSR_1080P : THEFT4_OUTPUT_720P, 0, 0);
}

// Persist the height rather than a UI index, so invalid/older values safely
// select 720p. The Lab launcher exposes these three scene budgets independently
// of FSR; the ordinary launcher's legacy policy above is unchanged.
static inline uint32_t theft4_lab_render_height(uint32_t height) {
    return height == 900 || height == 1080 ? height : 720;
}

static inline theft4_output_policy theft4_output_policy_for_lab(
    uint32_t render_height, bool fsr1, uint32_t native_width, uint32_t native_height) {
    const uint32_t height = theft4_lab_render_height(render_height);
    const uint32_t width = height * 16 / 9;
    theft4_output_policy policy = {width, height, width, height, fsr1, width, height};
    if (fsr1) {
        const theft4_output_policy native_fit = theft4_output_policy_for_mode(
            THEFT4_OUTPUT_FSR_BOOST, native_width, native_height);
        policy.output_width = native_fit.output_width;
        policy.output_height = native_fit.output_height;
        // The existing native hooks divide the logical video mode by 1.5 for
        // FSR Quality. This preserves the explicitly selected scene resolution
        // regardless of the actual drawable size (including small windows).
        policy.video_width = width * 3 / 2;
        policy.video_height = height * 3 / 2;
    }
    return policy;
}
