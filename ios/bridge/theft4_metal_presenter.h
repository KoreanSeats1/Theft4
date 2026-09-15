#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The pointer is a CAMetalLayer owned by the UIKit view hierarchy. The bridge
// retains it only while bound and never assumes ownership of the UIView.
void theft4_metal_bind_layer(void* layer);
void theft4_metal_unbind_layer(void* layer);
void theft4_metal_resize_layer(void* layer, double width, double height,
                               double scale);
bool theft4_metal_has_layer(void);
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

#ifdef __cplusplus
}
#endif
