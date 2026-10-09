#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>

namespace rex::graphics::gta4_native {
// One ticket per cached handle, even if its generation changes repeatedly.
// Numeric continuation survives replacement, erasure and rehash of the source
// map. The caller holds its source-cache mutex for Track/Erase/Sweep/Clear.
// Unlike a hash-bucket sweep, colliding keys cannot exceed the visit allowance.
class NativeGeometryRetirement {
 public:
  void Track(uint32_t handle) { handles_.insert(handle); }
  void Erase(uint32_t handle) { handles_.erase(handle); }
  void Clear() { handles_.clear(); cursor_.reset(); }
  size_t Size() const { return handles_.size(); }
  template<class Retire>
  size_t Sweep(size_t budget, Retire retire) {
    const auto count = std::min(budget, handles_.size());
    size_t removed = 0;
    for (size_t visited = 0; visited < count && !handles_.empty(); ++visited) {
      auto next = cursor_ ? handles_.upper_bound(*cursor_) : handles_.begin();
      if (next == handles_.end()) next = handles_.begin();
      const auto handle = *next;
      cursor_ = handle;
      if (retire(handle)) { handles_.erase(next); ++removed; }
    }
    if (handles_.empty()) cursor_.reset();
    return removed;
  }
 private:
  std::set<uint32_t> handles_;
  std::optional<uint32_t> cursor_;
};
}  // namespace rex::graphics::gta4_native
