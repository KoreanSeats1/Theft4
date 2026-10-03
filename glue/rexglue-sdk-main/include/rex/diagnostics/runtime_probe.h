#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <rex/chrono/clock.h>

namespace rex::diagnostics::runtime_probe {
// Bounded counters; no allocation, file I/O, logging, or guest-memory walks.
// Durations overlap (read total includes host read); never sum across stages.
enum class Stage : size_t { ReadLock, ReadValidate, ReadHost, ReadInvalidate,
  ReadTransfer, ReadScatter, HostTask, DeferredDelay, StreamRequest,
  StreamComplete, StreamPump, StreamUnload, QueuedReadWait, Count };
inline constexpr size_t kStages = size_t(Stage::Count);
inline constexpr const char* kNames[] = {"read_lock", "read_validate", "read_host",
  "read_invalidate", "read_transfer", "read_scatter", "host_task", "deferred_delay",
  "stream_request", "stream_complete", "stream_pump", "stream_unload", "queued_read_wait"};
struct Counters { std::atomic<uint64_t> count{0}, ticks{0}, maximum{0}, active{0}; };
inline std::array<Counters, kStages> counters{};
inline std::atomic<bool> enabled{false};
inline std::atomic<uint64_t> read_errors{0}, read_shorts{0}, read_bytes{0};
inline std::atomic<uint64_t> last_error_handle{0}, last_error_offset{0}, last_error_length{0},
  last_error_status{0}, last_error_tick{0}, native_error{0};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
inline uint64_t Tick() { return rex::chrono::Clock::QueryHostTickCount(); }
inline void Add(Stage stage, uint64_t duration) {
  auto& c = counters[size_t(stage)];
  c.ticks.fetch_add(duration, std::memory_order_relaxed);
  c.count.fetch_add(1, std::memory_order_relaxed);
  uint64_t old = c.maximum.load(std::memory_order_relaxed);
  while (old < duration && !c.maximum.compare_exchange_weak(old, duration, std::memory_order_relaxed)) {}
}
class Scope {
 public:
  explicit Scope(Stage stage) : stage_(stage), active_(enabled.load(std::memory_order_relaxed)) {
    if (active_) { begin_=Tick(); counters[size_t(stage_)].active.fetch_add(1,std::memory_order_relaxed); }
  }
  ~Scope() { Finish(); }
  Scope(const Scope&) = delete;
  void Finish() {
    if (!active_) return;
    Add(stage_, Tick()-begin_);
    counters[size_t(stage_)].active.fetch_sub(1,std::memory_order_relaxed);
    active_=false;
  }
 private: Stage stage_; bool active_; uint64_t begin_=0;
};
inline void ReadResult(uint32_t handle, uint64_t offset, uint32_t requested,
                       uint64_t transferred, uint32_t status) {
  if (!enabled.load(std::memory_order_relaxed)) return;
  read_bytes.fetch_add(transferred, std::memory_order_relaxed);
  if (status & 0x80000000u) read_errors.fetch_add(1, std::memory_order_relaxed);
  else if (transferred < requested) read_shorts.fetch_add(1,std::memory_order_relaxed);
  else return;
  // Last event details are advisory during concurrent reads; counters authoritative.
  last_error_handle.store(handle,std::memory_order_relaxed);
  last_error_offset.store(offset,std::memory_order_relaxed);
  last_error_length.store(requested,std::memory_order_relaxed);
  last_error_status.store(status,std::memory_order_relaxed);
  last_error_tick.store(Tick(),std::memory_order_relaxed);
}
} // namespace rex::diagnostics::runtime_probe
