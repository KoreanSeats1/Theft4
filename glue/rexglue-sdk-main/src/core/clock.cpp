/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/diagnostics/runtime_callers.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/math.h>

REXCVAR_DEFINE_BOOL(clock_no_scaling, false, "Clock",
                    "Disable clock scaling (inverted: false = scaling enabled)");

REXCVAR_DEFINE_BOOL(clock_source_raw, false, "Clock", "Use raw clock source without scaling");
REXCVAR_DEFINE_BOOL(clock_direct_reads, false, "Clock",
                    "Read guest time from a monotonic epoch without a shared reader mutex");

namespace rex::chrono {

// Time scalar applied to all time operations.
std::atomic<double> guest_time_scalar_{1.0};
// Tick frequency of guest.
std::atomic<uint64_t> guest_tick_frequency_{Clock::host_tick_frequency_platform()};
// Base FILETIME of the guest system from app start.
std::atomic<uint64_t> guest_system_time_base_{Clock::QueryHostSystemTime()};
// Combined time and frequency ratio between host and guest.
// Split in numerator (first) and denominator (second).
// Computed by RecomputeGuestTickScalar.
std::pair<uint64_t, uint64_t> guest_tick_ratio_ = std::make_pair(1, 1);

// Native guest ticks.
uint64_t last_guest_tick_count_ = 0;
// Last sampled host tick count.
uint64_t last_host_tick_count_ = Clock::QueryHostTickCount();
// Mutex to ensure last_host_tick_count_ and last_guest_tick_count_ are in sync
std::mutex tick_mutex_;

namespace {
// Readers never modify a common cache line. All epoch fields are atomic and
// sequentially consistent: validating an unchanged even version proves a
// coherent snapshot without C++ data races or architecture-specific fences.
// Writers hold tick_mutex_. Two unsuccessful reads fall back to that mutex,
// so a preempted reconfiguration never leaves readers in an unbounded spin.
struct alignas(64) GuestClockEpoch {
  std::atomic<uint64_t> version{0};
  std::atomic<uint64_t> host{0}, guest{0}, numerator{1}, denominator{1};
};
GuestClockEpoch direct_epoch;
std::atomic<int> direct_policy{-1};
std::atomic<bool> direct_active{false};

uint64_t ScaleEpochDelta(uint64_t host, uint64_t origin, uint64_t guest,
                         uint64_t numerator, uint64_t denominator) {
  const uint64_t delta = host >= origin ? host - origin : 0;
#if defined(__SIZEOF_INT128__)
  // Round once from the fixed origin, preserving fractional elapsed ticks.
  // The wide intermediate avoids multiplying a long uptime in uint64_t.
  uint64_t product;
  if (!__builtin_mul_overflow(delta, numerator, &product))
    return guest + product / denominator;
  return guest + uint64_t((__uint128_t(delta) * numerator) / denominator);
#else
  // Direct mode is enabled only on targets with the wide integer support.
  return guest + (delta / denominator) * numerator +
      ((delta % denominator) * numerator) / denominator;
#endif
}

uint64_t ReadDirectLocked(uint64_t now) {
  return ScaleEpochDelta(now, direct_epoch.host.load(), direct_epoch.guest.load(),
                        direct_epoch.numerator.load(), direct_epoch.denominator.load());
}

void PublishDirectEpochLocked(uint64_t host, uint64_t guest) {
  direct_epoch.version.fetch_add(1);
  direct_epoch.host.store(host);
  direct_epoch.guest.store(guest);
  direct_epoch.numerator.store(guest_tick_ratio_.first);
  direct_epoch.denominator.store(guest_tick_ratio_.second);
  direct_epoch.version.fetch_add(1);
}

uint64_t ReadDirect() {
  for (unsigned attempt = 0; attempt != 2; ++attempt) {
    const uint64_t version = direct_epoch.version.load();
    if (version & 1) break;
    const uint64_t host = direct_epoch.host.load();
    const uint64_t guest = direct_epoch.guest.load();
    const uint64_t numerator = direct_epoch.numerator.load();
    const uint64_t denominator = direct_epoch.denominator.load();
    const uint64_t now = Clock::QueryHostTickCount();
    if (direct_epoch.version.load() == version)
      return ScaleEpochDelta(now, host, guest, numerator, denominator);
  }
  std::lock_guard<std::mutex> lock(tick_mutex_);
  return ReadDirectLocked(Clock::QueryHostTickCount());
}
}  // namespace

bool Clock::ConfigureDirectReads(bool enabled) {
#if !defined(__SIZEOF_INT128__)
  if (enabled) return false;
#endif
  int unset = -1;
  if (!direct_policy.compare_exchange_strong(unset, enabled ? 1 : 0))
    return unset == (enabled ? 1 : 0);
  if (!enabled) return true;
  // Keep the existing time origin and accumulated guest ticks at handoff.
  // Called before workers, never as a live switch during gameplay.
  std::lock_guard<std::mutex> lock(tick_mutex_);
  const uint64_t now = std::max(last_host_tick_count_, QueryHostTickCount());
  last_guest_tick_count_ = ScaleEpochDelta(now, last_host_tick_count_,
      last_guest_tick_count_, guest_tick_ratio_.first, guest_tick_ratio_.second);
  last_host_tick_count_ = now;
  PublishDirectEpochLocked(now, last_guest_tick_count_);
  direct_active.store(true, std::memory_order_release);
  return true;
}

bool Clock::DirectReadsEnabled() {
  return direct_active.load(std::memory_order_acquire);
}

std::atomic<uint64_t> stale_samples_{0}, prevented_host_ticks_{0},
    largest_stale_ticks_{0}, last_stale_host_tick_{0};

ClockDiagnostics Clock::QueryDiagnostics() {
  return {stale_samples_.load(std::memory_order_relaxed),
          prevented_host_ticks_.load(std::memory_order_relaxed),
          largest_stale_ticks_.load(std::memory_order_relaxed),
          last_stale_host_tick_.load(std::memory_order_relaxed)};
}

void RecomputeGuestTickScalar() {
  // Create a rational number with numerator (first) and denominator (second)
  auto frac = std::make_pair(guest_tick_frequency_.load(), Clock::QueryHostTickFrequency());
  // Doing it this way ensures we don't mess up our frequency scaling and
  // precisely controls the precision the guest_time_scalar_ can have.
  const double scalar = guest_time_scalar_.load();
  if (scalar > 1.0) {
    frac.first *= static_cast<uint64_t>(scalar * 10.0);
    frac.second *= 10;
  } else {
    frac.first *= 10;
    frac.second *= static_cast<uint64_t>(10.0 / scalar);
  }
  // Keep this a rational calculation and reduce the fraction
  reduce_fraction(frac);

  std::lock_guard<std::mutex> lock(tick_mutex_);
  const bool direct = Clock::DirectReadsEnabled();
  if (direct) {
    const uint64_t now = Clock::QueryHostTickCount();
    last_guest_tick_count_ = ReadDirectLocked(now);
    last_host_tick_count_ = now;
  }
  guest_tick_ratio_ = frac;
  if (direct) PublishDirectEpochLocked(last_host_tick_count_, last_guest_tick_count_);
}

// Update the guest timer for all threads.
// Return a copy of the value so locking is reduced.
uint64_t UpdateGuestClock() {
  if (Clock::DirectReadsEnabled() && !REXCVAR_GET(clock_no_scaling)) return ReadDirect();
  uint64_t host_tick_count = Clock::QueryHostTickCount();

  if (REXCVAR_GET(clock_no_scaling)) {
    // Nothing to update, calculate on the fly
    return host_tick_count * guest_tick_ratio_.first / guest_tick_ratio_.second;
  }

  std::unique_lock<std::mutex> lock(tick_mutex_, std::defer_lock);
  if (lock.try_lock()) {
    // Translate host tick count to guest tick count.
    uint64_t host_tick_delta =
        host_tick_count > last_host_tick_count_ ? host_tick_count - last_host_tick_count_ : 0;
    // A thread may sample the host before another thread, then acquire this
    // mutex later. Never rewind the baseline and count that interval twice.
    if (host_tick_count < last_host_tick_count_) {
      const uint64_t stale = last_host_tick_count_ - host_tick_count;
      stale_samples_.fetch_add(1, std::memory_order_relaxed);
      prevented_host_ticks_.fetch_add(stale, std::memory_order_relaxed);
      largest_stale_ticks_.store(std::max(largest_stale_ticks_.load(std::memory_order_relaxed), stale),
                                std::memory_order_relaxed);
      last_stale_host_tick_.store(last_host_tick_count_, std::memory_order_relaxed);
    } else {
      last_host_tick_count_ = host_tick_count;
    }
    uint64_t guest_tick_delta =
        host_tick_delta * guest_tick_ratio_.first / guest_tick_ratio_.second;
    last_guest_tick_count_ += guest_tick_delta;
    return last_guest_tick_count_;
  } else {
    rex::diagnostics::callers::Span contention(rex::diagnostics::callers::ClockContention, 0);
    // Wait until another thread has finished updating the clock.
    lock.lock();
    return last_guest_tick_count_;
  }
}

// Offset of the current guest system file time relative to the guest base time.
inline uint64_t QueryGuestSystemTimeOffset() {
  if (REXCVAR_GET(clock_no_scaling)) {
    return Clock::QueryHostSystemTime() - guest_system_time_base_;
  }

  auto guest_tick_count = UpdateGuestClock();

  uint64_t numerator = 10000000;  // 100ns/10MHz resolution
  uint64_t denominator = guest_tick_frequency_;
  reduce_fraction(numerator, denominator);

  return guest_tick_count * numerator / denominator;
}

uint64_t Clock::QueryHostTickFrequency() {
#if REX_CLOCK_RAW_AVAILABLE
  if (REXCVAR_GET(clock_source_raw)) {
    return host_tick_frequency_raw();
  }
#endif
  return host_tick_frequency_platform();
}
uint64_t Clock::QueryHostTickCount() {
#if REX_CLOCK_RAW_AVAILABLE
  if (REXCVAR_GET(clock_source_raw)) {
    return host_tick_count_raw();
  }
#endif
  return host_tick_count_platform();
}

double Clock::guest_time_scalar() {
  return guest_time_scalar_;
}

void Clock::set_guest_time_scalar(double scalar) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return;
  }

  guest_time_scalar_ = scalar;
  RecomputeGuestTickScalar();
}

