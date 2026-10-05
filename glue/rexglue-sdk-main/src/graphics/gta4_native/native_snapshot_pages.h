#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace rex::graphics::gta4_native {

// One render-worker writer, immutable published objects, arbitrary retiring
// threads. Aliasing owners keep the complete page alive, including snapshots
// retained by persistent tail state or a partial flush. No storage is recycled
// while a reader owns any snapshot. The page destructor destroys every object
// exactly once on its final owner's thread; there is no per-object pool lock.
template <typename T, size_t ObjectsPerPage = 64>
class NativeSnapshotPages {
  static_assert(ObjectsPerPage > 0);
  struct Statistics { std::atomic<size_t> live_pages{0}; };
  struct Page {
    explicit Page(std::shared_ptr<Statistics> s) : stats(std::move(s)) {
      if(stats)stats->live_pages.fetch_add(1, std::memory_order_relaxed);
    }
    ~Page() {
      for (size_t i = 0; i < used; ++i) std::destroy_at(Object(i));
      if(stats)stats->live_pages.fetch_sub(1, std::memory_order_relaxed);
    }
    T* Object(size_t i) { return reinterpret_cast<T*>(storage + sizeof(T) * i); }
    std::shared_ptr<Statistics> stats;
    size_t used = 0;
    // Deliberately uninitialized: each constructed object initializes its own
    // fields. Zeroing the complete page would repeat that work.
    alignas(T) std::byte storage[sizeof(T) * ObjectsPerPage];
  };
 public:
  NativeSnapshotPages() = default;
  // Set before creating snapshots; retiring pages retain their original policy.
  void InitializeDiagnostics(bool enabled) {diagnostics_=enabled;}
  NativeSnapshotPages(const NativeSnapshotPages&) = delete;
  NativeSnapshotPages& operator=(const NativeSnapshotPages&) = delete;
  template <typename... Args> std::shared_ptr<T> Create(Args&&... args) {
    if (!active_ || active_->used == ObjectsPerPage) {
      if(diagnostics_&&!stats_)stats_=std::make_shared<Statistics>();
      active_ = std::make_shared<Page>(diagnostics_?stats_:nullptr);
      if(diagnostics_)++pages_created_;
    }
    auto* object = std::construct_at(active_->Object(active_->used), std::forward<Args>(args)...);
    ++active_->used;
    if(diagnostics_)++objects_created_;
    return std::shared_ptr<T>(active_, object);
  }
  // Drop only the builder's owner; published snapshots retain their page.
  void ReleaseActive() { active_.reset(); }
  uint64_t pages_created() const { return pages_created_; }
  uint64_t objects_created() const { return objects_created_; }
  size_t live_bytes() const {
    return stats_?stats_->live_pages.load(std::memory_order_relaxed)*sizeof(Page):0;
  }
 private:
  bool diagnostics_=true;
  std::shared_ptr<Statistics> stats_;
  std::shared_ptr<Page> active_;
  uint64_t pages_created_ = 0, objects_created_ = 0;
};
}  // namespace rex::graphics::gta4_native
