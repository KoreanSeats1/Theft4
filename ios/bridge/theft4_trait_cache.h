#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace theft4 {
// Fixed-size CPU metadata only. Values must retain the object identified by
// their key, so pointer recycling cannot turn a stale trait into a hit.
// Collision replacement never alters the object or any encoded GPU owner.
template<class Value,size_t Slots=64,size_t Ways=4> class TraitCache {
  static_assert(Ways&&Ways<=Slots&&Ways<=256&&std::has_single_bit(Slots));
  struct Slot {const void* key=nullptr;Value value{};};
  std::array<Slot,Slots> slots_{};
  std::array<uint8_t,Slots> next_{};
 public:
  static size_t SetFor(const void* key) {
    return (reinterpret_cast<uintptr_t>(key)>>4)&(Slots-1);
  }
  // Non-null keys only; callers perform their ordinary nil/range validation.
  // On a miss the caller must replace every trait and its strong owner before
  // another lookup. Direct replacement avoids clearing then assigning ARC
  // ownership twice; the previous owner survives until that replacement.
  Value& Lookup(const void* key,bool& hit) {
    const auto begin=SetFor(key);auto& first=slots_[begin];
    // Preserve the preceding direct-cache fast path for primary hits. Only
    // actual collisions pay for the remaining bounded probes.
    if(first.key==key){hit=true;return first.value;}
    size_t empty=first.key?Ways:0;
    for(size_t way=1;way<Ways;++way) {
      auto& slot=slots_[(begin+way)&(Slots-1)];
      if(slot.key==key){hit=true;return slot.value;}
      if(!slot.key&&empty==Ways)empty=way;
    }
    const auto way=empty!=Ways?empty:size_t(next_[begin]++%Ways);
    auto& slot=slots_[(begin+way)&(Slots-1)];slot.key=key;hit=false;return slot.value;
  }
};
}
