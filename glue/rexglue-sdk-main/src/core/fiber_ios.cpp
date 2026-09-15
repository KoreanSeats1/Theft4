/**
 * iOS experimental Fiber backend. Uses the public setjmp/longjmp interface
 * to resume suspended calls and an ARM64 stack pivot for first entry.
 * Compiling this backend does not establish device context-switch safety.
 * M3 must validate registers, stack lifetime, TLS and repeated switches.
 * BSD 3-Clause License; see the SDK LICENSE.
 */

#include <rex/platform.h>

#if REX_PLATFORM_IOS

#include <rex/thread/fiber.h>

#include <cassert>
#include <csetjmp>
#include <cstdlib>

#if !defined(__aarch64__) || defined(__arm64e__)
#error The experimental iOS fiber backend supports arm64, not arm64e or x86.
#endif

namespace rex::thread {

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
  auto* f = new Fiber();
  f->is_thread_fiber_ = true;
  f->started_ = true;  // already running on this OS thread
  tls_current_ = f;
  return f;
}

Fiber* Fiber::Create(size_t stack_size, void (*entry)(void*), void* arg) {
  // Minimum 64 KiB, aligned to 16 bytes for the AArch64 ABI.
  if (stack_size < 65536) stack_size = 65536;

  void* stack = nullptr;
  if (posix_memalign(&stack, 16, stack_size) != 0) stack = nullptr;
  if (!stack) return nullptr;

  auto* f = new Fiber();
  f->stack_ = stack;
  f->stack_size_ = stack_size;
  f->entry_ = entry;
  f->arg_ = arg;
  f->started_ = false;
  return f;
}

// Trampoline runs on the fiber's own stack. Must not return.
__attribute__((noinline, noreturn))
/*static*/ void Fiber::Trampoline() {
  Fiber* f = tls_current_;
  f->entry_(f->arg_);
  // Entry is a coroutine body — if it ever returns without SwitchTo'ing
  // somewhere else, we have no valid context to resume. Trap loudly.
  __builtin_trap();
}

void Fiber::SwitchTo(Fiber* target) {
  Fiber* from = tls_current_;
  assert(from && "Fiber::SwitchTo called without ConvertCurrentThread");
  assert(target && "Fiber::SwitchTo target is null");

  tls_current_ = target;

  // setjmp returns 0 on the direct call, non-zero when longjmp'd back.
  if (setjmp(from->context_) == 0) {
    if (target->started_) {
      longjmp(target->context_, 1);
    } else {
      // First switch — pivot SP to the fiber's private stack and tail-call
      // the trampoline. AArch64 stack grows downward, SP must be 16-byte
      // aligned, and we leave 16 bytes of red-zone headroom.
      target->started_ = true;
      uintptr_t sp =
          (reinterpret_cast<uintptr_t>(target->stack_) + target->stack_size_) & ~uintptr_t(15);
      void (*trampoline)() = &Fiber::Trampoline;
      __asm__ volatile(
          "mov sp, %[newsp]\n\t"  // pivot to the fiber's stack
          "br  %[func]\n\t"       // tail-call trampoline (never returns)
          :
          : [newsp] "r"(sp), [func] "r"(trampoline)
          : "memory");

      __builtin_unreachable();
    }
  }
  // Non-zero return — we were longjmp'd back in. Resume.
}

void Fiber::Destroy() {
  if (is_thread_fiber_) {
    tls_current_ = nullptr;
  } else {
    assert(this != tls_current_ && "Fiber::Destroy called on the running fiber");
    std::free(stack_);
  }
  delete this;
}

}  // namespace rex::thread

#endif  // REX_PLATFORM_IOS
