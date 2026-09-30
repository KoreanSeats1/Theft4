#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <dispatch/dispatch.h>
#include <sys/sysctl.h>
#endif

namespace rex::graphics::gta4_native {

// Each owner admits at most one helper. GCD owns the threads and chooses cores
// on every SoC; callers impose stage-specific work and CPU-capacity limits.
// No model identifiers, affinity masks, spinning, or per-draw dispatches.
class NativePreparationTask {
 public:
  using Function = void (*)(void*);
  NativePreparationTask() = default;
  void Initialize(bool requested = Requested(),
                  const char* name = "Theft4 constant preparation", uint32_t minimum_cpus = 4) {
    if (initialized_) return;
    initialized_ = true;
#if defined(__APPLE__)
    int physical = 0, active = 0;
    size_t size = sizeof(physical);
    if (sysctlbyname("hw.physicalcpu", &physical, &size, nullptr, 0)) physical = 0;
    size = sizeof(active);
    if (sysctlbyname("hw.activecpu", &active, &size, nullptr, 0)) active = physical;
    available_cpus_ = uint32_t(std::max(0, std::min(physical, active)));
    // Leave capacity for the guest producer, render worker and audio/runtime.
    // Unknown topology and small systems retain the original serial path.
    if (available_cpus_ < minimum_cpus || !requested) return;
    group_ = dispatch_group_create();
    auto attributes = dispatch_queue_attr_make_with_qos_class(
        DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0);
    queue_ = dispatch_queue_create(name, attributes);
#endif
  }
  ~NativePreparationTask() {
    Wait();
#if defined(__APPLE__) && !OS_OBJECT_USE_OBJC
    if (queue_) dispatch_release(queue_);
    if (group_) dispatch_release(group_);
#endif
  }
  NativePreparationTask(const NativePreparationTask&) = delete;
  NativePreparationTask& operator=(const NativePreparationTask&) = delete;
  static bool Requested() {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    // Called when the render worker starts after the launcher applies env.
    const char* value = std::getenv("THEFT4_PARALLEL_PREPARATION");
    return !value || std::strcmp(value, "0") != 0;
#else
    return false;
#endif
  }
  uint32_t available_cpus() const { return available_cpus_; }
  bool available() const {
#if defined(__APPLE__)
    return group_ && queue_;
#else
    return false;
#endif
  }
  bool Start(void* context, Function function) {
#if defined(__APPLE__)
    if (!available() || pending_) return false;
    context_ = context;
    function_ = function;
    pending_ = true;
    dispatch_group_async_f(group_, queue_, this, [](void* raw) {
      auto& task = *static_cast<NativePreparationTask*>(raw);
      task.function_(task.context_);
    });
    return true;
#else
    return false;
#endif
  }
  bool pending() const { return pending_; }
  void Wait() {
#if defined(__APPLE__)
    if (pending_) {
      dispatch_group_wait(group_, DISPATCH_TIME_FOREVER);
      pending_ = false;
    }
#endif
  }
 private:
  uint32_t available_cpus_ = 0;
  bool pending_ = false;
  bool initialized_ = false;
  void* context_ = nullptr;
  Function function_ = nullptr;
#if defined(__APPLE__)
  dispatch_group_t group_ = nullptr;
  dispatch_queue_t queue_ = nullptr;
#endif
};
}  // namespace rex::graphics::gta4_native
