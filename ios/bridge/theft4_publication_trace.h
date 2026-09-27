#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace theft4 {

struct PublicationSample {
  uint64_t frame = 0;
  uint64_t monotonic_ns = 0;
};

// Single presenter writer, one background drain reader. The ring is only a
// buffer between timer-driven checkpoints, not the lifetime of the capture.
// No allocation, file I/O, or GPU query occurs in Record().
template <size_t Capacity = 16384>
class PublicationTrace {
  static_assert(Capacity > 1);
  static_assert(std::atomic<uint64_t>::is_always_lock_free);
  struct Slot {
    std::atomic<uint64_t> sequence{0};
    std::atomic<uint64_t> frame{0};
    std::atomic<uint64_t> monotonic_ns{0};
  };
  std::array<Slot, Capacity> slots_{};
  std::atomic<bool> enabled_{false};
  std::atomic<uint64_t> published_{0};
  uint64_t writer_sequence_ = 0;

 public:
  // May be called while the presenter writer is active. Sequence numbers never
  // reset; the returned cursor excludes all previous capture sessions.
  uint64_t Start() {
    const uint64_t cursor = published_.load(std::memory_order_acquire);
    enabled_.store(true, std::memory_order_release);
    return cursor;
  }
  void Stop() { enabled_.store(false, std::memory_order_release); }
  bool Enabled() const { return enabled_.load(std::memory_order_relaxed); }

  void Record(uint64_t frame, uint64_t monotonic_ns) {
    if (!Enabled()) return;
    const uint64_t sequence = ++writer_sequence_;
    Slot& slot = slots_[sequence % Capacity];
    slot.sequence.store(sequence * 2 + 1, std::memory_order_seq_cst);
    slot.frame.store(frame, std::memory_order_seq_cst);
    slot.monotonic_ns.store(monotonic_ns, std::memory_order_seq_cst);
    slot.sequence.store(sequence * 2, std::memory_order_seq_cst);
    published_.store(sequence, std::memory_order_release);
  }

  size_t CopyAfter(uint64_t& cursor, PublicationSample* out, size_t max_count,
                   uint64_t& lost) const {
    lost = 0;
    if (!out || !max_count) return 0;
    const uint64_t end = published_.load(std::memory_order_acquire);
    const uint64_t first = end >= Capacity ? end - Capacity + 1 : 1;
    if (cursor + 1 < first) {
      lost += first - cursor - 1;
      cursor = first - 1;
    }
    size_t count = 0;
    const uint64_t limit = std::min<uint64_t>(end, cursor + max_count);
    for (uint64_t sequence = cursor + 1; sequence <= limit; ++sequence) {
      const Slot& slot = slots_[sequence % Capacity];
      const uint64_t before = slot.sequence.load(std::memory_order_seq_cst);
      const uint64_t frame = slot.frame.load(std::memory_order_seq_cst);
      const uint64_t timestamp = slot.monotonic_ns.load(std::memory_order_seq_cst);
      const uint64_t after = slot.sequence.load(std::memory_order_seq_cst);
      if (before == sequence * 2 && after == before)
        out[count++] = {frame, timestamp};
      else ++lost;  // A concurrent overwrite is reported, never synthesized.
      cursor = sequence;
    }
    return count;
  }
};

}  // namespace theft4
