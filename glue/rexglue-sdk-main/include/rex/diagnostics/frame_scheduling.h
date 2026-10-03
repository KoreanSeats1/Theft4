#pragma once
#include <stdint.h>
#ifdef __cplusplus
#include <atomic>
extern "C" {
#endif
#define REX_FRAME_SCHEDULING_FIELDS 26
typedef struct rex_frame_scheduling_sample {
  uint64_t value[REX_FRAME_SCHEDULING_FIELDS];
} rex_frame_scheduling_sample;
// Mode is live, process-wide and independent of capture. ON adds a reversible
// QoS override only to actual native/guest Present participants.
void rex_frame_scheduling_set_mode(int enabled);
int rex_frame_scheduling_mode(void);
uint64_t rex_frame_scheduling_generation(void);
void rex_frame_scheduling_capture(int enabled);
void rex_frame_scheduling_active(int active);
uint32_t rex_frame_scheduling_read(uint32_t* cursor, rex_frame_scheduling_sample* rows,
                                   uint32_t capacity);
uint64_t rex_frame_scheduling_dropped(void);
const char* rex_frame_scheduling_columns(void);
#ifdef __cplusplus
}
namespace rex::diagnostics::frame_schedule {
extern std::atomic<bool> capturing;
enum Role : uint32_t {
  Guest = 1,
  MainGuest = 2,
  NativeRenderer = 4,
  FrameProducer = 8,
  Audio = 16
};
enum Work : uint32_t {
  RegisterReference = 1,
  BufferReference = 2,
  AudioPrepare = 3,
  AudioMix = 4,
  XmaWork = 5,
  XmaDecode = 6
};
void RegisterGuest(uint32_t guest_id, uint32_t entry_pc, uint32_t guest_cpu, int32_t guest_priority,
                   bool main);
void MarkAudio();
// Caller must be the owning thread, at a Present boundary outside host locks.
void FrameBoundary(Role role, uint64_t frame, uint32_t caller = 0);
void SuspendCurrent();
struct CpuToken {
  uint64_t cpu = 0, wall = 0, call = 0, epoch = 0, mode = 0;
  uint32_t kind = 0, site = 0;
};
CpuToken BeginWork(Work work, uint32_t site);
void EndWork(CpuToken token);
class CpuScope {
 public:
  CpuScope(Work work, uint32_t site = 0)
      : token_(capturing.load(std::memory_order_relaxed) ? BeginWork(work, site) : CpuToken{}) {}
  CpuScope(const CpuScope&) = delete;
  CpuScope& operator=(const CpuScope&) = delete;
  ~CpuScope() {
    if (token_.wall)
      EndWork(token_);
  }

 private:
  CpuToken token_;
};
}  // namespace rex::diagnostics::frame_schedule
#endif
