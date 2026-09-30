#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace rex::graphics::gta4_native {

// Renderer-owned successful lookup receipts. The selector only chooses a set;
// exact logical equality remains mandatory. No allocations or shared locks.
template <typename Key, typename Value, size_t Sets = 64, size_t Ways = 8>
class NativePipelineRequestCache {
 public:
  const Value* Find(const Key& key, uint64_t selector) {
    auto& set = entries_[selector % Sets];
    for (auto& entry : set) {
      if (entry.valid && entry.selector == selector && entry.key == key) {
        entry.last_use = NextUse();
        return &entry.value;
      }
    }
    return nullptr;
  }
  void Store(const Key& key, uint64_t selector, const Value& value) {
    const size_t index = selector % Sets;
    auto& set = entries_[index];
    Entry* oldest = &set[0];
    for (auto& entry : set) {
      if (!entry.valid || (entry.selector == selector && entry.key == key)) {
        entry = {key, value, selector, NextUse(), true};
        return;
      }
      if (entry.last_use < oldest->last_use) oldest = &entry;
    }
    *oldest = {key, value, selector, NextUse(), true};
  }
 private:
  struct Entry {
    Key key{};
    Value value{};
    uint64_t selector = 0, last_use = 0;
    bool valid = false;
  };
  uint64_t NextUse() {
    if (++use_ == 0) {
      for (auto& set : entries_) for (auto& entry : set) entry.last_use = 0;
      use_ = 1;
    }
    return use_;
  }
  std::array<std::array<Entry, Ways>, Sets> entries_{};
  uint64_t use_ = 0;
  static_assert(Sets && Ways);
};
}  // namespace rex::graphics::gta4_native
