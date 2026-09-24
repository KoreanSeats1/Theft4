#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace theft4 {
struct FrameStageSample {
  uint64_t stage = 0, frame = 0, monotonic_ns = 0, a = 0, b = 0;
};
// Multiple producer threads, one export reader. Producers never wait for the
// exporter or another producer: contention is explicitly counted as loss.
// No allocations, formatting, I/O, or GPU queries in Record.
template <size_t Capacity = 8192>
class FrameStageTrace {
  std::array<FrameStageSample, Capacity> samples_{};
  std::mutex mutex_;
  std::atomic<bool> enabled_{false};
  std::atomic<uint64_t> contention_lost_{0};
  uint64_t sequence_ = 0;
 public:
  bool Enabled() const { return enabled_.load(std::memory_order_relaxed); }
  uint64_t Start() {
    std::lock_guard lock(mutex_);
    contention_lost_.store(0);
    enabled_.store(true, std::memory_order_release);
    return sequence_;
  }
  void Stop() { enabled_.store(false, std::memory_order_release); }
  void Record(FrameStageSample sample) {
    if (!Enabled()) return;
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { contention_lost_.fetch_add(1, std::memory_order_relaxed); return; }
    if (!Enabled()) return;
    samples_[++sequence_ % Capacity] = sample;
  }
  size_t CopyAfter(uint64_t& cursor, FrameStageSample* out, size_t capacity, uint64_t& lost) {
    lost = 0;
    if (!out || !capacity) return 0;
    std::lock_guard lock(mutex_);
    lost = contention_lost_.exchange(0);
    if (sequence_ > cursor && sequence_ - cursor > Capacity) {
      lost += sequence_ - cursor - Capacity;
      cursor = sequence_ - Capacity;
    }
    size_t n = 0;
    while (cursor < sequence_ && n < capacity) out[n++] = samples_[++cursor % Capacity];
    return n;
  }
};
}  // namespace theft4
