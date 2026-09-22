#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace theft4 {
// One presenter writer, one UI reader/controller. Atomic slots avoid a lock on
// the renderer. A generation change discards intervals across hidden/background
// periods. Sequential consistency makes the per-slot sequence check coherent.
template <size_t Capacity = 256>
class FrameTimeHistory {
  static_assert(Capacity > 1);
  static_assert(std::atomic<uint64_t>::is_always_lock_free);
  struct Slot {
    std::atomic<uint64_t> sequence{0}, generation{0}, timestamp{0};
  };
  std::array<Slot, Capacity> slots_{};
  std::atomic<uint64_t> control_{0}, published_{0};
  uint64_t writer_sequence_ = 0;
 public:
  bool Enabled() const { return (control_.load() & 1) != 0; }
  // UI thread only. Repeated calls with the same state retain history.
  void SetEnabled(bool enabled) {
    const auto current = control_.load();
    if (bool(current & 1) != enabled) control_.store(current + 1);
  }
  void Record(uint64_t timestamp_ns) {
    const auto generation = control_.load();
    if (!(generation & 1)) return;
    const auto sequence = ++writer_sequence_;
    auto& slot = slots_[sequence % Capacity];
    slot.sequence.store(sequence * 2 + 1);
    slot.generation.store(generation);
    slot.timestamp.store(timestamp_ns);
    slot.sequence.store(sequence * 2);
    published_.store(sequence);
  }
  size_t Copy(uint64_t now_ns, double* milliseconds, size_t capacity,
              double& pending_ms) const {
    pending_ms = 0;
    const auto generation = control_.load();
    if (!(generation & 1) || !milliseconds || !capacity) return 0;
    const auto end = published_.load();
    const auto length = std::min<uint64_t>(end, std::min(capacity, Capacity - 1) + 1);
    uint64_t previous = 0;
    bool have_previous = false;
    size_t count = 0;
    for (auto sequence = end - length + 1; length && sequence <= end; ++sequence) {
      const auto& slot = slots_[sequence % Capacity];
      const auto before = slot.sequence.load();
      const auto epoch = slot.generation.load();
      const auto timestamp = slot.timestamp.load();
      const auto after = slot.sequence.load();
      // The writer lapped this read. Skip this refresh rather than invent a spike.
      if (before != sequence * 2 || after != before) return 0;
      if (epoch != generation) { have_previous = false; count = 0; continue; }
      if (have_previous && timestamp >= previous)
        milliseconds[count++] = double(timestamp - previous) / 1e6;
      else if (have_previous) count = 0;
      previous = timestamp;
      have_previous = true;
    }
    if (control_.load() != generation) return 0;
    if (have_previous && now_ns > previous) pending_ms = double(now_ns - previous) / 1e6;
    return count;
  }
};
}  // namespace theft4
