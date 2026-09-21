#pragma once
#include <cstddef>
#include <cstdint>
#ifdef THEFT4_LAB_BUILD
#include <memory_resource>
#endif
#include <unordered_map>
#include <unordered_set>

namespace rex::graphics::gta4_native {

// Queue lock owns this index. Counts are logical references, not shared_ptrs or
// GPU ownership. Every zero count is removed immediately. If an invariant is
// violated the renderer falls back to its original queue scan, never guessing
// that an image is safe to retire.
class NativeTextureProtectionIndex {
 public:
#ifdef THEFT4_LAB_BUILD
  // The queue index is serialized by render_mutex_; the batch index is worker
  // owned. Neither needs a synchronized allocator. Recycle map nodes without
  // retaining zero-count generations or changing the exact scan fallback.
  explicit NativeTextureProtectionIndex(
      std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
      : pool_(std::pmr::pool_options{64, 128}, upstream), counts_(&pool_) {}
#endif
  bool Retain(uint64_t generation) {
    if (!generation) return true;
    auto& count = counts_[generation];
    if (count == UINT64_MAX) { valid_ = false; return false; }
    ++count; return true;
  }
  bool Release(uint64_t generation) {
    if (!generation) return true;
    const auto it = counts_.find(generation);
    if (it == counts_.end() || !it->second) { valid_ = false; return false; }
    if (!--it->second) counts_.erase(it);
    return true;
  }
  void AppendTo(std::unordered_set<uint64_t>& destination) const {
    destination.reserve(destination.size() + counts_.size());
    for (const auto& [generation, count] : counts_) if (count) destination.insert(generation);
  }
  // Append the logical difference without changing either index. This lets a
  // worker keep staged references in the mutex-owned queue index until the
  // batch completes, while exact protection queries omit those deferred refs.
  bool AppendToExcluding(const NativeTextureProtectionIndex& excluded,
                         std::unordered_set<uint64_t>& destination) const {
    if (!valid_ || !excluded.valid_) return false;
    for (const auto& [generation, count] : excluded.counts_) {
      const auto it = counts_.find(generation);
      if (it == counts_.end() || it->second < count) return false;
    }
    destination.reserve(destination.size() + counts_.size());
    for (const auto& [generation, count] : counts_) {
      const auto excluded_it = excluded.counts_.find(generation);
      const uint64_t excluded_count =
          excluded_it == excluded.counts_.end() ? 0 : excluded_it->second;
      if (count > excluded_count) destination.insert(generation);
    }
    return true;
  }
  // Copy logical counts using this index's allocator. Used outside the queue
  // critical section to retain an immutable batch reconciliation snapshot.
  bool AssignFrom(const NativeTextureProtectionIndex& source) {
    if (this == &source) return valid_;
    Reset();
    if (!source.valid_) { valid_ = false; return false; }
    counts_.reserve(source.counts_.size());
    for (const auto& [generation, count] : source.counts_)
      counts_.emplace(generation, count);
    return true;
  }
  // Subtract a complete snapshot in two passes so an invariant failure cannot
  // partially update the destination. The caller may rebuild from its queue.
  bool ReleaseAllFrom(const NativeTextureProtectionIndex& source) {
    if (!valid_ || !source.valid_) { valid_ = false; return false; }
    for (const auto& [generation, count] : source.counts_) {
      const auto it = counts_.find(generation);
      if (it == counts_.end() || it->second < count) {
        valid_ = false;
        return false;
      }
    }
    for (const auto& [generation, count] : source.counts_) {
      const auto it = counts_.find(generation);
      it->second -= count;
      if (!it->second) counts_.erase(it);
    }
    return true;
  }
  bool Contains(uint64_t generation) const { return counts_.contains(generation); }
  size_t size() const { return counts_.size(); }
  bool valid() const { return valid_; }
  void Reset() { counts_.clear(); valid_ = true; }
 private:
#ifdef THEFT4_LAB_BUILD
  // Destruction order matters: the map returns nodes before its pool dies.
  // Pool capacity follows peak live allocation demand, not historical IDs.
  std::pmr::unsynchronized_pool_resource pool_;
  std::pmr::unordered_map<uint64_t, uint64_t> counts_;
#else
  std::unordered_map<uint64_t, uint64_t> counts_;
#endif
  bool valid_ = true;
};
}  // namespace rex::graphics::gta4_native
