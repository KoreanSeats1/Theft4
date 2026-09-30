#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace rex::graphics::gta4_native {

// Renderer-owned successful lookup receipts. The selector only chooses a set;
// exact logical equality remains mandatory. No allocations or shared locks.
template <typename Key, typename Value, size_t Sets = 32, size_t Ways = 4>
class NativePipelineRequestCache {
 public:
  const Value* Find(const Key& key, uint64_t selector) const {
    const auto& set = entries_[selector % Sets];
    for (const auto& entry : set)
      if (entry.valid && entry.key == key) return &entry.value;
    return nullptr;
  }
  void Store(const Key& key, uint64_t selector, const Value& value) {
    const size_t index = selector % Sets;
    auto& set = entries_[index];
    for (auto& entry : set) {
      if (!entry.valid || entry.key == key) {
        entry = {key, value, true};
        return;
      }
    }
    set[next_[index]] = {key, value, true};
    next_[index] = (next_[index] + 1) % Ways;
  }
 private:
  struct Entry { Key key{}; Value value{}; bool valid = false; };
  std::array<std::array<Entry, Ways>, Sets> entries_{};
  std::array<size_t, Sets> next_{};
};
}  // namespace rex::graphics::gta4_native
