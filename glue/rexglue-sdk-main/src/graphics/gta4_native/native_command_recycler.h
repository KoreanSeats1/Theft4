#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace rex::graphics::gta4_native {

// One producer (under command_capture_mutex_) and one render worker. Commands
// release their resources on the worker before storage is published for reuse.
// Neither thread takes the exchange mutex for each command.
template <typename Command, size_t Batch = 128, size_t SharedLimit = 1024>
class NativeCommandRecycler {
  static_assert(Batch > 0 && SharedLimit >= Batch);
 public:
  using Owner = std::unique_ptr<Command>;
  NativeCommandRecycler() {
    shared_free_.reserve(SharedLimit);
    producer_free_.reserve(Batch);
    worker_free_.reserve(Batch);
  }

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
    Owner command = std::move(producer_free_.back());
    producer_free_.pop_back();
    return command;
  }

  void Recycle(Owner command) {
    assert(command);
    // Assignment releases all shared references, owned payloads and sync
    // state before another producer can see the storage.
    *command = Command{};
    worker_free_.push_back(std::move(command));
    if (worker_free_.size() == Batch) FlushWorker();
  }

  void FlushWorker() {
    std::lock_guard lock(mutex_);
    while (!worker_free_.empty() && shared_free_.size() < SharedLimit) {
      shared_free_.push_back(std::move(worker_free_.back()));
      worker_free_.pop_back();
    }
    max_shared_ = std::max(max_shared_, shared_free_.size());
    worker_free_.clear(); // bounded storage, including during producer stalls
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
  mutable std::mutex mutex_;
  std::vector<Owner> shared_free_;
  std::vector<Owner> producer_free_;
  std::vector<Owner> worker_free_;
  size_t max_shared_ = 0;
};

}  // namespace rex::graphics::gta4_native
