/** Signal-safe Darwin fault diagnostics. All initialization happens outside signals. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A value snapshot captured by a guest wrapper in normal execution. This is not
// a pointer to guest memory. context_tag identifies the instrumented function.
typedef struct RexGuestFaultContext {
  uint32_t context_tag;
  uint32_t r3;
  uint32_t r29;
  uint32_t r30;
  uint32_t r31;
  uint32_t lr;
  uint64_t memory_base;
} RexGuestFaultContext;

typedef struct RexFaultDiagnosticsSnapshot {
  uint64_t handled_av_count;
  uint64_t unhandled_av_count;
  uint64_t first_av_pc;
  uint64_t first_av_address;
  uint64_t first_av_host_ticks;
  uint64_t latest_av_pc;
  uint64_t latest_av_address;
  uint64_t latest_av_host_ticks;
  uint32_t first_av_valid;
  uint32_t latest_av_valid;
  uint32_t recorder_ready;
  uint32_t unhandled_record_claimed;
} RexFaultDiagnosticsSnapshot;

// Native-endian fixed-size binary record, currently little-endian on Darwin.
// magic is ASCII RXFLT001. Access kind: 0 unknown, 1 read, 2 write.
// host_ticks uses mach_absolute_time; nanoseconds = ticks * numer / denom.
// host_arch: 1 ARM64, 2 x86-64. x[] is meaningful only for ARM64.
typedef struct RexUnhandledFaultRecord {
  uint64_t magic;
  uint32_t version;
  uint32_t record_size;
  int32_t signal_number;
  int32_t signal_code;
  uint32_t access_kind;
  uint32_t host_arch;
  uint64_t host_ticks;
  uint64_t pc;
  uint64_t fault_address;
  uint64_t esr;
  uint64_t far;
  uint64_t sp;
  uint64_t pstate;
  uint64_t x[31];
  uint32_t fpsr;
  uint32_t fpcr;
  uint32_t timebase_numer;
  uint32_t timebase_denom;
  uint32_t guest_context_valid;
  uint32_t reserved;
  RexGuestFaultContext guest;
  uint64_t image_slide;  // Main executable ASLR slide; captured at normal startup.
} RexUnhandledFaultRecord;

// Darwin only. Opens an append-only 0600 file once. Returns 0 or -1 with errno.
// Call during normal startup. The descriptor remains open for process lifetime.
int RexInitializeFaultDiagnostics(const char* path);
// Alternative for a caller-owned preopened descriptor. Set once at startup;
// keep the descriptor open and do not reuse it until process termination.
int RexSetFaultDiagnosticFileDescriptor(int fd);
void RexReadFaultDiagnostics(RexFaultDiagnosticsSnapshot* out);
// Optional fatal-only appendix writer, invoked once after the complete fixed
// record. Install at normal startup. Callback must use only signal-safe bounded
// operations and must not allocate, lock, format, or access mutable containers.
typedef void (*RexFaultSnapshotWriter)(int fd);
void RexSetFaultSnapshotWriter(RexFaultSnapshotWriter writer);
// Normal execution only. Slots are preallocated; no TLS lookup occurs in the
// signal handler. Up to 128 distinct thread identities may register per process.
// Read returns 1 if valid, 0 otherwise. Save/restore around nested wrappers.
int RexReadCurrentGuestFaultContext(RexGuestFaultContext* out);
void RexSetGuestFaultContext(const RexGuestFaultContext* values);
void RexClearGuestFaultContext(void);

#ifdef __cplusplus
}
#endif
