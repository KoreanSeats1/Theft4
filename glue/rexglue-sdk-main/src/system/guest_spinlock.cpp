/**
 * Xenia : Xbox 360 Emulator Research Project
 * Copyright 2022 Ben Vanik. All rights reserved.
 * Released under the BSD license - see LICENSE in the root for more details.
 * Adapted for ReXGlue runtime by Tom Clay, 2026.
 *
 * Shared by KernelState/XThread and xboxkrnl exports. Moved unchanged from
 * xboxkrnl_threading.cpp so the system runtime can initialize its own workers
 * without linking every HLE export and its desktop media dependencies.
 */
#include <rex/kernel/xboxkrnl/threading.h>
#include <rex/system/kernel_state.h>
#include <rex/thread/atomic.h>
#include <rex/thread.h>
#include <rex/assert.h>

namespace rex::kernel::xboxkrnl {
using namespace rex::system;

unsigned char xeKfRaiseIrql(PPCContext* ctx, unsigned char new_irql) {
  auto* mem = rex::system::kernel_state()->memory();
  auto pcr = mem->TranslateVirtual<X_KPCR*>(static_cast<uint32_t>(ctx->r13.u64));
  uint8_t old_irql = pcr->current_irql;
  pcr->current_irql = new_irql;
  return old_irql;
}

void xeKfLowerIrql(PPCContext* ctx, unsigned char new_irql) {
  auto* mem = rex::system::kernel_state()->memory();
  auto pcr = mem->TranslateVirtual<X_KPCR*>(static_cast<uint32_t>(ctx->r13.u64));
  pcr->current_irql = new_irql;
}

uint32_t xeKeKfAcquireSpinLock(PPCContext* ctx, X_KSPINLOCK* lock, bool change_irql) {
  uint32_t old_irql = change_irql ? xeKfRaiseIrql(ctx, IRQL_DISPATCH) : 0;
  uint32_t pcr_addr = static_cast<uint32_t>(ctx->r13.u64);
  const uint32_t self = rex::byte_swap(pcr_addr);
  assert_true(lock->prcb_of_owner.value != self);
  while (!rex::thread::atomic_cas(0u, self, &lock->prcb_of_owner.value)) {
    rex::thread::MaybeYield();
  }
  return old_irql;
}

void xeKeKfReleaseSpinLock(PPCContext* ctx, X_KSPINLOCK* lock, uint32_t old_irql,
                         bool change_irql) {
  assert_true(lock->prcb_of_owner == static_cast<uint32_t>(ctx->r13.u64));
  rex::thread::atomic_store_release(0u, &lock->prcb_of_owner.value);
  if (change_irql && old_irql < IRQL_DISPATCH) {
    xeKfLowerIrql(ctx, static_cast<unsigned char>(old_irql));
  }
}
}  // namespace rex::kernel::xboxkrnl
