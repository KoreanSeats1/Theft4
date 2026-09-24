#include "theft4_metal_presenter.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <os/lock.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include "theft4_frame_time_history.h"
#include "theft4_publication_trace.h"
#include "theft4_frame_stage_trace.h"

namespace {

os_unfair_lock presenter_lock = OS_UNFAIR_LOCK_INIT;
CAMetalLayer* bound_layer = nil;
id<MTLDevice> metal_device = nil;
id<MTLCommandQueue> command_queue = nil;
id<MTLBuffer> guest_memory_buffer = nil;
id<MTLBuffer> edram_buffer = nil;
id<MTLCommandBuffer> frame_command_buffer = nil;
dispatch_semaphore_t frame_slots = nil;
std::atomic<uint64_t> submitted_frames{0};
std::atomic<uint64_t> completed_frames{0};
std::atomic<uint64_t> published_game_frames{0};
theft4::FrameTimeHistory<> frame_time_history;
theft4::PublicationTrace<> publication_trace;
theft4::FrameStageTrace<> frame_stage_trace;
std::atomic<int64_t> display_target_ns{0};

// Protected by presenter_lock. Latched before the game renderer is created.
theft4_output_policy launch_output = theft4_output_policy_for_enhanced(true);

void SetOutputPolicy(theft4_output_policy output) {
  os_unfair_lock_lock(&presenter_lock);
  launch_output = output;
  if (bound_layer) {
    bound_layer.contentsScale = 1.0;
    bound_layer.drawableSize = CGSizeMake(output.output_width, output.output_height);
  }
  os_unfair_lock_unlock(&presenter_lock);
}

void EnsureDeviceLocked() {
  if (!metal_device) metal_device = MTLCreateSystemDefaultDevice();
  if (metal_device && !command_queue) {
    command_queue = [metal_device newCommandQueue];
    command_queue.label = @"Theft4 Xenos Queue";
  }
}

id<MTLCommandBuffer> EnsureFrameCommandBufferLocked() {
  EnsureDeviceLocked();
  if (!command_queue) return nil;
  if (!frame_slots) frame_slots = dispatch_semaphore_create(3);
  if (!frame_command_buffer) {
    // Keep at most three guest frames in flight. This is the same lifetime
    // boundary that future EDRAM, texture and shader work will use.
    dispatch_semaphore_wait(frame_slots, DISPATCH_TIME_FOREVER);
    frame_command_buffer = [command_queue commandBuffer];
    if (!frame_command_buffer) {
      dispatch_semaphore_signal(frame_slots);
      return nil;
    }
    frame_command_buffer.label = @"Theft4 Guest Frame";
  }
  return frame_command_buffer;
}

}  // namespace

void theft4_metal_bind_layer(void* raw_layer) {
  CAMetalLayer* layer = (__bridge CAMetalLayer*)raw_layer;
  if (!layer) return;

  os_unfair_lock_lock(&presenter_lock);
  EnsureDeviceLocked();
  layer.device = metal_device;
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  layer.framebufferOnly = YES;
  layer.opaque = YES;
  layer.contentsGravity = kCAGravityResizeAspect;
  layer.allowsNextDrawableTimeout = YES;
  layer.presentsWithTransaction = NO;
  bound_layer = layer;
  os_unfair_lock_unlock(&presenter_lock);
}

void theft4_metal_unbind_layer(void* raw_layer) {
  CAMetalLayer* layer = (__bridge CAMetalLayer*)raw_layer;
  os_unfair_lock_lock(&presenter_lock);
  if (!layer || bound_layer == layer) bound_layer = nil;
  os_unfair_lock_unlock(&presenter_lock);
}

void theft4_metal_resize_layer(void* raw_layer, double width, double height,
                               double scale) {
  CAMetalLayer* layer = (__bridge CAMetalLayer*)raw_layer;
  if (!layer || width <= 0.0 || height <= 0.0 || scale <= 0.0) return;
  // UIKit owns placement, but never silently changes the chosen pixel budget.
  const auto output = theft4_metal_get_output_policy();
  layer.contentsScale = 1.0;
  layer.drawableSize = CGSizeMake(output.output_width, output.output_height);
}

void theft4_metal_set_output_mode(theft4_output_mode mode,
                                uint32_t native_width, uint32_t native_height) {
  SetOutputPolicy(theft4_output_policy_for_mode(mode, native_width, native_height));
}

void theft4_metal_set_lab_output(uint32_t render_height, bool fsr1,
                                uint32_t native_width, uint32_t native_height,
                                bool fixed_1080_output_profile) {
  SetOutputPolicy(fixed_1080_output_profile
      && render_height != THEFT4_LAB_NATIVE_16_9
      ? theft4_output_policy_for_fixed_1080_lab_selected_aspect(
            render_height, fsr1, native_width, native_height)
      : theft4_output_policy_for_lab(render_height, fsr1, native_width, native_height));
}

