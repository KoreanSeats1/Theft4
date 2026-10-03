#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <time.h>
#include <vector>
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#endif

namespace rex::graphics::gta4_native {
// One completed CPU batch at a time. No per-draw dispatch, guest access, Vulkan
// call or normal-frame wait. The renderer still owns GPU retirement callbacks.
template <typename Command, typename Recycler>
class NativeDeferredCleanup {
 public:
  using Owner = std::unique_ptr<Command>;
  ~NativeDeferredCleanup() {
    Wait();
#if defined(__APPLE__) && !OS_OBJECT_USE_OBJC
    if (queue_) dispatch_release(queue_);
    if (group_) dispatch_release(group_);
#endif
  }
  void Initialize(bool requested, uint32_t available_cpus) {
#if defined(__APPLE__)
    if (!requested || available_cpus < 4 || group_) return;
    group_ = dispatch_group_create();
    auto attributes = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,QOS_CLASS_UTILITY,0);
    queue_ = dispatch_queue_create("Theft4 completed CPU records",attributes);
#endif
  }
  bool available() const {
#if defined(__APPLE__)
    return group_ && queue_;
#else
    return false;
#endif
  }
  void Poll() {
#if defined(__APPLE__)
    if (!pending_ || dispatch_group_wait(group_,DISPATCH_TIME_NOW) != 0) return;
    // The group wait establishes callback completion and publication; result
    // fields are never read while the utility callback can still write them.
    pending_ = false;
    ++completed_; commands_ += job_commands_; cpu_ns_ += job_cpu_ns_; wall_ns_ += job_wall_ns_;
    retained_metadata_bytes_ = 0;
#endif
  }
  void Wait() {
#if defined(__APPLE__)
    if (pending_) { dispatch_group_wait(group_,DISPATCH_TIME_FOREVER); Poll(); }
#endif
  }
  bool TryStart(std::vector<Owner>& records, size_t metadata_bytes, Recycler& recycler) {
    Poll();
    if (!available() || records.empty()) return false;
    if (pending_) { ++busy_fallbacks_; return false; }
    // Bounds direct command metadata and owner count. Immutable resources can
    // be shared with caches/current work: this is not a total resource-byte cap.
    if (records.size() > 8192 || metadata_bytes > 67108864) { ++budget_fallbacks_; return false; }
#if defined(__APPLE__)
    records_.swap(records);
    recycler_ = &recycler; job_commands_ = records_.size();
    retained_metadata_bytes_ = metadata_bytes;
    high_water_metadata_bytes_ = std::max(high_water_metadata_bytes_,uint64_t(metadata_bytes));
    ++started_; pending_ = true;
    dispatch_group_async_f(group_,queue_,this,[](void* raw) {
      auto& self = *static_cast<NativeDeferredCleanup*>(raw);
      const auto wall_begin = std::chrono::steady_clock::now();
      timespec begin{},end{};
      const bool valid_cpu = clock_gettime(CLOCK_THREAD_CPUTIME_ID,&begin)==0;
      self.recycler_->RecycleExternalBatch(self.records_);
      self.job_cpu_ns_ = 0;
      if (valid_cpu && clock_gettime(CLOCK_THREAD_CPUTIME_ID,&end)==0) {
        const int64_t duration = (int64_t(end.tv_sec)-begin.tv_sec)*1000000000ll+end.tv_nsec-begin.tv_nsec;
        if (duration>0) self.job_cpu_ns_ = uint64_t(duration);
      }
      self.job_wall_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now()-wall_begin).count();
    });
    return true;
#else
    return false;
#endif
  }
  uint64_t started() const { return started_; }
  uint64_t completed() const { return completed_; }
  uint64_t commands() const { return commands_; }
  uint64_t cpu_ns() const { return cpu_ns_; }
  uint64_t wall_ns() const { return wall_ns_; }
  uint64_t retained_metadata_bytes() const { return retained_metadata_bytes_; }
  uint64_t high_water_metadata_bytes() const { return high_water_metadata_bytes_; }
  uint64_t busy_fallbacks() const { return busy_fallbacks_; }
  uint64_t budget_fallbacks() const { return budget_fallbacks_; }
 private:
  Recycler* recycler_ = nullptr;
  std::vector<Owner> records_;
  bool pending_ = false;
  uint64_t job_commands_ = 0,job_cpu_ns_ = 0,job_wall_ns_ = 0;
  uint64_t started_ = 0,completed_ = 0,commands_ = 0,cpu_ns_ = 0,wall_ns_ = 0;
  uint64_t retained_metadata_bytes_ = 0,high_water_metadata_bytes_ = 0;
  uint64_t busy_fallbacks_ = 0,budget_fallbacks_ = 0;
#if defined(__APPLE__)
  dispatch_group_t group_ = nullptr;
  dispatch_queue_t queue_ = nullptr;
#endif
};
} // namespace rex::graphics::gta4_native
