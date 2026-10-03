/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <atomic>
#include <thread>
#include <rex/thread/runtime_wait_policy.h>

#include <rex/thread.h>

namespace rex::thread {
namespace {
std::atomic<int> runtime_wait_policy{-1};
}

bool ConfigureRuntimeWaitFixes(bool enabled) {
  int expected = -1;
  const int requested = enabled ? 1 : 0;
  return runtime_wait_policy.compare_exchange_strong(expected, requested) ||
         expected == requested;
}

bool RuntimeWaitFixesEnabled() {
  return runtime_wait_policy.load(std::memory_order_relaxed) == 1;
}


// =============================================================================
// Common code
// =============================================================================

uint32_t logical_processor_count() {
  static uint32_t value = 0;
  if (!value) {
    value = std::thread::hardware_concurrency();
  }
  return value;
}

thread_local uint32_t current_thread_id_ = UINT_MAX;

uint32_t current_thread_id() {
  return current_thread_id_ == UINT_MAX ? current_thread_system_id() : current_thread_id_;
}

void set_current_thread_id(uint32_t id) {
  current_thread_id_ = id;
}

}  // namespace rex::thread
