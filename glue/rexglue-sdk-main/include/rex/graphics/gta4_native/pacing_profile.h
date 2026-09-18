#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace rex::graphics::gta4_native::pacing {

// One observation per guest present, not per draw. Host ticks correlate with
// the native profiler; steady-clock nanoseconds belong to limiter deadlines.
// Never subtract values from those two clock domains.
struct Sample {
  uint32_t frame = 0, system_thread = 0, guest_thread = 0;
  uint32_t requested_fps = 0, applied_fps = 0;
  bool limiter_before_submit = false;
  bool submitted = false, wait_requested = false, late_reset = false, mode_changed = false;
  uint64_t hook_begin = 0, submit_begin = 0, submit_end = 0;
  uint64_t limiter_begin = 0, mutex_begin = 0, mutex_acquired = 0;
  uint64_t sleep_begin = 0, wake = 0, limiter_end = 0;
  int64_t decision_ns = 0, prior_deadline_ns = 0, wait_until_ns = 0;
  int64_t next_deadline_ns = 0, sleep_begin_ns = 0, wake_ns = 0;
};

struct Snapshot {
  uint64_t begin_tick = 0, end_tick = 0, dropped = 0;
  bool started = false, stopped = false;
  std::vector<Sample> samples;
};

template <size_t Capacity = 1024>
class Recorder {
 public:
  bool Active() const noexcept { return active_.load(std::memory_order_acquire); }

  bool Start(uint64_t tick) {
    std::lock_guard lock(mutex_);
    if (started_) return false;  // One immutable capture per process.
    started_ = true;
    begin_tick_ = tick;
    active_.store(true, std::memory_order_release);
    return true;
  }

  bool Record(const Sample& sample) {
    if (!Active()) return false;
    // Only fixed-size copying while locked. No allocation, clock calls, I/O
    // or formatting. This also serializes multiple present producer threads.
    std::lock_guard lock(mutex_);
    if (!Active()) return false;
    if (count_ == Capacity) {
      ++dropped_;
      return false;
    }
    samples_[count_++] = sample;
    return true;
  }

  void Stop(uint64_t tick) {
    std::lock_guard lock(mutex_);
    if (!Active()) return;
    end_tick_ = tick;
    active_.store(false, std::memory_order_release);
  }

  // Called by the export worker, after Stop. The copy owns its storage and
  // cannot race a writer or read an overwritten capture slot.
  Snapshot Read() const {
    std::lock_guard lock(mutex_);
    Snapshot result{begin_tick_, end_tick_, dropped_, started_, started_ && !Active(), {}};
    result.samples.assign(samples_.begin(), samples_.begin() + count_);
    return result;
  }

 private:
  mutable std::mutex mutex_;
  std::atomic<bool> active_{false};
  bool started_ = false;
  uint64_t begin_tick_ = 0, end_tick_ = 0, dropped_ = 0;
  size_t count_ = 0;
  std::array<Sample, Capacity> samples_{};
};

#ifdef THEFT4_LAB_BUILD
// Shared by the title hooks and native renderer within the Lab executable.
inline Recorder<> capture;
#endif

}  // namespace rex::graphics::gta4_native::pacing
