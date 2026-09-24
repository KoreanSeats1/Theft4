#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "theft4_output_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

// The pointer is a CAMetalLayer owned by the UIKit view hierarchy. The bridge
// retains it only while bound and never assumes ownership of the UIView.
void theft4_metal_bind_layer(void* layer);
void theft4_metal_unbind_layer(void* layer);
// Main thread, before runtime/presenter creation only. Latches the launch mode;
// subsequent UIKit layout keeps this size rather than reverting to 720p.
void theft4_metal_set_output_mode(theft4_output_mode mode,
                                  uint32_t native_width, uint32_t native_height);
void theft4_metal_set_lab_output(uint32_t render_height, bool fsr1,
                                uint32_t native_width, uint32_t native_height,
                                bool fixed_1080_output_profile);
theft4_output_policy theft4_metal_get_output_policy(void);
void theft4_metal_resize_layer(void* layer, double width, double height,
                               double scale);
bool theft4_metal_has_layer(void);
// NSProcessInfoThermalState as a stable integer: 0 nominal, 1 fair, 2 serious,
// 3 critical. This is intentionally a low-frequency diagnostic sample only.
uint32_t theft4_platform_thermal_state(void);
// Borrowed pointer retained by the UIKit bridge while bound. Used only to
// create the MoltenVK surface; ownership remains with the view hierarchy.
void* theft4_metal_bound_layer(void);
bool theft4_metal_bound_layer_size(uint32_t* width, uint32_t* height);
bool theft4_metal_present_clear(double red, double green, double blue,
                                double alpha);

// Initializes the persistent resources used by the embedded Xenos renderer.
// Guest RAM is mirrored in a Metal shared buffer, while EDRAM uses the native
// 10 MiB Xenos allocation size. These resources intentionally live beside the
// presenter so the command processor never owns UIKit or Objective-C objects.
bool theft4_metal_renderer_initialize(uint64_t guest_memory_size,
                                      uint64_t edram_size);
void theft4_metal_renderer_shutdown(void);

// Opens a Metal command buffer for the current guest frame and closes it at
// the corresponding XE_SWAP. Draw translation will encode into this same
// lifecycle as individual pipeline stages are brought online.
bool theft4_metal_renderer_note_draw(void);
bool theft4_metal_renderer_end_frame(uint32_t frontbuffer_ptr,
                                     uint32_t frontbuffer_width,
                                     uint32_t frontbuffer_height);

uint64_t theft4_metal_renderer_submitted_frames(void);
uint64_t theft4_metal_renderer_completed_frames(void);

// Counts distinct content publications successfully handed to the iOS Vulkan/Metal
// swapchain. UIKit samples this monotonically increasing value for the small
// on-screen FPS indicator; no logging or GPU readback is involved.
void theft4_frame_counter_note_published(void);
uint64_t theft4_frame_counter_published_frames(void);

// Publication-time trace. It does not measure physical display scanout or
// GPU work. Drain on a background queue and checkpoint to disk periodically.
typedef struct theft4_publication_sample {
  uint64_t frame;
  uint64_t monotonic_ns;
} theft4_publication_sample;
uint64_t theft4_publication_capture_start(void);
void theft4_publication_capture_stop(void);
uint32_t theft4_publication_capture_read(uint64_t* cursor,
    theft4_publication_sample* samples, uint32_t capacity, uint64_t* lost);

// Stage rows use guest frame IDs, unlike the publication counter above.
// a/b: submit=end success/0; worker-end=slot/0; worker-begin=0/0; queue=submission/slot;
// fence=submission/slot (frame 0); present=content sequence/result;
// constants=reuses/changed-version reuses; fallback=count/arena bytes.
enum theft4_frame_stage {
  THEFT4_STAGE_GUEST_SUBMIT_BEGIN = 1, THEFT4_STAGE_GUEST_SUBMIT_END = 2,
  THEFT4_STAGE_LIMITER_END = 3, THEFT4_STAGE_WORKER_BEGIN = 4,
  THEFT4_STAGE_WORKER_END = 5, THEFT4_STAGE_QUEUE_SUBMIT_BEGIN = 6,
  THEFT4_STAGE_QUEUE_SUBMIT_END = 7, THEFT4_STAGE_FENCE_BEGIN = 8,
  THEFT4_STAGE_FENCE_END = 9, THEFT4_STAGE_PRESENT_QUEUED = 10,
  THEFT4_STAGE_CONSTANT_REUSE = 11, THEFT4_STAGE_CONSTANT_FALLBACK = 12,
  THEFT4_STAGE_DISPLAY_TARGET = 13
};
typedef struct theft4_frame_stage_sample {
  uint64_t stage, frame, monotonic_ns, a, b;
} theft4_frame_stage_sample;
uint64_t theft4_frame_stages_start(void);
void theft4_frame_stages_stop(void);
void theft4_frame_stage_record(uint32_t stage, uint64_t frame, uint64_t a, uint64_t b);
uint32_t theft4_frame_stages_read(uint64_t* cursor, theft4_frame_stage_sample* samples,
    uint32_t capacity, uint64_t* lost);
// UIKit display-link prediction, not a GPU completion or presented callback.
void theft4_pacing_note_display_target(double target_timestamp);
int64_t theft4_pacing_display_target_ns(void); // steady_clock domain; zero if stale.

// Rolling publication intervals, not physical display scanout times. UI control
// and snapshots are main-thread only; the presenter is the sole sample writer.
#define THEFT4_FRAME_TIME_SAMPLES 180
typedef struct theft4_frame_time_snapshot {
  uint32_t count;
  double milliseconds[THEFT4_FRAME_TIME_SAMPLES];
  double pending_ms;
} theft4_frame_time_snapshot;
void theft4_frame_time_set_enabled(bool enabled);
void theft4_frame_time_copy(theft4_frame_time_snapshot* snapshot);

#ifdef __cplusplus
}
#endif
