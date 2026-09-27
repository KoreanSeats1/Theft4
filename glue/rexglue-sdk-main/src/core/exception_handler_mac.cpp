/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay & Rien Gupta, 2026 - Adapted for ReXGlue runtime MacOS
 */

#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
// Darwin hides the deprecated ucontext API unless an XSI feature level is
// requested before <ucontext.h> is included.
#define _XOPEN_SOURCE 700
#endif

#include <rex/exception_handler.h>

#if REX_PLATFORM_LINUX || REX_PLATFORM_DARWIN

#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>

#include <rex/assert.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <rex/platform.h>

#include <ucontext.h>

#if REX_PLATFORM_DARWIN
#include <rex/fault_diagnostics.h>

#include <atomic>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <unistd.h>

namespace {
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<uintptr_t>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(sizeof(RexGuestFaultContext) == 32);
static_assert(sizeof(RexUnhandledFaultRecord) == 400);

static_assert(std::atomic<RexFaultSnapshotWriter>::is_always_lock_free);
std::atomic<RexFaultSnapshotWriter> fault_snapshot_writer{nullptr};
std::atomic<int> fault_record_fd{-1};
std::atomic<uint32_t> fault_timebase_numer{0}, fault_timebase_denom{0};
std::atomic<uint64_t> fault_image_slide{0};
std::atomic<uint64_t> handled_av_count{0}, unhandled_av_count{0};
std::atomic<uint32_t> unhandled_record_claimed{0};

struct FaultSite {
  std::atomic<uint64_t> sequence{0}, pc{0}, address{0}, ticks{0};
};
FaultSite first_av, latest_av;

// Writers never wait: contention may omit a metadata update, but not a count.
// Sequence protection prevents a reader from combining two different faults.
void StoreFaultSite(FaultSite& site, uint64_t pc, uint64_t address,
                    uint64_t ticks, bool first_only) {
  uint64_t sequence = site.sequence.load(std::memory_order_relaxed);
  if ((sequence & 1) || (first_only && sequence != 0) ||
      !site.sequence.compare_exchange_strong(sequence, sequence + 1,
                                            std::memory_order_acquire)) {
    return;
  }
  site.pc.store(pc, std::memory_order_relaxed);
  site.address.store(address, std::memory_order_relaxed);
  site.ticks.store(ticks, std::memory_order_relaxed);
  site.sequence.store(sequence + 2, std::memory_order_release);
}

bool ReadFaultSite(const FaultSite& site, uint64_t& pc, uint64_t& address,
                   uint64_t& ticks) {
  const uint64_t before = site.sequence.load(std::memory_order_acquire);
  if (before == 0 || (before & 1)) return false;
  const uint64_t candidate_pc = site.pc.load(std::memory_order_relaxed);
  const uint64_t candidate_address = site.address.load(std::memory_order_relaxed);
  const uint64_t candidate_ticks = site.ticks.load(std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_acquire);
  if (before != site.sequence.load(std::memory_order_relaxed)) return false;
  pc = candidate_pc;
  address = candidate_address;
  ticks = candidate_ticks;
  return true;
}

struct GuestFaultSlot {
  std::atomic<uintptr_t> owner{0};
  std::atomic<uint64_t> sequence{0};
  std::atomic<uint32_t> valid{0};
  std::atomic<uint32_t> context_tag{0}, r3{0}, r29{0}, r30{0}, r31{0}, lr{0};
  std::atomic<uint64_t> memory_base{0};
};
GuestFaultSlot guest_fault_slots[128];
// This TLS variable is accessed only from normal execution, never from signals.
thread_local GuestFaultSlot* current_guest_fault_slot = nullptr;

GuestFaultSlot* FindGuestFaultSlot(bool allocate) {
  const uintptr_t owner = reinterpret_cast<uintptr_t>(pthread_self());
  for (auto& slot : guest_fault_slots) {
    uintptr_t observed = slot.owner.load(std::memory_order_acquire);
    if (observed == owner) return &slot;
    if (allocate && observed == 0 &&
        slot.owner.compare_exchange_strong(observed, owner,
                                           std::memory_order_acq_rel)) {
      return &slot;
    }
  }
  return nullptr;
}

bool ReadGuestFaultSlot(const GuestFaultSlot* slot, RexGuestFaultContext* out) {
  if (!slot) return false;
  const uint64_t before = slot->sequence.load(std::memory_order_acquire);
  if ((before & 1) || !slot->valid.load(std::memory_order_relaxed)) return false;
  RexGuestFaultContext candidate;
  candidate.context_tag = slot->context_tag.load(std::memory_order_relaxed);
  candidate.r3 = slot->r3.load(std::memory_order_relaxed);
  candidate.r29 = slot->r29.load(std::memory_order_relaxed);
  candidate.r30 = slot->r30.load(std::memory_order_relaxed);
  candidate.r31 = slot->r31.load(std::memory_order_relaxed);
  candidate.lr = slot->lr.load(std::memory_order_relaxed);
  candidate.memory_base = slot->memory_base.load(std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_acquire);
  if (before != slot->sequence.load(std::memory_order_relaxed)) return false;
  *out = candidate;
  return true;
}

void RecordFirstUnhandledFault(int signal_number, const siginfo_t* info,
                               mcontext_t mcontext, uint32_t access_kind) {
  uint32_t expected = 0;
  if (!unhandled_record_claimed.compare_exchange_strong(
          expected, 1, std::memory_order_relaxed)) return;
  const int fd = fault_record_fd.load(std::memory_order_acquire);
  if (fd < 0) return;

  RexUnhandledFaultRecord record{};
  record.magic = UINT64_C(0x313030544c465852);  // RXFLT001
  record.version = 1;
  record.record_size = sizeof(record);
  record.signal_number = signal_number;
  record.signal_code = info ? info->si_code : 0;
  record.access_kind = access_kind;
  record.host_ticks = mach_absolute_time();
  record.fault_address = info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
  record.timebase_numer = fault_timebase_numer.load(std::memory_order_relaxed);
  record.timebase_denom = fault_timebase_denom.load(std::memory_order_relaxed);
  record.image_slide = fault_image_slide.load(std::memory_order_relaxed);
#if REX_ARCH_ARM64
  record.host_arch = 1;
  for (size_t i = 0; i < 29; ++i) record.x[i] = mcontext->__ss.__x[i];
#if __DARWIN_OPAQUE_ARM_THREAD_STATE64
  record.x[29] = reinterpret_cast<uintptr_t>(mcontext->__ss.__opaque_fp);
  record.x[30] = reinterpret_cast<uintptr_t>(mcontext->__ss.__opaque_lr);
  record.sp = reinterpret_cast<uintptr_t>(mcontext->__ss.__opaque_sp);
  record.pc = reinterpret_cast<uintptr_t>(mcontext->__ss.__opaque_pc);
#else
  record.x[29] = mcontext->__ss.__fp;
  record.x[30] = mcontext->__ss.__lr;
  record.sp = mcontext->__ss.__sp;
  record.pc = mcontext->__ss.__pc;
#endif
  record.esr = mcontext->__es.__esr;
  record.far = mcontext->__es.__far;
  record.pstate = mcontext->__ss.__cpsr;
  record.fpsr = mcontext->__ns.__fpsr;
  record.fpcr = mcontext->__ns.__fpcr;
#elif REX_ARCH_AMD64
  record.host_arch = 2;
  record.pc = mcontext->__ss.__rip;
  record.sp = mcontext->__ss.__rsp;
  record.pstate = mcontext->__ss.__rflags;
  record.esr = mcontext->__es.__err;
  record.far = record.fault_address;
#endif
  record.guest_context_valid = ReadGuestFaultSlot(FindGuestFaultSlot(false), &record.guest);

  // write(2) is async-signal-safe. Bound retries, including EINTR; never fsync,
  // format text, allocate, lock, or inspect pointed-to guest memory here.
  const char* bytes = reinterpret_cast<const char*>(&record);
  size_t remaining = sizeof(record);
  for (unsigned attempt = 0; attempt < 4 && remaining; ++attempt) {
    const ssize_t count = write(fd, bytes, remaining);
    if (count > 0) {
      bytes += count;
      remaining -= static_cast<size_t>(count);
    } else if (count == 0 || errno != EINTR) {
      break;
    }
  }
  if (remaining == 0) {
    const RexFaultSnapshotWriter writer = fault_snapshot_writer.load(std::memory_order_acquire);
    if (writer) writer(fd);
  }
}
}  // namespace