std::pair<uint64_t, uint64_t> Clock::guest_tick_ratio() {
  std::lock_guard<std::mutex> lock(tick_mutex_);
  return guest_tick_ratio_;
}

uint64_t Clock::guest_tick_frequency() {
  return guest_tick_frequency_;
}

void Clock::set_guest_tick_frequency(uint64_t frequency) {
  guest_tick_frequency_ = frequency;
  RecomputeGuestTickScalar();
}

uint64_t Clock::guest_system_time_base() {
  return guest_system_time_base_;
}

void Clock::set_guest_system_time_base(uint64_t time_base) {
  guest_system_time_base_ = time_base;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
uint64_t Clock::QueryGuestTickCount() {
  rex::diagnostics::callers::Span sample(rex::diagnostics::callers::ClockRead, REX_CALLER_NATIVE_PC);
  auto guest_tick_count = UpdateGuestClock();
  return guest_tick_count;
}

uint64_t Clock::QueryGuestSystemTime() {
  if (REXCVAR_GET(clock_no_scaling)) {
    return Clock::QueryHostSystemTime();
  }

  auto guest_system_time_offset = QueryGuestSystemTimeOffset();
  return guest_system_time_base_ + guest_system_time_offset;
}

uint32_t Clock::QueryGuestUptimeMillis() {
  return static_cast<uint32_t>(std::min<uint64_t>(QueryGuestSystemTimeOffset() / 10000,
                                                  std::numeric_limits<uint32_t>::max()));
}

void Clock::SetGuestSystemTime(uint64_t system_time) {
  if (REXCVAR_GET(clock_no_scaling)) {
    // Time is fixed to host time.
    return;
  }

  // Query the filetime offset to calculate a new base time.
  auto guest_system_time_offset = QueryGuestSystemTimeOffset();
  guest_system_time_base_ = system_time - guest_system_time_offset;
}

uint32_t Clock::ScaleGuestDurationMillis(uint32_t guest_ms) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return guest_ms;
  }

  constexpr uint64_t max = std::numeric_limits<uint32_t>::max();

  if (guest_ms >= max) {
    return max;
  } else if (!guest_ms) {
    return 0;
  }
  uint64_t scaled_ms =
      static_cast<uint64_t>((static_cast<uint64_t>(guest_ms) * guest_time_scalar_));
  return static_cast<uint32_t>(std::min(scaled_ms, max));
}

