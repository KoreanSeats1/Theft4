#pragma once
#include <stdint.h>
#ifdef __cplusplus
#include <atomic>
extern "C" {
#endif
// Counters are cumulative within one process. Single serial export consumer.
// Slots never recycle; finite capacity and dropped observations are explicit.
typedef struct rex_runtime_caller_sample {
  uint64_t thread_id, kind, site, calls, samples, wall_ticks, max_wall_ticks;
} rex_runtime_caller_sample;
void rex_runtime_callers_enable(int enabled);
uint32_t rex_runtime_callers_read(uint32_t* cursor, rex_runtime_caller_sample* rows,
                                 uint32_t capacity);
uint64_t rex_runtime_callers_dropped(void);
uint64_t rex_runtime_callers_frequency(void);
#ifdef __cplusplus
}
namespace rex::diagnostics::callers {
extern std::atomic<bool> enabled;
enum Kind : uint32_t {
  ClockRead = 1, ClockContention = 2, HostYield = 3,
  DelayZero = 4, DelayNonzero = 5, WaitSingle = 6, WaitMultiple = 7,
  GuestZeroRetry = 8, GuestDelayWrapper = 9, GuestFencePoll = 10,
  SignalAndWait = 11
};
// No allocations, stack walking, shared counter increments or logging per hit.
// All calls counted; wall duration sampled every64th call per thread/site.
struct Token { void* bucket = nullptr; uint64_t begin = 0; };
Token Begin(Kind kind, uint64_t site);
void End(Token token);
inline bool Enabled() { return enabled.load(std::memory_order_relaxed); }
class Span {
 public:
  Span(Kind kind, uint64_t site) : token_(Enabled() ? Begin(kind, site) : Token{}) {}
  ~Span() { if (token_.begin) End(token_); }
  Span(const Span&) = delete;
  Span& operator=(const Span&) = delete;
 private:
  Token token_;
};
}  // namespace rex::diagnostics::callers
#if defined(__GNUC__) || defined(__clang__)
#define REX_CALLER_NATIVE_PC uint64_t(reinterpret_cast<uintptr_t>(__builtin_extract_return_addr(__builtin_return_address(0))))
#else
#define REX_CALLER_NATIVE_PC uint64_t(0)
#endif
#endif