extern "C" void RexSetFaultSnapshotWriter(RexFaultSnapshotWriter writer) {
  fault_snapshot_writer.store(writer, std::memory_order_release);
}

extern "C" int RexSetFaultDiagnosticFileDescriptor(int fd) {
  if (fd < 0) { errno = EINVAL; return -1; }
  mach_timebase_info_data_t timebase{};
  if (mach_timebase_info(&timebase) != KERN_SUCCESS || !timebase.denom) {
    errno = EINVAL;
    return -1;
  }
  fault_image_slide.store(static_cast<uint64_t>(_dyld_get_image_vmaddr_slide(0)),
                          std::memory_order_relaxed);
  fault_timebase_numer.store(timebase.numer, std::memory_order_relaxed);
  fault_timebase_denom.store(timebase.denom, std::memory_order_relaxed);
  int expected = -1;
  if (!fault_record_fd.compare_exchange_strong(expected, fd, std::memory_order_release)) {
    errno = EALREADY;
    return -1;
  }
  return 0;
}

extern "C" int RexInitializeFaultDiagnostics(const char* path) {
  if (!path || !*path) { errno = EINVAL; return -1; }
  const int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (fd < 0) return -1;
  if (RexSetFaultDiagnosticFileDescriptor(fd) == 0) return 0;
  const int saved_errno = errno;
  close(fd);
  errno = saved_errno;
  return -1;
}