theft4_output_policy theft4_metal_get_output_policy(void) {
  os_unfair_lock_lock(&presenter_lock);
  const auto output = launch_output;
  os_unfair_lock_unlock(&presenter_lock);
  return output;
}

bool theft4_metal_has_layer(void) {
  os_unfair_lock_lock(&presenter_lock);
  const bool available = bound_layer && metal_device && command_queue;
  os_unfair_lock_unlock(&presenter_lock);
  return available;
}

uint32_t theft4_platform_thermal_state(void) {
  return static_cast<uint32_t>(NSProcessInfo.processInfo.thermalState);
}

void* theft4_metal_bound_layer(void) {
  os_unfair_lock_lock(&presenter_lock);
  CAMetalLayer* layer = bound_layer;
  os_unfair_lock_unlock(&presenter_lock);
  return (__bridge void*)layer;
}

bool theft4_metal_bound_layer_size(uint32_t* width, uint32_t* height) {
  if (!width || !height) return false;
  os_unfair_lock_lock(&presenter_lock);
  CAMetalLayer* layer = bound_layer;
  const CGSize drawable_size = layer ? layer.drawableSize : CGSizeZero;
  os_unfair_lock_unlock(&presenter_lock);
  if (drawable_size.width < 1.0 || drawable_size.height < 1.0) {
    *width = 0;
    *height = 0;
    return false;
  }
  *width = (uint32_t)drawable_size.width;
  *height = (uint32_t)drawable_size.height;
  return true;
}

bool theft4_metal_present_clear(double red, double green, double blue,
                                double alpha) {
  @autoreleasepool {
    os_unfair_lock_lock(&presenter_lock);
    CAMetalLayer* layer = bound_layer;
    id<MTLCommandQueue> queue = command_queue;
    os_unfair_lock_unlock(&presenter_lock);
    if (!layer || !queue) return false;

    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) return false;
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(red, green, blue, alpha);

    id<MTLCommandBuffer> buffer = [queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [buffer renderCommandEncoderWithDescriptor:pass];
    [encoder endEncoding];
    [buffer presentDrawable:drawable];
    [buffer commit];
    return true;
  }
}

void theft4_frame_counter_note_published(void) {
  const uint64_t frame = published_game_frames.fetch_add(1, std::memory_order_relaxed) + 1;
  if (frame_time_history.Enabled() || publication_trace.Enabled()) {
    const uint64_t now_ns = uint64_t(CACurrentMediaTime() * 1e9);
    if (frame_time_history.Enabled()) frame_time_history.Record(now_ns);
    publication_trace.Record(frame, now_ns);
  }
}

uint64_t theft4_publication_capture_start(void) { return publication_trace.Start(); }
void theft4_publication_capture_stop(void) {
  publication_trace.Stop();
  frame_stage_trace.Stop();
}
uint64_t theft4_frame_stages_start(void) { return frame_stage_trace.Start(); }
void theft4_frame_stages_stop(void) { frame_stage_trace.Stop(); }
void theft4_frame_stage_record(uint32_t stage, uint64_t frame, uint64_t a, uint64_t b) {
  if (!frame_stage_trace.Enabled()) return;
  frame_stage_trace.Record({stage, frame, uint64_t(CACurrentMediaTime() * 1e9), a, b});
}
uint32_t theft4_frame_stages_read(uint64_t* cursor, theft4_frame_stage_sample* samples,
    uint32_t capacity, uint64_t* lost) {
  if (!cursor || !samples || !lost) return 0;
  // Copy explicitly across the C ABI; no type-punning of distinct struct types.
  theft4::FrameStageSample batch[256];
  const auto n = frame_stage_trace.CopyAfter(*cursor, batch, std::min(capacity, 256u), *lost);
  for (size_t i = 0; i < n; ++i)
    samples[i] = {batch[i].stage, batch[i].frame, batch[i].monotonic_ns, batch[i].a, batch[i].b};
  return uint32_t(n);
}
void theft4_pacing_note_display_target(double target_timestamp) {
  const double now = CACurrentMediaTime();
  const double delta = target_timestamp - now;
  if (!std::isfinite(delta) || delta < -0.1 || delta > 0.1) {
    display_target_ns.store(0, std::memory_order_release);
    return;
  }
  const auto steady = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  display_target_ns.store(steady + int64_t(delta * 1e9), std::memory_order_release);
  theft4_frame_stage_record(THEFT4_STAGE_DISPLAY_TARGET, 0, uint64_t(target_timestamp * 1e9), 0);
}
int64_t theft4_pacing_display_target_ns(void) {
  const auto target = display_target_ns.load(std::memory_order_acquire);
  const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  return target > 0 && target > now - 100'000'000 && target < now + 100'000'000 ? target : 0;
}
uint32_t theft4_publication_capture_read(uint64_t* cursor,
    theft4_publication_sample* samples, uint32_t capacity, uint64_t* lost) {
  if (!cursor || !samples || !lost || !capacity) return 0;
  static_assert(sizeof(theft4_publication_sample) == sizeof(theft4::PublicationSample));
  return static_cast<uint32_t>(publication_trace.CopyAfter(*cursor,
      reinterpret_cast<theft4::PublicationSample*>(samples), capacity, *lost));
}

