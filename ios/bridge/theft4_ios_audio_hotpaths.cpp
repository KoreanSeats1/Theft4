#include <arm_neon.h>
#include <pthread.h>
#include <sched.h>
#include <sys/qos.h>
#include <time.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <rex/logging.h>

#include "gta4_init.h"
#include "theft4_audio_gain_ramp.h"

namespace {

constexpr size_t kMixBytes = 1024;
constexpr size_t kVectorBytes = 16;

std::atomic<uint32_t> g_mix_ramp_validation{0};
std::atomic<bool> g_nonzero_ramp_validated{false};
std::atomic<bool> g_queue_backpressure_reported{false};
std::atomic<bool> g_fence_backpressure_reported{false};

using theft4::audio::MixGainRamp;

uint8_t* GuestAddress(uint8_t* base, uint32_t address) {
  return REX_RAW_ADDR(address);
}

}  // namespace

// Keep this translation unit resident in the static iOS bridge archive so its
// strong entry point replaces the generated weak wrapper.
extern "C" void theft4_ios_audio_hotpaths_link_anchor() {}

// GTA IV performs its software DSP mix on this dedicated guest thread. Give
// that host thread interactive QoS so Metal command translation and shader
// compilation can't starve audio production. The generated AOT body remains
// authoritative; this wrapper changes scheduling only.
extern "C" void sub_821909D0(PPCContext& ctx, uint8_t* base) {
  const int result =
      pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  if (result == 0) {
    REXLOG_INFO("Theft4 GTA audio mixer promoted to user-interactive QoS");
  } else {
    REXLOG_WARN("Theft4 could not promote GTA audio mixer QoS: {}", result);
  }
  __imp__sub_821909D0(ctx, base);
}

// GTA IV polls this three-word queue snapshot until fewer than two entries are
// pending. The original Xenon loop is a busy wait, and on iPad it can occupy an
// entire core while starving both the queue consumer and the audio mixer.
// Preserve the guest-visible register and memory effects exactly, but add a
// cheap ARM spin hint and a brief sleep after sustained backpressure. A plain
// sched_yield remained a major CPU cost in the on-device profile. Restrict the
// sleep to the confirmed retry callsite, never other snapshot consumers.
extern "C" void sub_82A46D70(PPCContext& ctx, uint8_t* base) {
  (void)base;

  ctx.r11.s64 = 0;
  REX_STORE_U32(ctx.r4.u32, ctx.r11.u32);
  ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 16544);
  REX_STORE_U32(ctx.r4.u32 + 4, ctx.r11.u32);
  ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 16552);
  ctx.r10.u64 = REX_LOAD_U32(ctx.r3.u32 + 16544);
  ctx.r11.u64 = ctx.r10.u64 - ctx.r11.u64;
  REX_STORE_U32(ctx.r4.u32 + 8, ctx.r11.u32);

  static thread_local uint32_t backpressure_polls = 0;
  static const bool backoff_enabled = [] {
    const char* setting = std::getenv("THEFT4_QUEUE_BACKOFF");
    return !setting || std::strcmp(setting, "0") != 0;
  }();
  if (ctx.r11.u32 >= 2 && ctx.lr == 0x828BF480) {
    if (!g_queue_backpressure_reported.exchange(true,
                                                 std::memory_order_relaxed)) {
      REXLOG_INFO("Theft4 queue backpressure: {} at retry caller 828BF480",
                  backoff_enabled ? "50 us sleep per 32 polls" : "legacy scheduler yield");
    }
    ++backpressure_polls;
    if ((backpressure_polls & 31U) == 0) {
      if (backoff_enabled) {
        const timespec delay{0, 50000};
        // An interrupted sleep may return early; the guest checks the queue
        // again. No lock is held and no guest work is skipped or completed.
        nanosleep(&delay, nullptr);
      } else {
        sched_yield();
      }
    } else {
      __asm__ volatile("yield");
    }
  } else {
    backpressure_polls = 0;
  }
}