extern "C" void RexReadFaultDiagnostics(RexFaultDiagnosticsSnapshot* out) {
  if (!out) return;
  *out = {};
  out->handled_av_count = handled_av_count.load(std::memory_order_relaxed);
  out->unhandled_av_count = unhandled_av_count.load(std::memory_order_relaxed);
  out->first_av_valid = ReadFaultSite(first_av, out->first_av_pc,
      out->first_av_address, out->first_av_host_ticks);
  out->latest_av_valid = ReadFaultSite(latest_av, out->latest_av_pc,
      out->latest_av_address, out->latest_av_host_ticks);
  out->recorder_ready = fault_record_fd.load(std::memory_order_acquire) >= 0;
  out->unhandled_record_claimed = unhandled_record_claimed.load(std::memory_order_relaxed);
}

extern "C" int RexReadCurrentGuestFaultContext(RexGuestFaultContext* out) {
  return out && ReadGuestFaultSlot(current_guest_fault_slot, out);
}

extern "C" void RexSetGuestFaultContext(const RexGuestFaultContext* values) {
  if (!values) { RexClearGuestFaultContext(); return; }
  if (!current_guest_fault_slot) current_guest_fault_slot = FindGuestFaultSlot(true);
  GuestFaultSlot* slot = current_guest_fault_slot;
  if (!slot) return;
  const uint64_t sequence = slot->sequence.fetch_add(1, std::memory_order_acq_rel);
  slot->context_tag.store(values->context_tag, std::memory_order_relaxed);
  slot->r3.store(values->r3, std::memory_order_relaxed);
  slot->r29.store(values->r29, std::memory_order_relaxed);
  slot->r30.store(values->r30, std::memory_order_relaxed);
  slot->r31.store(values->r31, std::memory_order_relaxed);
  slot->lr.store(values->lr, std::memory_order_relaxed);
  slot->memory_base.store(values->memory_base, std::memory_order_relaxed);
  slot->valid.store(1, std::memory_order_relaxed);
  slot->sequence.store(sequence + 2, std::memory_order_release);
}

extern "C" void RexClearGuestFaultContext(void) {
  if (current_guest_fault_slot) {
    current_guest_fault_slot->valid.store(0, std::memory_order_release);
  }
}
#endif  // REX_PLATFORM_DARWIN