int64_t Clock::ScaleGuestDurationFileTime(int64_t guest_file_time) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return static_cast<uint64_t>(guest_file_time);
  }

  if (!guest_file_time) {
    return 0;
  } else if (guest_file_time > 0) {
    // Absolute time.
    uint64_t guest_time = Clock::QueryGuestSystemTime();
    int64_t relative_time = guest_file_time - static_cast<int64_t>(guest_time);
    int64_t scaled_time = static_cast<int64_t>(relative_time * guest_time_scalar_);
    return static_cast<int64_t>(guest_time) + scaled_time;
  } else {
    // Relative time.
    uint64_t scaled_file_time =
        static_cast<uint64_t>((static_cast<uint64_t>(guest_file_time) * guest_time_scalar_));
    // TODO(benvanik): check for overflow?
    return scaled_file_time;
  }
}

void Clock::ScaleGuestDurationTimeval(int32_t* tv_sec, int32_t* tv_usec) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return;
  }

  uint64_t scaled_sec = static_cast<uint64_t>(static_cast<uint64_t>(*tv_sec) * guest_time_scalar_);
  uint64_t scaled_usec =
      static_cast<uint64_t>(static_cast<uint64_t>(*tv_usec) * guest_time_scalar_);
  if (scaled_usec > std::numeric_limits<uint32_t>::max()) {
    uint64_t overflow_sec = scaled_usec / 1000000;
    scaled_usec -= overflow_sec * 1000000;
    scaled_sec += overflow_sec;
  }
  *tv_sec = int32_t(scaled_sec);
  *tv_usec = int32_t(scaled_usec);
}

}  // namespace rex::chrono
