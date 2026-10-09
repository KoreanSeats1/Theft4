#pragma once

#include <stdbool.h>
#include <stdint.h>

// An explicit native-pixel launcher choice, not a fallback for missing/older preferences.
#define THEFT4_LAB_NATIVE_16_9 UINT32_MAX

// Shared by the Objective-C launcher/Metal layer and C++ runtime startup.
// Original choices retain centered 16:9. The optional viewport mod expands it.
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
    bool native_aspect;
    // Exact physical shape, independent of rounded lower-resolution targets.
    uint32_t aspect_width;
    uint32_t aspect_height;
    double safe_left, safe_top, safe_right, safe_bottom;
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

// Fixed scene budgets keep their authored 16:9 shape.
static inline uint32_t theft4_width_for_native_aspect(
    uint32_t height, uint32_t native_width, uint32_t native_height) {
    (void)native_width;
    (void)native_height;
    return height * 16 / 9;
}

static inline theft4_output_policy theft4_output_policy_for_lab(
    uint32_t render_height, bool fsr1, uint32_t native_width, uint32_t native_height) {
    const uint32_t height = theft4_lab_render_height(render_height);
    if (height == THEFT4_LAB_NATIVE_16_9) {
        // Largest exact 16:9 physical-pixel extent that fits the drawable.
        uint32_t units = native_width / 16;
        if (native_height / 9 < units) units = native_height / 9;
        if (!units) units = 120;
        const uint32_t width = units * 16;
        const uint32_t native_fit_height = units * 9;
        theft4_output_policy policy = {width, native_fit_height, width, native_fit_height,
                                      false, width, native_fit_height};
        return policy;
    }
    const uint32_t width = theft4_width_for_native_aspect(
        height, native_width, native_height);
    theft4_output_policy policy = {width, height, width, height, fsr1, width, height};
    if (fsr1) {
        const theft4_output_policy fit = theft4_output_policy_for_mode(
            THEFT4_OUTPUT_FSR_BOOST, native_width, native_height);
        policy.output_width = fit.output_width;
        policy.output_height = fit.output_height;
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

// Expand the original 16:9 rectangle rather than fitting it into a smaller
// native-shaped rectangle. Retain the center's selected pixel density.
static inline void theft4_expand_extent(uint32_t *width, uint32_t *height,
    uint32_t display_width, uint32_t display_height, uint32_t maximum) {
    if (!display_width || !display_height || !*width || !*height) return;
    uint64_t w = *width, h = *height;
    if (w * display_height > h * display_width)
        h = (w * display_height + display_width / 2) / display_width;
    else
        w = (h * display_width + display_height / 2) / display_height;
    if (w > maximum || h > maximum) {
        if (w >= h) {
            h = h * maximum / w;
            w = maximum;
        } else {
            w = w * maximum / h;
            h = maximum;
        }
    }
    *width = (uint32_t)(w ? w : 1);
    *height = (uint32_t)(h ? h : 1);
}

// Match the render-graph selection once. Keep the unfitted logical video budget:
// fitting an integer extent twice can trim a different edge on the second fit.
static inline theft4_output_policy theft4_output_policy_for_native_aspect_lab(
    uint32_t render_height, bool fsr1, uint32_t native_width,
    uint32_t native_height, bool fixed_1080_output_profile) {
    theft4_output_policy policy = fixed_1080_output_profile
        ? theft4_output_policy_for_fixed_1080_lab_selected_aspect(
            render_height, fsr1, native_width, native_height)
        : theft4_output_policy_for_lab(render_height, fsr1, native_width, native_height);
    if (!native_width || !native_height) return policy;
    policy.native_aspect = true;
    policy.aspect_width = native_width;
    policy.aspect_height = native_height;
    if (theft4_lab_render_height(render_height) == THEFT4_LAB_NATIVE_16_9) {
        policy.render_width = native_width;
        policy.render_height = native_height;
        theft4_expand_extent(&policy.render_width, &policy.render_height,
            native_width, native_height, 4095);
    } else {
        theft4_expand_extent(&policy.render_width, &policy.render_height,
            native_width, native_height, policy.fsr1 ? 2730 : 4095);
    }
    if (policy.fsr1) {
        theft4_expand_extent(&policy.output_width, &policy.output_height,
            native_width, native_height, 4095);
        policy.video_width = policy.render_width * 3 / 2;
        policy.video_height = policy.render_height * 3 / 2;
    } else {
        policy.video_width = policy.render_width;
        policy.video_height = policy.render_height;
    }
    uint32_t width = policy.video_width < 640 ? 640 : policy.video_width;
    uint32_t height = policy.video_height < 480 ? 480 : policy.video_height;
    // Match LimitExtent followed by SelectExtent("auto", configured display).
    if (width > 4095 || height > 4095) {
        if (width >= height) {
            height = (uint32_t)((uint64_t)height * 4095 / width);
            width = 4095;
        } else {
            width = (uint32_t)((uint64_t)width * 4095 / height);
            height = 4095;
        }
    }
    if ((uint64_t)width * native_height > (uint64_t)height * native_width)
        width = (uint32_t)((uint64_t)height * native_width / native_height);
    else
        height = (uint32_t)((uint64_t)width * native_height / native_width);
    if (!width) width = 1;
    if (!height) height = 1;
    if (policy.fsr1 && (width * 2 + 1) / 3 >= 640 && (height * 2 + 1) / 3 >= 360) {
        policy.render_width = (width * 2 + 1) / 3;
        policy.render_height = (height * 2 + 1) / 3;
    } else {
        policy.fsr1 = false;
        policy.render_width = width;
        policy.render_height = height;
        policy.output_width = width;
        policy.output_height = height;
    }
    return policy;
}