namespace rex::arch {

bool signal_handlers_installed_ = false;
struct sigaction original_sigill_handler_;
struct sigaction original_sigsegv_handler_;
#if REX_PLATFORM_DARWIN
struct sigaction original_sigbus_handler_;
#endif

// This can be as large as needed, but isn't often needed.
// As we will be sometimes firing many exceptions we want to avoid having to
// scan the table too much or invoke many custom handlers.
constexpr size_t kMaxHandlerCount = 8;

// All custom handlers, left-aligned and null terminated.
// Executed in order.
std::pair<ExceptionHandler::Handler, void*> handlers_[kMaxHandlerCount];

static void ExceptionHandlerCallback(int signal_number, siginfo_t* signal_info,
                                     void* signal_context);

static void PropagateUnhandledSignal(int signal_number, siginfo_t* signal_info,
                                     void* signal_context) {
  const struct sigaction* previous = &original_sigill_handler_;
  if (signal_number == SIGSEGV) previous = &original_sigsegv_handler_;
#if REX_PLATFORM_DARWIN
  if (signal_number == SIGBUS) previous = &original_sigbus_handler_;
#endif
  const struct sigaction saved = *previous;
  const bool synchronous = signal_info && signal_info->si_code > 0;
  const bool custom = saved.sa_handler != SIG_DFL && saved.sa_handler != SIG_IGN &&
      saved.sa_handler != nullptr &&
      (!(saved.sa_flags & SA_SIGINFO) || saved.sa_sigaction != ExceptionHandlerCallback);

  if (!synchronous && saved.sa_handler == SIG_IGN) return;
  struct sigaction default_action {};
  default_action.sa_handler = SIG_DFL;
  sigemptyset(&default_action.sa_mask);
  // An unhandled hardware fault must never return to the unchanged instruction.
  // Reset first so a fault inside a saved handler cannot recurse into this one.
  if (synchronous || !custom || (saved.sa_flags & SA_RESETHAND)) {
    sigaction(signal_number, &default_action, nullptr);
  }
  if (custom) {
    // Apply the saved handler's mask and SA_NODEFER semantics. A saved handler
    // may exit or recover via a nonlocal transfer. Merely returning from a
    // synchronous fault is deliberately fatal, even if it changes the context.
    sigset_t mask = saved.sa_mask;
    if (!(saved.sa_flags & SA_NODEFER)) sigaddset(&mask, signal_number);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    const sigset_t& interrupted_mask = static_cast<ucontext_t*>(signal_context)->uc_sigmask;
    if ((saved.sa_flags & SA_NODEFER) &&
        sigismember(&saved.sa_mask, signal_number) != 1 &&
        sigismember(&interrupted_mask, signal_number) != 1) {
      sigset_t unblock;
      sigemptyset(&unblock);
      sigaddset(&unblock, signal_number);
      sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
    }
    if (saved.sa_flags & SA_SIGINFO) saved.sa_sigaction(signal_number, signal_info, signal_context);
    else saved.sa_handler(signal_number);
    if (!synchronous) return;
  }
  // SIG_IGN cannot recover an invalid synchronous access. Re-raise with default
  // disposition on this thread; _exit is a final fallback if delivery fails.
  sigaction(signal_number, &default_action, nullptr);
  sigset_t unblock;
  sigemptyset(&unblock);
  sigaddset(&unblock, signal_number);
  sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
  raise(signal_number);
  _exit(128 + signal_number);
}

static void ExceptionHandlerCallback(int signal_number, siginfo_t* signal_info,
                                     void* signal_context) {
  const int saved_errno = errno;
  mcontext_t mcontext = reinterpret_cast<ucontext_t*>(signal_context)->uc_mcontext;

  HostThreadContext thread_context;

#if REX_ARCH_AMD64
#if REX_PLATFORM_DARWIN
  thread_context.rip = uint64_t(mcontext->__ss.__rip);
  thread_context.eflags = uint32_t(mcontext->__ss.__rflags);
  // The REG_ order may be different than the register indices in the
  // instruction encoding.
  thread_context.rax = uint64_t(mcontext->__ss.__rax);
  thread_context.rcx = uint64_t(mcontext->__ss.__rcx);
  thread_context.rdx = uint64_t(mcontext->__ss.__rdx);
  thread_context.rbx = uint64_t(mcontext->__ss.__rbx);
  thread_context.rsp = uint64_t(mcontext->__ss.__rsp);
  thread_context.rbp = uint64_t(mcontext->__ss.__rbp);
  thread_context.rsi = uint64_t(mcontext->__ss.__rsi);
  thread_context.rdi = uint64_t(mcontext->__ss.__rdi);
  thread_context.r8 = uint64_t(mcontext->__ss.__r8);
  thread_context.r9 = uint64_t(mcontext->__ss.__r9);
  thread_context.r10 = uint64_t(mcontext->__ss.__r10);
  thread_context.r11 = uint64_t(mcontext->__ss.__r11);
  thread_context.r12 = uint64_t(mcontext->__ss.__r12);
  thread_context.r13 = uint64_t(mcontext->__ss.__r13);
  thread_context.r14 = uint64_t(mcontext->__ss.__r14);
  thread_context.r15 = uint64_t(mcontext->__ss.__r15);
  std::memcpy(thread_context.xmm_registers, &mcontext->__fs.__fpu_xmm0,
              sizeof(thread_context.xmm_registers));
#else
  thread_context.rip = uint64_t(mcontext.gregs[REG_RIP]);
  thread_context.eflags = uint32_t(mcontext.gregs[REG_EFL]);
  // The REG_ order may be different than the register indices in the
  // instruction encoding.
  thread_context.rax = uint64_t(mcontext.gregs[REG_RAX]);
  thread_context.rcx = uint64_t(mcontext.gregs[REG_RCX]);
  thread_context.rdx = uint64_t(mcontext.gregs[REG_RDX]);
  thread_context.rbx = uint64_t(mcontext.gregs[REG_RBX]);
  thread_context.rsp = uint64_t(mcontext.gregs[REG_RSP]);
  thread_context.rbp = uint64_t(mcontext.gregs[REG_RBP]);
  thread_context.rsi = uint64_t(mcontext.gregs[REG_RSI]);
  thread_context.rdi = uint64_t(mcontext.gregs[REG_RDI]);
  thread_context.r8 = uint64_t(mcontext.gregs[REG_R8]);
  thread_context.r9 = uint64_t(mcontext.gregs[REG_R9]);
  thread_context.r10 = uint64_t(mcontext.gregs[REG_R10]);
  thread_context.r11 = uint64_t(mcontext.gregs[REG_R11]);
  thread_context.r12 = uint64_t(mcontext.gregs[REG_R12]);
  thread_context.r13 = uint64_t(mcontext.gregs[REG_R13]);
  thread_context.r14 = uint64_t(mcontext.gregs[REG_R14]);
  thread_context.r15 = uint64_t(mcontext.gregs[REG_R15]);
  std::memcpy(thread_context.xmm_registers, mcontext.fpregs->_xmm,
              sizeof(thread_context.xmm_registers));
#endif
#elif REX_ARCH_ARM64
#if REX_PLATFORM_DARWIN
  // macOS ARM64: mcontext_t is __darwin_mcontext64* (pointer, not value)
  std::memcpy(thread_context.x, mcontext->__ss.__x, sizeof(mcontext->__ss.__x));
#if __DARWIN_OPAQUE_ARM_THREAD_STATE64
  thread_context.x[29] = reinterpret_cast<uintptr_t>(mcontext->__ss.__opaque_fp);
  thread_context.x[30] = reinterpret_cast<uintptr_t>(mcontext->__ss.__opaque_lr);
  thread_context.sp = reinterpret_cast<uintptr_t>(mcontext->__ss.__opaque_sp);
  thread_context.pc = reinterpret_cast<uintptr_t>(mcontext->__ss.__opaque_pc);
#else
  thread_context.x[29] = mcontext->__ss.__fp;
  thread_context.x[30] = mcontext->__ss.__lr;
  thread_context.sp = mcontext->__ss.__sp;
  thread_context.pc = mcontext->__ss.__pc;
#endif
  thread_context.pstate = mcontext->__ss.__cpsr;
  thread_context.fpsr = mcontext->__ns.__fpsr;
  thread_context.fpcr = mcontext->__ns.__fpcr;
  static_assert(sizeof(thread_context.v) == sizeof(mcontext->__ns.__v));
  std::memcpy(thread_context.v, mcontext->__ns.__v, sizeof(thread_context.v));
#else
  // Linux ARM64: mcontext_t is struct sigcontext (value, not pointer)
  std::memcpy(thread_context.x, mcontext.regs, sizeof(thread_context.x));
  thread_context.sp = mcontext.sp;
  thread_context.pc = mcontext.pc;
  thread_context.pstate = mcontext.pstate;
  struct fpsimd_context* mcontext_fpsimd = nullptr;
  struct esr_context* mcontext_esr = nullptr;
  for (struct _aarch64_ctx* mcontext_extension =
           reinterpret_cast<struct _aarch64_ctx*>(mcontext.__reserved);
       mcontext_extension->magic;
       mcontext_extension = reinterpret_cast<struct _aarch64_ctx*>(
           reinterpret_cast<uint8_t*>(mcontext_extension) + mcontext_extension->size)) {
    switch (mcontext_extension->magic) {
      case FPSIMD_MAGIC:
        mcontext_fpsimd = reinterpret_cast<struct fpsimd_context*>(mcontext_extension);
        break;
      case ESR_MAGIC:
        mcontext_esr = reinterpret_cast<struct esr_context*>(mcontext_extension);
        break;
      default:
        break;
    }
  }
  assert_not_null(mcontext_fpsimd);
  if (mcontext_fpsimd) {
    thread_context.fpsr = mcontext_fpsimd->fpsr;
    thread_context.fpcr = mcontext_fpsimd->fpcr;
    std::memcpy(thread_context.v, mcontext_fpsimd->vregs, sizeof(thread_context.v));
  }
#endif  // REX_PLATFORM_DARWIN
#endif  // REX_ARCH

  Exception ex;
  switch (signal_number) {
    case SIGILL:
      ex.InitializeIllegalInstruction(&thread_context);
      break;
    case SIGBUS:
      // On macOS, SIGBUS (KERN_PROTECTION_FAILURE) is raised for writes to
      // read-protected pages — the same scenario that raises SIGSEGV on Linux.
      // Fall through to treat it as an access violation.
#if !REX_PLATFORM_DARWIN
      // On non-Mac POSIX, SIGBUS is a bus error (misaligned access), not a
      // protection fault — don't handle it here.
      assert_unhandled_case(signal_number);
      return;
#endif
      [[fallthrough]];
    case SIGSEGV: {
      Exception::AccessViolationOperation access_violation_operation;
#if REX_ARCH_AMD64
      // x86_pf_error_code::X86_PF_WRITE
      constexpr uint64_t kX86PageFaultErrorCodeWrite = UINT64_C(1) << 1;
#if REX_PLATFORM_DARWIN
      access_violation_operation = (uint64_t(mcontext->__es.__err) & kX86PageFaultErrorCodeWrite)
                                       ? Exception::AccessViolationOperation::kWrite
                                       : Exception::AccessViolationOperation::kRead;
#else
      access_violation_operation = (uint64_t(mcontext.gregs[REG_ERR]) & kX86PageFaultErrorCodeWrite)
                                       ? Exception::AccessViolationOperation::kWrite
                                       : Exception::AccessViolationOperation::kRead;
#endif
#elif REX_ARCH_ARM64
      // For a Data Abort (EC - ESR_EL1 bits 31:26 - 0b100100 from a lower
      // Exception Level, 0b100101 without a change in the Exception Level),
      // bit 6 is 0 for reading from a memory location, 1 for writing to a
      // memory location.
#if REX_PLATFORM_DARWIN
      {
        uint32_t mac_esr = mcontext->__es.__esr;
        if (((mac_esr >> 26) & 0b111110) == 0b100100) {
          access_violation_operation = (mac_esr & (1U << 6))
                                           ? Exception::AccessViolationOperation::kWrite
                                           : Exception::AccessViolationOperation::kRead;
        } else {
          // A non-data-abort may be an instruction fetch from an invalid PC.
          // Do not dereference that PC or assert/log from inside the handler.
          access_violation_operation = Exception::AccessViolationOperation::kUnknown;
        }
      }
#else
      if (mcontext_esr && ((mcontext_esr->esr >> 26) & 0b111110) == 0b100100) {
        access_violation_operation = (mcontext_esr->esr & (UINT64_C(1) << 6))
                                         ? Exception::AccessViolationOperation::kWrite
                                         : Exception::AccessViolationOperation::kRead;
      } else {
        // Determine the memory access direction based on which instruction has
        // requested it.
        // esr_context may be unavailable on certain hosts (for instance, on
        // Android, it was added only in NDK r16 - which is the first NDK
        // version to support the Android API level 27, while NDK r15 doesn't
        // have esr_context in its API 26 sigcontext.h).
        // On AArch64 (unlike on AArch32), the program counter is the address of
        // the currently executing instruction.
        bool instruction_is_store;
        if (IsArm64LoadPrefetchStore(*reinterpret_cast<const uint32_t*>(mcontext.pc),
                                     instruction_is_store)) {
          access_violation_operation = instruction_is_store
                                           ? Exception::AccessViolationOperation::kWrite
                                           : Exception::AccessViolationOperation::kRead;
        } else {
          assert_always(
              "No ESR in the exception thread context, or it's not a Data "
              "Abort, and the faulting instruction is not a known load, "
              "prefetch or store instruction");
          access_violation_operation = Exception::AccessViolationOperation::kUnknown;
        }
      }
#endif  // REX_PLATFORM_DARWIN
#else
      access_violation_operation = Exception::AccessViolationOperation::kUnknown;
#endif  // REX_ARCH
      ex.InitializeAccessViolation(&thread_context,
                                   reinterpret_cast<uint64_t>(signal_info->si_addr),
                                   access_violation_operation);
    } break;
    default:
      assert_unhandled_case(signal_number);
  }

#if REX_PLATFORM_DARWIN
  const bool is_access_violation = ex.code() == Exception::Code::kAccessViolation;
  if (is_access_violation) {
    const uint64_t ticks = mach_absolute_time();
    StoreFaultSite(first_av, ex.pc(), ex.fault_address(), ticks, true);
    StoreFaultSite(latest_av, ex.pc(), ex.fault_address(), ticks, false);
  }
#endif

  for (size_t i = 0; i < rex::countof(handlers_) && handlers_[i].first; ++i) {
    if (handlers_[i].first(&ex, handlers_[i].second)) {
      // Exception handled.
#if REX_PLATFORM_DARWIN
      if (is_access_violation) handled_av_count.fetch_add(1, std::memory_order_relaxed);
#endif
#if REX_ARCH_AMD64
#if REX_PLATFORM_DARWIN
      mcontext->__ss.__rip = thread_context.rip;
      mcontext->__ss.__rflags = thread_context.eflags;
      uint32_t modified_register_index;
      uint16_t modified_int_registers_remaining = ex.modified_int_registers();
      while (rex::bit_scan_forward(modified_int_registers_remaining, &modified_register_index)) {
        modified_int_registers_remaining &= ~(UINT16_C(1) << modified_register_index);
        switch (modified_register_index) {
          case 0:
            mcontext->__ss.__rax = thread_context.int_registers[modified_register_index];
            break;
          case 1:
            mcontext->__ss.__rcx = thread_context.int_registers[modified_register_index];
            break;
          case 2:
            mcontext->__ss.__rdx = thread_context.int_registers[modified_register_index];
            break;
          case 3:
            mcontext->__ss.__rbx = thread_context.int_registers[modified_register_index];
            break;
          case 4:
            mcontext->__ss.__rsp = thread_context.int_registers[modified_register_index];
            break;
          case 5:
            mcontext->__ss.__rbp = thread_context.int_registers[modified_register_index];
            break;
          case 6:
            mcontext->__ss.__rsi = thread_context.int_registers[modified_register_index];
            break;
          case 7:
            mcontext->__ss.__rdi = thread_context.int_registers[modified_register_index];
            break;
          case 8:
            mcontext->__ss.__r8 = thread_context.int_registers[modified_register_index];
            break;
          case 9:
            mcontext->__ss.__r9 = thread_context.int_registers[modified_register_index];
            break;
          case 10:
            mcontext->__ss.__r10 = thread_context.int_registers[modified_register_index];
            break;
          case 11:
            mcontext->__ss.__r11 = thread_context.int_registers[modified_register_index];
            break;
          case 12:
            mcontext->__ss.__r12 = thread_context.int_registers[modified_register_index];
            break;
          case 13:
            mcontext->__ss.__r13 = thread_context.int_registers[modified_register_index];
            break;
          case 14:
            mcontext->__ss.__r14 = thread_context.int_registers[modified_register_index];
            break;
          case 15:
            mcontext->__ss.__r15 = thread_context.int_registers[modified_register_index];
            break;
          default:
            assert_unhandled_case(modified_register_index);
        }
      }
      uint16_t modified_xmm_registers_remaining = ex.modified_xmm_registers();
      while (rex::bit_scan_forward(modified_xmm_registers_remaining, &modified_register_index)) {
        modified_xmm_registers_remaining &= ~(UINT16_C(1) << modified_register_index);
        std::memcpy(reinterpret_cast<char*>(&mcontext->__fs.__fpu_xmm0) +
                        modified_register_index * sizeof(vec128_t),
                    &thread_context.xmm_registers[modified_register_index], sizeof(vec128_t));
      }
#else
      mcontext.gregs[REG_RIP] = greg_t(thread_context.rip);
      mcontext.gregs[REG_EFL] = greg_t(thread_context.eflags);
      uint32_t modified_register_index;
      // The order must match the order in X64Register.
      static const size_t kIntRegisterMap[] = {
          REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI,
          REG_R8,  REG_R9,  REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15,
      };
      uint16_t modified_int_registers_remaining = ex.modified_int_registers();
      while (rex::bit_scan_forward(modified_int_registers_remaining, &modified_register_index)) {
        modified_int_registers_remaining &= ~(UINT16_C(1) << modified_register_index);
        mcontext.gregs[kIntRegisterMap[modified_register_index]] =
            thread_context.int_registers[modified_register_index];
      }
      uint16_t modified_xmm_registers_remaining = ex.modified_xmm_registers();
      while (rex::bit_scan_forward(modified_xmm_registers_remaining, &modified_register_index)) {
        modified_xmm_registers_remaining &= ~(UINT16_C(1) << modified_register_index);
        std::memcpy(&mcontext.fpregs->_xmm[modified_register_index],
                    &thread_context.xmm_registers[modified_register_index], sizeof(vec128_t));
      }
#endif
#elif REX_ARCH_ARM64
      uint32_t modified_register_index;
      uint32_t modified_x_registers_remaining = ex.modified_x_registers();
#if REX_PLATFORM_DARWIN
      while (rex::bit_scan_forward(modified_x_registers_remaining, &modified_register_index)) {
        modified_x_registers_remaining &= ~(UINT32_C(1) << modified_register_index);
        if (modified_register_index < 29) {
          mcontext->__ss.__x[modified_register_index] = thread_context.x[modified_register_index];
        } else if (modified_register_index == 29) {
#if __DARWIN_OPAQUE_ARM_THREAD_STATE64
          mcontext->__ss.__opaque_fp = reinterpret_cast<void*>(thread_context.x[29]);
#else
          mcontext->__ss.__fp = thread_context.x[29];
#endif
        } else {
#if __DARWIN_OPAQUE_ARM_THREAD_STATE64
          mcontext->__ss.__opaque_lr = reinterpret_cast<void*>(thread_context.x[30]);
#else
          mcontext->__ss.__lr = thread_context.x[30];
#endif
        }
      }
#if __DARWIN_OPAQUE_ARM_THREAD_STATE64
      mcontext->__ss.__opaque_sp = reinterpret_cast<void*>(thread_context.sp);
      mcontext->__ss.__opaque_pc = reinterpret_cast<void*>(thread_context.pc);
#else
      mcontext->__ss.__sp = thread_context.sp;
      mcontext->__ss.__pc = thread_context.pc;
#endif
      mcontext->__ss.__cpsr = thread_context.pstate;
      mcontext->__ns.__fpsr = thread_context.fpsr;
      mcontext->__ns.__fpcr = thread_context.fpcr;
      {
        uint32_t modified_v_registers_remaining = ex.modified_v_registers();
        while (rex::bit_scan_forward(modified_v_registers_remaining, &modified_register_index)) {
          modified_v_registers_remaining &= ~(UINT32_C(1) << modified_register_index);
          std::memcpy(&mcontext->__ns.__v[modified_register_index],
                      &thread_context.v[modified_register_index], sizeof(vec128_t));
        }
      }
#else
      while (rex::bit_scan_forward(modified_x_registers_remaining, &modified_register_index)) {
        modified_x_registers_remaining &= ~(UINT32_C(1) << modified_register_index);
        mcontext.regs[modified_register_index] = thread_context.x[modified_register_index];
      }
      mcontext.sp = thread_context.sp;
      mcontext.pc = thread_context.pc;
      mcontext.pstate = thread_context.pstate;
      if (mcontext_fpsimd) {
        mcontext_fpsimd->fpsr = thread_context.fpsr;
        mcontext_fpsimd->fpcr = thread_context.fpcr;
        uint32_t modified_v_registers_remaining = ex.modified_v_registers();
        while (rex::bit_scan_forward(modified_v_registers_remaining, &modified_register_index)) {
          modified_v_registers_remaining &= ~(UINT32_C(1) << modified_register_index);
          std::memcpy(&mcontext_fpsimd->vregs[modified_register_index],
                      &thread_context.v[modified_register_index], sizeof(vec128_t));
          mcontext.regs[modified_register_index] = thread_context.x[modified_register_index];
        }
      }
#endif  // REX_PLATFORM_DARWIN
#endif  // REX_ARCH
      errno = saved_errno;
      return;
    }
  }
#if REX_PLATFORM_DARWIN
  if (is_access_violation) unhandled_av_count.fetch_add(1, std::memory_order_relaxed);
  if (signal_info && signal_info->si_code > 0) {
    RecordFirstUnhandledFault(signal_number, signal_info, mcontext,
        static_cast<uint32_t>(ex.access_violation_operation()));
  }
#endif
  PropagateUnhandledSignal(signal_number, signal_info, signal_context);
  errno = saved_errno;  // A user-generated signal may legitimately be ignored.
}

void ExceptionHandler::Install(Handler fn, void* data) {
  if (!signal_handlers_installed_) {
    struct sigaction signal_handler;

    std::memset(&signal_handler, 0, sizeof(signal_handler));
    signal_handler.sa_sigaction = ExceptionHandlerCallback;
    signal_handler.sa_flags = SA_SIGINFO;

    if (sigaction(SIGILL, &signal_handler, &original_sigill_handler_) != 0) {
      assert_always("Failed to install new SIGILL handler");
    }
    if (sigaction(SIGSEGV, &signal_handler, &original_sigsegv_handler_) != 0) {
      assert_always("Failed to install new SIGSEGV handler");
    }
#if REX_PLATFORM_DARWIN
    if (sigaction(SIGBUS, &signal_handler, &original_sigbus_handler_) != 0) {
      assert_always("Failed to install new SIGBUS handler");
    }
#endif
    signal_handlers_installed_ = true;
  }

  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (!handlers_[i].first) {
      handlers_[i].first = fn;
      handlers_[i].second = data;
      return;
    }
  }
  assert_always("Too many exception handlers installed");
}

void ExceptionHandler::Uninstall(Handler fn, void* data) {
  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (handlers_[i].first == fn && handlers_[i].second == data) {
      for (; i < rex::countof(handlers_) - 1; ++i) {
        handlers_[i] = handlers_[i + 1];
      }
      handlers_[i].first = nullptr;
      handlers_[i].second = nullptr;
      break;
    }
  }

  bool has_any = false;
  for (size_t i = 0; i < rex::countof(handlers_); ++i) {
    if (handlers_[i].first) {
      has_any = true;
      break;
    }
  }
  if (!has_any) {
    if (signal_handlers_installed_) {
      if (sigaction(SIGILL, &original_sigill_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGILL handler");
      }
      if (sigaction(SIGSEGV, &original_sigsegv_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGSEGV handler");
      }
#if REX_PLATFORM_DARWIN
      if (sigaction(SIGBUS, &original_sigbus_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGBUS handler");
      }
#endif
      signal_handlers_installed_ = false;
    }
  }
}

}  // namespace rex::arch

#endif  // REX_PLATFORM_LINUX || REX_PLATFORM_DARWIN
