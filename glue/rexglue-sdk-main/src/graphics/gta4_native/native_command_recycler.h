#pragma once

#include <algorithm>
#include <cassert>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

namespace rex::graphics::gta4_native {

// One producer (under command_capture_mutex_) and one render worker own their
// local lists. Optional utility cleanup publishes private batches only through
// the locked shared exchange. Slots contain destroyed raw storage or fully
// reset commands with bounded empty payload capacity. No per-command locking.
template <typename Command, size_t Batch = 128, size_t SharedLimit = 1024>
class NativeCommandRecycler {
  static_assert(Batch > 0 && SharedLimit >= Batch);
  struct StorageDelete {
    bool constructed = false;
    void operator()(Command* pointer) const noexcept {
      if (constructed) std::destroy_at(pointer);
      if constexpr (alignof(Command) > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        ::operator delete(pointer, std::align_val_t(alignof(Command)));
      } else {
        ::operator delete(pointer);
      }
    }
  };
  // Slots can own reset live commands when payload reuse is enabled. A dead
  // slot retains only the outer allocation, matching the original path.
  using Storage = std::unique_ptr<Command, StorageDelete>;
 public:
  using Owner = std::unique_ptr<Command>;
  NativeCommandRecycler() {
    shared_free_.reserve(SharedLimit);
    producer_free_.reserve(Batch);
    worker_free_.reserve(Batch);
  }

  void InitializePayloadReuse(bool enabled,bool diagnostics=true) { retain_payloads_ = enabled;diagnostics_=diagnostics; }
  uint64_t RetainedAcquires() const { return retained_acquires_.load(std::memory_order_relaxed); }
  Owner Acquire(bool* reused = nullptr) {
    if (producer_free_.empty()) {
      std::lock_guard lock(mutex_);
      const size_t count = std::min(Batch, shared_free_.size());
      for (size_t i = 0; i < count; ++i) {
        producer_free_.push_back(std::move(shared_free_.back()));
        shared_free_.pop_back();
      }
    }
    const bool hit = !producer_free_.empty();
    if (reused) *reused = hit;
    if (!hit) return std::make_unique<Command>();
    Storage storage = std::move(producer_free_.back());
    producer_free_.pop_back();
    Command* command = storage.get();
    if (storage.get_deleter().constructed) {if(diagnostics_)retained_acquires_.fetch_add(1,std::memory_order_relaxed);}
    else std::construct_at(command);
    storage.release();
    return Owner(command);
  }

  void Recycle(Owner command) {
    assert(command);
    // Release all resource owners now. Enabled reuse retains only bounded
    // empty payload capacity; the legacy path retains raw outer storage.
    worker_free_.push_back(ResetOrDestroy(std::move(command)));
    if (worker_free_.size() == Batch) FlushWorker();
  }

  // Utility producer touches only its private batch and mutex-owned shared
  // exchange. Never calls Recycle or mutates the render worker's local list.
  void RecycleExternalBatch(std::vector<Owner>& commands) {
    std::vector<Storage> batch;
    batch.reserve(Batch);
    const auto publish = [&] {
      {
        std::lock_guard lock(mutex_);
        for (auto& slot : batch)
          if (shared_free_.size() < SharedLimit) shared_free_.push_back(std::move(slot));
        if(diagnostics_)max_shared_ = std::max(max_shared_,shared_free_.size());
      }
      batch.clear(); // excess slots are destroyed outside the exchange lock
    };
    for (auto& command : commands) {
      batch.push_back(ResetOrDestroy(std::move(command)));
      if (batch.size() == Batch) publish();
    }
    publish();
    commands.clear();
  }

  void FlushWorker() {
    {
      std::lock_guard lock(mutex_);
      while (!worker_free_.empty() && shared_free_.size() < SharedLimit) {
        shared_free_.push_back(std::move(worker_free_.back()));
        worker_free_.pop_back();
      }
      if(diagnostics_)max_shared_ = std::max(max_shared_, shared_free_.size());
    }
    worker_free_.clear(); // release excess payload capacity outside exchange lock
  }

  size_t SharedSize() const {
    std::lock_guard lock(mutex_);
    return shared_free_.size();
  }
  size_t SharedHighWater() const {
    std::lock_guard lock(mutex_);
    return max_shared_;
  }

 private:
  Storage ResetOrDestroy(Owner command) {
    Command* pointer = command.release();
    if constexpr (requires(Command& value) { value.ResetForReuse(); }) {
      if (retain_payloads_) { pointer->ResetForReuse(); return Storage(pointer,StorageDelete{true}); }
    }
    std::destroy_at(pointer);
    return Storage(pointer,StorageDelete{false});
  }
  bool retain_payloads_ = false; // frozen before producer/worker creation
  bool diagnostics_=true;
  std::atomic<uint64_t> retained_acquires_{0};
  mutable std::mutex mutex_;
  std::vector<Storage> shared_free_;
  std::vector<Storage> producer_free_;
  std::vector<Storage> worker_free_;
  size_t max_shared_ = 0;
};

}  // namespace rex::graphics::gta4_native