// A second measured CPU sink is the D3D fence retry in sub_82A491E0.
// Execute the full generated timeout/progress logic on EVERY poll. Only add
// backoff when that exact caller is still waiting and the GPU read pointer
// hasn't advanced; never fake completion or sleep on other users of this API.
extern "C" void sub_82A41320(PPCContext& ctx, uint8_t* base) {
  const bool fence_retry = ctx.lr == 0x82A49270;
  const uint32_t wait_context = ctx.r3.u32;
  __imp__sub_82A41320(ctx, base);

  static const bool backoff_enabled = [] {
    const char* setting = std::getenv("THEFT4_FENCE_BACKOFF");
    return !setting || std::strcmp(setting, "0") != 0;
  }();
  struct PollState {
    uint32_t context = 0;
    uint32_t progress = 0;
    uint32_t count = 0;
  };
  static thread_local PollState poll;
  if (!backoff_enabled || !fence_retry || !ctx.r3.u32) {
    poll.count = 0;
    return;
  }
  // The generated function maintains this snapshot at wait_context + 8.
  const uint32_t progress = REX_LOAD_U32(wait_context + 8);
  if (poll.context != wait_context || poll.progress != progress) {
    poll = {wait_context, progress, 0};
  }
  if (++poll.count >= 32) {
    poll.count = 0;
    if (!g_fence_backpressure_reported.exchange(true, std::memory_order_relaxed)) {
      REXLOG_INFO("Theft4 D3D fence backoff: 50 us after 32 unchanged retries at 82A49270");
    }
    const timespec delay{0, 50000};
    nanosleep(&delay, nullptr);
  }
}

extern "C" void sub_82199BC8(PPCContext& ctx, uint8_t* base) {
  const uint32_t destination_address = ctx.r3.u32;
  const uint32_t source_address = ctx.r4.u32;
  const uint32_t initial_gain_address = ctx.r5.u32;
  const uint32_t gain_step_address = ctx.r6.u32;

  // The generated VMX loads align addresses down. Keep unusual alignment or
  // partial aliasing on the authoritative path rather than changing semantics.
  const uint64_t source_end = uint64_t(source_address) + kMixBytes;
  const uint64_t destination_end = uint64_t(destination_address) + kMixBytes;
  if (((destination_address | source_address | initial_gain_address |
        gain_step_address) & 15U) ||
      (destination_address != source_address &&
       destination_address < source_end && source_address < destination_end)) {
    __imp__sub_82199BC8(ctx, base);
    return;
  }

  bool nonzero_step = false;
  if (!g_nonzero_ramp_validated.load(std::memory_order_relaxed)) {
    const auto step = theft4::audio::LoadBigEndianFloat4(
        GuestAddress(base, gain_step_address));
    nonzero_step = vmaxvq_u32(vandq_u32(vreinterpretq_u32_f32(step),
                                      vdupq_n_u32(0x7fffffffU))) != 0;
  }
  uint32_t expected_state = g_mix_ramp_validation.load(std::memory_order_acquire);
  if ((expected_state == 0 || (expected_state == 2 && nonzero_step)) &&
      g_mix_ramp_validation.compare_exchange_strong(
          expected_state, 1, std::memory_order_acq_rel)) {
    alignas(16) std::array<uint8_t, kMixBytes> expected_output;
    alignas(16) std::array<uint8_t, kMixBytes> source_copy;
    alignas(16) std::array<uint8_t, kVectorBytes> initial_gain_copy;
    alignas(16) std::array<uint8_t, kVectorBytes> gain_step_copy;
    std::memcpy(expected_output.data(),
                GuestAddress(base, destination_address), kMixBytes);
    std::memcpy(source_copy.data(), GuestAddress(base, source_address),
                kMixBytes);
    std::memcpy(initial_gain_copy.data(),
                GuestAddress(base, initial_gain_address), kVectorBytes);
    std::memcpy(gain_step_copy.data(), GuestAddress(base, gain_step_address),
                kVectorBytes);

    // Validate both the initial invocation and the first nonzero ramp. Startup
    // silence alone cannot exercise the gain-accumulation arithmetic.
    __imp__sub_82199BC8(ctx, base);
    MixGainRamp(expected_output.data(), source_copy.data(),
                initial_gain_copy.data(), gain_step_copy.data());
    const bool identical =
        std::memcmp(expected_output.data(),
                    GuestAddress(base, destination_address), kMixBytes) == 0;
    g_mix_ramp_validation.store(identical ? 2U : 3U,
                                std::memory_order_release);
    if (identical) {
      if (nonzero_step) g_nonzero_ramp_validated.store(true, std::memory_order_relaxed);
      REXLOG_INFO(
          "Theft4 ARM64 audio gain-ramp hotpath validated byte-for-byte (nonzero_step={})",
          nonzero_step);
    } else {
      REXLOG_ERROR(
          "Theft4 ARM64 audio gain-ramp validation failed; keeping generated path");
    }
    return;
  }

  if (g_mix_ramp_validation.load(std::memory_order_acquire) != 2U) {
    __imp__sub_82199BC8(ctx, base);
    return;
  }

  ctx.fpscr.enableFlushMode();
  MixGainRamp(GuestAddress(base, destination_address),
              GuestAddress(base, source_address),
              GuestAddress(base, initial_gain_address),
              GuestAddress(base, gain_step_address));
}
