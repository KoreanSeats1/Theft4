#pragma once

#include <stdbool.h>
#include <stdint.h>

// An explicit native-pixel launcher choice, not a fallback for missing/older preferences.
#define THEFT4_LAB_NATIVE_16_9 UINT32_MAX

// Shared by the Objective-C launcher/Metal layer and C++ runtime startup.
// Lab modes follow the device aspect; legacy modes retain their 16:9 budget.
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
// select 720p. The Lab launcher exposes these four scene budgets independently
// of FSR; the ordinary launcher's legacy policy above is unchanged.
static inline uint32_t theft4_lab_render_height(uint32_t height) {
    return height == 540 || height == 900 || height == 1080 ||
        height == THEFT4_LAB_NATIVE_16_9 ? height : 720;
}

// GTA IV's scene budget is selected by height. Shape it to the actual device
// instead of forcing 16:9; the aspect hooks keep HUD geometry in its authored
// 16:9 coordinate space while the 3D camera expands to the full display.
static inline uint32_t theft4_width_for_native_aspect(
    uint32_t height, uint32_t native_width, uint32_t native_height) {
    if (!native_width || !native_height) return height * 16 / 9;
    const uint64_t numerator = (uint64_t)height * native_width + native_height / 2;
    const uint64_t width = numerator / native_height;
    return width ? (uint32_t)width : 1;
}

static inline theft4_output_policy theft4_output_policy_for_lab(
    uint32_t render_height, bool fsr1, uint32_t native_width, uint32_t native_height) {
    const uint32_t height = theft4_lab_render_height(render_height);
    if (height == THEFT4_LAB_NATIVE_16_9) {
        // Native means the full physical device extent and aspect. This path
        // does not upscale, even if an old FSR preference was left enabled.
        const uint32_t width = native_width ? native_width : 1920;
        const uint32_t native_fit_height = native_height ? native_height : 1080;
        theft4_output_policy policy = {width, native_fit_height, width, native_fit_height,
                                      false, width, native_fit_height};
        return policy;
    }
    const uint32_t width = theft4_width_for_native_aspect(
        height, native_width, native_height);
    theft4_output_policy policy = {width, height, width, height, fsr1, width, height};
    if (fsr1) {
        policy.output_width = native_width ? native_width : width * 3 / 2;
        policy.output_height = native_height ? native_height : height * 3 / 2;
        // The existing native hooks divide the logical video mode by 1.5 for
        // FSR Quality. Preserve the selected scene height and native shape
        // independently of the drawable's physical pixel count.
        policy.video_width = width * 3 / 2;
        policy.video_height = height * 3 / 2;
    }
    return policy;
}

// Fixed-1080p phone profiles do not allocate a native-resolution presentation
// target. Lower scene modes can use FSR while staying inside that display budget.
static inline theft4_output_policy theft4_output_policy_for_fixed_1080_lab_selected(
    uint32_t render_height, bool fsr1) {
    const uint32_t height = theft4_lab_render_height(render_height);
    if (height == THEFT4_LAB_NATIVE_16_9)
        return theft4_output_policy_for_lab(height, false, 1920, 1080);
    return theft4_output_policy_for_lab(height, fsr1, 1920, 1080);
}

static inline theft4_output_policy theft4_output_policy_for_fixed_1080_lab_selected_aspect(
    uint32_t render_height, bool fsr1, uint32_t native_width, uint32_t native_height) {
    const uint32_t height = theft4_lab_render_height(render_height);
    if (height == THEFT4_LAB_NATIVE_16_9)
        return theft4_output_policy_for_lab(height, false, native_width, native_height);
    theft4_output_policy policy = theft4_output_policy_for_lab(
        height, fsr1, native_width, native_height);
    policy.output_height = 1080;
    policy.output_width = theft4_width_for_native_aspect(
        policy.output_height, native_width, native_height);
    return policy;
}

static inline theft4_output_policy theft4_output_policy_for_a19_lab_selected(
    uint32_t render_height, bool fsr1) {
    return theft4_output_policy_for_fixed_1080_lab_selected(render_height, fsr1);
}

static inline theft4_output_policy theft4_output_policy_for_a19_lab(
    uint32_t render_height) {
    const uint32_t height = theft4_lab_render_height(render_height);
    return theft4_output_policy_for_a19_lab_selected(height, height < 1080);
}
