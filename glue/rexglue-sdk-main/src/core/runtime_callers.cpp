#include <rex/diagnostics/runtime_callers.h>
#include <rex/chrono/clock.h>
#include <rex/thread.h>
#include <algorithm>
#include <limits>
#if defined(__APPLE__)
#include <pthread.h>
#endif

namespace rex::diagnostics::callers {
std::atomic<bool> enabled{false};
namespace {
constexpr uint32_t kThreads = 128, kBuckets = 64;
struct Bucket {
  std::atomic<uint32_t> kind{0}; // release-publishes immutable site and thread ID
  uint64_t site = 0;
  std::atomic<uint64_t> calls{0}, samples{0}, wall{0}, maximum{0};
  uint64_t exported_calls = 0, exported_samples = 0; // export queue only
};
struct alignas(64) ThreadCounters {
  uint64_t id = 0;
  std::atomic<uint64_t> dropped{0};
  Bucket buckets[kBuckets];
};
ThreadCounters counters[kThreads];
std::atomic<uint32_t> thread_count{0};
// One TLS lookup on Darwin, rather than a lookup for each bookkeeping field.
struct LocalState {
  ThreadCounters* counters = nullptr;
  Bucket* last_bucket = nullptr;
  bool attempted = false;
};
thread_local LocalState local_state;
}
Token Begin(Kind kind, uint64_t site) {
  auto& state = local_state;
  if (!state.attempted) {
    state.attempted = true;
    const uint32_t index = thread_count.fetch_add(1, std::memory_order_relaxed);
    if (index < kThreads) {
      state.counters = &counters[index];
#if defined(__APPLE__)
      pthread_threadid_np(nullptr, &state.counters->id);
#else
      state.counters->id = rex::thread::current_thread_system_id();
#endif
    }
  }
  auto* local = state.counters;
  if (!local) return {};
  Bucket* bucket = state.last_bucket;
  if (!bucket || bucket->kind.load(std::memory_order_relaxed) != kind || bucket->site != site) {
    bucket = nullptr;
    const uint32_t start = uint32_t((site >> 2) ^ (site >> 16) ^ kind) % kBuckets;
    for (uint32_t probe = 0; probe < kBuckets; ++probe) {
      auto& candidate = local->buckets[(start + probe) % kBuckets];
      const uint32_t existing = candidate.kind.load(std::memory_order_relaxed);
      if (!existing) {
        candidate.site = site;
        candidate.kind.store(kind, std::memory_order_release);
        bucket = &candidate;
        break;
      }
      if (existing == kind && candidate.site == site) { bucket = &candidate; break; }
    }
    if (!bucket) {
      local->dropped.store(local->dropped.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
      return {};
    }
    state.last_bucket = bucket;
  }
  const uint64_t calls = bucket->calls.load(std::memory_order_relaxed) + 1;
  bucket->calls.store(calls, std::memory_order_relaxed);
  return {bucket, (calls & 63) == 1 ? rex::chrono::Clock::QueryHostTickCount() : 0};
}
void End(Token token) {
  const uint64_t elapsed = rex::chrono::Clock::QueryHostTickCount() - token.begin;
  auto& bucket = *static_cast<Bucket*>(token.bucket);
  bucket.wall.store(bucket.wall.load(std::memory_order_relaxed) + elapsed, std::memory_order_relaxed);
  bucket.maximum.store(std::max(bucket.maximum.load(std::memory_order_relaxed), elapsed), std::memory_order_relaxed);
  // Publish duration before completed sample count. Rows are approximate
  // concurrent cumulative snapshots, not transactionally consistent traces.
  bucket.samples.store(bucket.samples.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}
}  // namespace rex::diagnostics::callers
using namespace rex::diagnostics::callers;
extern "C" void rex_runtime_callers_enable(int value) { enabled.store(value != 0, std::memory_order_relaxed); }
extern "C" uint64_t rex_runtime_callers_frequency() { return rex::chrono::Clock::QueryHostTickFrequency(); }
extern "C" uint64_t rex_runtime_callers_dropped() {
  const uint32_t threads = thread_count.load(std::memory_order_relaxed);
  uint64_t total = 0;
  // Unregistered threads cannot be counted per hit without a shared write.
  // UINT64_MAX is the explicit 'thread capacity exceeded' sentinel.
  if (threads > kThreads) return std::numeric_limits<uint64_t>::max();
  for (uint32_t i = 0; i < threads; ++i) total += counters[i].dropped.load(std::memory_order_relaxed);
  return total;
}
extern "C" uint32_t rex_runtime_callers_read(uint32_t* cursor, rex_runtime_caller_sample* rows, uint32_t capacity) {
  if (!cursor || !rows) return 0;
  uint32_t output = 0;
  while (*cursor < kThreads * kBuckets && output < capacity) {
    const uint32_t index = (*cursor)++;
    auto& thread = counters[index / kBuckets];
    auto& bucket = thread.buckets[index % kBuckets];
    const uint32_t kind = bucket.kind.load(std::memory_order_acquire);
    if (!kind) continue;
    const uint64_t calls = bucket.calls.load(std::memory_order_relaxed);
    const uint64_t samples = bucket.samples.load(std::memory_order_acquire);
    if (calls == bucket.exported_calls && samples == bucket.exported_samples) continue;
    rows[output++] = {thread.id, kind, bucket.site, calls, samples,
                     bucket.wall.load(std::memory_order_relaxed), bucket.maximum.load(std::memory_order_relaxed)};
    bucket.exported_calls = calls;
    bucket.exported_samples = samples;
  }
  return output;
}
