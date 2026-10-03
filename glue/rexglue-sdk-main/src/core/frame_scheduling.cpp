#include <rex/diagnostics/frame_scheduling.h>
#include <rex/chrono/clock.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <memory>
#include <time.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <mach/mach.h>
#endif

// This comparison is an iOS integration; other SDK hosts retain no-op hooks.
#if defined(__APPLE__) && TARGET_OS_IPHONE
namespace rex::diagnostics::frame_schedule {
std::atomic<bool> capturing{false};
namespace {
// The low bit is mode, higher bits its generation. One load captures both.
std::atomic<uint64_t> configuration{0}, capture_epoch{0};
std::atomic<bool> foreground{true};
constexpr uint32_t kThreads = 128, kRecords = 256, kBufferWords = 8192;
constexpr uint32_t kRegisterIterations = 16384, kBufferIterations = 16384;
enum Field {
  Event,
  Time,
  Thread,
  GuestId,
  Entry,
  GuestCpu,
  GuestPriority,
  Roles,
  Mode,
  Generation,
  RequestedQos,
  RequestedRelative,
  OverrideActive,
  Result,
  Policy,
  CurrentPriority,
  WorkKind,
  Units,
  CpuNs,
  WallNs,
  Checksum,
  OverheadNs,
  Frame,
  Disabled,
  CaptureEpoch,
  Site
};
static_assert(Site + 1 == REX_FRAME_SCHEDULING_FIELDS);
// Single producer (owning thread), single serial export consumer per ring.
// Reader never sees an unpublished slot; writer never overwrites unread data.
struct alignas(128) Ring {
  alignas(128) std::atomic<uint64_t> written{0};
  alignas(128) std::atomic<uint64_t> read{0};
  std::atomic<uint64_t> dropped{0};
  rex_frame_scheduling_sample rows[kRecords]{};
};
Ring rings[kThreads];
std::atomic<uint32_t> registrations{0}, witness_slots{0};
uint64_t Now() {
  const auto hz = rex::chrono::Clock::QueryHostTickFrequency();
  const auto ticks = rex::chrono::Clock::QueryHostTickCount();
  return hz ? uint64_t((__uint128_t(ticks) * 1000000000) / hz) : 0;
}
uint64_t Cpu() {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  timespec t{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0)
    return uint64_t(t.tv_sec) * 1000000000 + uint64_t(t.tv_nsec);
#endif
  return 0;
}
struct Local {
  Ring* ring = nullptr;
  uint64_t id = 0, applied = UINT64_MAX, frame = 0, next_witness = 0;
  uint32_t guest_id = 0, entry = 0, guest_cpu = 0, roles = 0, caller = 0;
  int32_t guest_priority = 0;
  bool registered = false, disabled = false, applied_foreground = false, witness_reserved = false;
  uint64_t retry_after = 0;
  uint64_t calls[7]{};
  uint64_t witness_start = 0, witness_cpu = 0;
  std::unique_ptr<std::array<uint64_t, kBufferWords>> buffer;
#if defined(__APPLE__) && TARGET_OS_IPHONE
  pthread_override_t override = nullptr;
  ~Local() {
    if (override)
      pthread_override_qos_class_end_np(override);
  }
#endif
};
thread_local Local local;
void Register() {
  if (local.registered)
    return;
  local.registered = true;
#if defined(__APPLE__)
  pthread_threadid_np(nullptr, &local.id);
#endif
  const uint32_t n = registrations.fetch_add(1, std::memory_order_relaxed);
  if (n < kThreads)
    local.ring = &rings[n];
}
void Push(const rex_frame_scheduling_sample& row) {
  auto* q = local.ring;
  if (!q)
    return;
  const auto w = q->written.load(std::memory_order_relaxed);
  if (w - q->read.load(std::memory_order_acquire) >= kRecords) {
    q->dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  q->rows[w % kRecords] = row;
  q->written.store(w + 1, std::memory_order_release);
}
rex_frame_scheduling_sample Row(uint64_t event, uint64_t mode) {
  rex_frame_scheduling_sample row{};
  auto* v = row.value;
  v[Event] = event;
  v[Time] = Now();
  v[Thread] = local.id;
  v[GuestId] = local.guest_id;
  v[Entry] = local.entry;
  v[GuestCpu] = local.guest_cpu;
  v[GuestPriority] = uint64_t(int64_t(local.guest_priority));
  v[Roles] = local.roles;
  v[Mode] = mode & 1;
  v[Generation] = mode >> 1;
  v[Frame] = local.frame;
  v[Site] = local.caller;
  v[Disabled] = local.disabled;
  v[CaptureEpoch] = capture_epoch.load(std::memory_order_relaxed);
#if defined(__APPLE__) && TARGET_OS_IPHONE
  v[OverrideActive] = local.override != nullptr;
#endif
  return row;
}
void Identity(uint64_t event, int result = 0) {
  auto row = Row(event, configuration.load(std::memory_order_acquire));
  row.value[Result] = uint64_t(int64_t(result));
#if defined(__APPLE__) && TARGET_OS_IPHONE
  qos_class_t qos = QOS_CLASS_UNSPECIFIED;
  int relative = 0;
  const int query = pthread_get_qos_class_np(pthread_self(), &qos, &relative);
  row.value[RequestedQos] = qos;
  row.value[RequestedRelative] = uint64_t(int64_t(relative));
  if (!result && query)
    row.value[Result] = query;
  thread_extended_info_data_t info{};
  mach_msg_type_number_t count = THREAD_EXTENDED_INFO_COUNT;
  const mach_port_t port = mach_thread_self();
  if (thread_info(port, THREAD_EXTENDED_INFO, reinterpret_cast<thread_info_t>(&info), &count) ==
      KERN_SUCCESS) {
    row.value[Policy] = info.pth_policy;
    row.value[CurrentPriority] = info.pth_curpri;
  }
  mach_port_deallocate(mach_task_self(), port);
#endif
  Push(row);
}
void Apply() {
  const uint64_t config = configuration.load(std::memory_order_acquire);
  const bool active = foreground.load(std::memory_order_acquire);
  if (local.applied == config && local.applied_foreground == active) {
    if (!local.retry_after || Now() < local.retry_after)
      return;
  }
  local.retry_after = 0;
  local.applied = config;
  local.applied_foreground = active;
  int result = 0;
#if defined(__APPLE__) && TARGET_OS_IPHONE
  const bool wanted = (config & 1) && active && (local.roles & (NativeRenderer | FrameProducer)) &&
                      !(local.roles & Audio);
  if (wanted && !local.override) {
    // An override leaves the original requested QoS intact, including an
    // unspecified base. Ending it restores that base without raw priorities.
    errno = 0;
    local.override =
        pthread_override_qos_class_start_np(pthread_self(), QOS_CLASS_USER_INTERACTIVE, -2);
    if (!local.override)
      result = errno ? errno : ENOTSUP;
  } else if (!wanted && local.override) {
    result = pthread_override_qos_class_end_np(local.override);
    if (!result)
      local.override = nullptr;
  }
#else
  if (config & 1)
    result = ENOTSUP;
#endif
  if (result)
    local.retry_after = Now() + 2000000000ull;
  Identity(2, result);
}
// Opaque register dependencies prevent folding/vectorization. No guest state,
// floating point, allocation, locks, syscalls or mutable shared cache lines.
__attribute__((noinline)) uint64_t Registers(uint32_t count, uint64_t seed) {
  uint64_t x = seed, y = 0xd1b54a32d192ed03ull;
  for (uint32_t i = 0; i < count; ++i) {
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    y += x * 0x2545f4914f6cdd1dull;
    __asm__ volatile("" : "+r"(x), "+r"(y));
  }
  return x ^ y;
}
__attribute__((noinline)) uint64_t Buffer(uint32_t count, const uint64_t* data) {
  const volatile uint64_t* input = data;
  uint64_t x = 0x9e3779b97f4a7c15ull;
  for (uint32_t i = 0; i < count; ++i)
    x = (x ^ (input[(i * 4051u) & (kBufferWords - 1)])) + 0x517cc1b727220a95ull;
  return x;
}
void Witness(uint64_t config) {
  if (!capturing.load(std::memory_order_relaxed) || local.disabled ||
      !foreground.load(std::memory_order_relaxed))
    return;
  if (!local.witness_reserved) {
    local.witness_reserved = true;
    if (witness_slots.fetch_add(1, std::memory_order_relaxed) >= 4) {
      local.disabled = true;
      Identity(5, ENOSPC);
      return;
    }
  }
  const uint64_t now = Now();
  if (now < local.next_witness)
    return;
  local.next_witness = now + 2000000000ull;
  const uint64_t overhead_cpu = Cpu();
  if (!overhead_cpu) {
    local.disabled = true;
    Identity(5, ENOTSUP);
    return;
  }
  const uint64_t epoch = capture_epoch.load(std::memory_order_relaxed);
  if (!local.buffer) {
    local.buffer = std::make_unique<std::array<uint64_t, kBufferWords>>();
    uint64_t x = 0x6a09e667f3bcc909ull;
    for (auto& value : *local.buffer) {
      x ^= x << 13;
      x ^= x >> 7;
      x ^= x << 17;
      value = x;
    }
    local.witness_start = now;
  }
  Identity(1);  // Periodic observed priority; requested QoS excludes overrides.
  for (uint32_t kind = 1; kind <= 2; ++kind) {
    const uint32_t count = kind == 1 ? kRegisterIterations : kBufferIterations;
    const uint64_t c0 = Cpu(), w0 = Now();
    const uint64_t sum =
        kind == 1 ? Registers(count, 0x243f6a8885a308d3ull) : Buffer(count, local.buffer->data());
    const uint64_t w1 = Now(), c1 = Cpu();
    auto row = Row(3, config);
    row.value[WorkKind] = kind;
    row.value[Units] = count;
    row.value[Checksum] = sum;
    row.value[CpuNs] = c1 >= c0 ? c1 - c0 : 0;
    row.value[WallNs] = w1 - w0;
    row.value[CaptureEpoch] = epoch;
    Push(row);
  }
  const uint64_t end_cpu = Cpu(), used = end_cpu >= overhead_cpu ? end_cpu - overhead_cpu : 0;
  local.witness_cpu += used;
  // Freeze identical work counts. A one-off 2 ms allowance covers first-use
  // setup; thereafter budget <=0.025% of one CPU per participating thread.
  if (!used || used > 2000000 || local.witness_cpu > 2000000 + (now - local.witness_start) / 4000)
    local.disabled = true;
  auto row = Row(5, config);
  row.value[OverheadNs] = used;
  row.value[CaptureEpoch] = epoch;
  Push(row);
}
}  // namespace
void RegisterGuest(uint32_t id, uint32_t entry, uint32_t cpu, int32_t priority, bool main) {
  Register();
  local.guest_id = id;
  local.entry = entry;
  local.guest_cpu = cpu;
  local.guest_priority = priority;
  local.roles |= Guest | (main ? MainGuest : 0);
  Identity(1);
}
void MarkAudio() {
  Register();
  local.roles |= Audio;
  local.applied = UINT64_MAX;
  Apply();
  Identity(1);
}
void FrameBoundary(Role role, uint64_t frame, uint32_t caller) {
  Register();
  local.frame = frame;
  local.caller = caller;
  if (!(local.roles & role)) {
    local.roles |= role;
    local.applied = UINT64_MAX;
    Identity(1);
  }
  Apply();
  if (!(local.roles & Audio))
    Witness(configuration.load(std::memory_order_acquire));
}
void SuspendCurrent() {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  if (local.override) {
    const int result = pthread_override_qos_class_end_np(local.override);
    if (!result)
      local.override = nullptr;
    Identity(2, result);
  }
#endif
  local.applied = UINT64_MAX;
}
CpuToken BeginWork(Work work, uint32_t site) {
  Register();
  const uint64_t call = ++local.calls[work];
  // Count each call but time only 1/64 DSP batches and 1/256 decoder calls.
  const uint64_t mask = work >= XmaWork ? 255 : 63;
  if ((call & mask) != 1)
    return {};
  return {Cpu(),
          Now(),
          call,
          capture_epoch.load(std::memory_order_relaxed),
          configuration.load(std::memory_order_acquire),
          uint32_t(work),
          site};
}
void EndWork(CpuToken token) {
  const uint64_t wall = Now(), cpu = Cpu();
  auto row = Row(4, token.mode);
  row.value[WorkKind] = token.kind;
  row.value[Units] = token.call;
  row.value[WallNs] = wall - token.wall;
  row.value[CpuNs] = token.cpu && cpu >= token.cpu ? cpu - token.cpu : 0;
  row.value[CaptureEpoch] = token.epoch;
  row.value[Site] = token.site;
  Push(row);
}
}  // namespace rex::diagnostics::frame_schedule
using namespace rex::diagnostics::frame_schedule;
extern "C" void rex_frame_scheduling_set_mode(int enabled) {
  uint64_t old = configuration.load(std::memory_order_relaxed), next;
  do {
    if (bool(old & 1) == bool(enabled))
      return;
    next = ((old + 2) & ~uint64_t(1)) | uint64_t(enabled != 0);
  } while (!configuration.compare_exchange_weak(old, next, std::memory_order_release,
                                                std::memory_order_relaxed));
}
extern "C" int rex_frame_scheduling_mode() {
  return configuration.load(std::memory_order_acquire) & 1;
}
extern "C" uint64_t rex_frame_scheduling_generation() {
  return configuration.load(std::memory_order_acquire) >> 1;
}
extern "C" void rex_frame_scheduling_capture(int enabled) {
  if (enabled)
    capture_epoch.fetch_add(1, std::memory_order_relaxed);
  capturing.store(enabled != 0, std::memory_order_release);
}
extern "C" void rex_frame_scheduling_active(int value) {
  foreground.store(value != 0, std::memory_order_release);
}
extern "C" uint32_t rex_frame_scheduling_read(uint32_t* cursor, rex_frame_scheduling_sample* rows,
                                              uint32_t capacity) {
  if (!cursor || !rows)
    return 0;
  uint32_t n = 0;
  while (*cursor < kThreads && n < capacity) {
    auto& q = rings[*cursor];
    auto r = q.read.load(std::memory_order_relaxed);
    const auto w = q.written.load(std::memory_order_acquire);
    while (r < w && n < capacity)
      rows[n++] = q.rows[(r++) % kRecords];
    q.read.store(r, std::memory_order_release);
    if (r == w)
      ++*cursor;
  }
  return n;
}
extern "C" uint64_t rex_frame_scheduling_dropped() {
  if (registrations.load(std::memory_order_relaxed) > kThreads)
    return UINT64_MAX;
  uint64_t n = 0;
  for (auto& q : rings)
    n += q.dropped.load(std::memory_order_relaxed);
  return n;
}
extern "C" const char* rex_frame_scheduling_columns() {
  return "event,monotonic_ns,thread_id,guest_id,entry_pc,guest_cpu,guest_priority,roles,mode,"
         "generation,requested_qos,requested_relative,override_active,result,policy,current_"
         "priority,work_kind,units,cpu_ns,wall_ns,checksum,overhead_cpu_ns,frame,witness_disabled,"
         "capture_epoch,site";
}

#else
namespace rex::diagnostics::frame_schedule {
std::atomic<bool> capturing{false};
void RegisterGuest(uint32_t, uint32_t, uint32_t, int32_t, bool) {}
void MarkAudio() {}
void FrameBoundary(Role, uint64_t, uint32_t) {}
void SuspendCurrent() {}
CpuToken BeginWork(Work, uint32_t) {
  return {};
}
void EndWork(CpuToken) {}
}  // namespace rex::diagnostics::frame_schedule
extern "C" void rex_frame_scheduling_set_mode(int) {}
extern "C" int rex_frame_scheduling_mode() {
  return 0;
}
extern "C" uint64_t rex_frame_scheduling_generation() {
  return 0;
}
extern "C" void rex_frame_scheduling_capture(int) {}
extern "C" void rex_frame_scheduling_active(int) {}
extern "C" uint32_t rex_frame_scheduling_read(uint32_t*, rex_frame_scheduling_sample*, uint32_t) {
  return 0;
}
extern "C" uint64_t rex_frame_scheduling_dropped() {
  return 0;
}
extern "C" const char* rex_frame_scheduling_columns() {
  return "";
}
#endif