void theft4_frame_time_set_enabled(bool enabled) {
  frame_time_history.SetEnabled(enabled);
}

void theft4_frame_time_copy(theft4_frame_time_snapshot* snapshot) {
  if (!snapshot) return;
  snapshot->count = (uint32_t)frame_time_history.Copy(
      uint64_t(CACurrentMediaTime() * 1e9), snapshot->milliseconds,
      THEFT4_FRAME_TIME_SAMPLES, snapshot->pending_ms);
}

uint64_t theft4_frame_counter_published_frames(void) {
  return published_game_frames.load(std::memory_order_relaxed);
}

bool theft4_metal_renderer_initialize(uint64_t guest_memory_size,
                                      uint64_t edram_size) {
  @autoreleasepool {
    os_unfair_lock_lock(&presenter_lock);
    EnsureDeviceLocked();
    if (!metal_device || !command_queue) {
      os_unfair_lock_unlock(&presenter_lock);
      return false;
    }

    if (!guest_memory_buffer) {
      guest_memory_buffer = [metal_device
          newBufferWithLength:(NSUInteger)guest_memory_size
                      options:MTLResourceStorageModeShared |
                              MTLResourceCPUCacheModeDefaultCache];
      guest_memory_buffer.label = @"Theft4 Xbox 360 Shared Memory";
    }
    if (!edram_buffer) {
      // Shared storage is deliberate for bring-up: it permits snapshots and
      // validation without an extra private-to-shared readback allocation.
      edram_buffer = [metal_device
          newBufferWithLength:(NSUInteger)edram_size
                      options:MTLResourceStorageModeShared |
                              MTLResourceCPUCacheModeDefaultCache];
      edram_buffer.label = @"Theft4 Xenos EDRAM";
    }
    if (!frame_slots) frame_slots = dispatch_semaphore_create(3);
    const bool ready = guest_memory_buffer && edram_buffer && frame_slots;
    os_unfair_lock_unlock(&presenter_lock);
    return ready;
  }
}

void theft4_metal_renderer_shutdown(void) {
  @autoreleasepool {
    os_unfair_lock_lock(&presenter_lock);
    id<MTLCommandBuffer> pending = frame_command_buffer;
    frame_command_buffer = nil;
    guest_memory_buffer = nil;
    edram_buffer = nil;
    frame_slots = nil;
    os_unfair_lock_unlock(&presenter_lock);
    if (pending) {
      [pending commit];
      [pending waitUntilCompleted];
    }
  }
}

bool theft4_metal_renderer_note_draw(void) {
  @autoreleasepool {
    os_unfair_lock_lock(&presenter_lock);
    const bool ready = guest_memory_buffer && edram_buffer &&
                       EnsureFrameCommandBufferLocked();
    os_unfair_lock_unlock(&presenter_lock);
    return ready;
  }
}

bool theft4_metal_renderer_end_frame(uint32_t frontbuffer_ptr,
                                     uint32_t frontbuffer_width,
                                     uint32_t frontbuffer_height) {
  @autoreleasepool {
    (void)frontbuffer_ptr;
    (void)frontbuffer_width;
    (void)frontbuffer_height;

    os_unfair_lock_lock(&presenter_lock);
    id<MTLCommandBuffer> buffer = EnsureFrameCommandBufferLocked();
    if (!buffer) {
      os_unfair_lock_unlock(&presenter_lock);
      return false;
    }
    frame_command_buffer = nil;
    dispatch_semaphore_t slots = frame_slots;
    const uint64_t frame = submitted_frames.fetch_add(1) + 1;
    buffer.label = [NSString stringWithFormat:@"Theft4 Guest Frame %llu",
                                               (unsigned long long)frame];
    [buffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
      (void)completed;
      completed_frames.fetch_add(1);
      dispatch_semaphore_signal(slots);
    }];
    [buffer commit];
    os_unfair_lock_unlock(&presenter_lock);
    return true;
  }
}

uint64_t theft4_metal_renderer_submitted_frames(void) {
  return submitted_frames.load();
}

uint64_t theft4_metal_renderer_completed_frames(void) {
  return completed_frames.load();
}
