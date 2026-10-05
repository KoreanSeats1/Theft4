#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include "frame_constant_arena.h"
#include "native_constant_projection.h"

namespace rex::graphics::gta4_native {

// Addresses still use the shipped full-bank ABI. Only registers proved read
// by the final executing shader are initialized. This allocation must NEVER
// enter a full-bank version/content cache or satisfy a different coverage mask.
inline size_t NativeMaskedConstantExtent(const NativeConstantMask& mask) {
  for (size_t i = mask.size(); i-- > 0;)
    if (mask[i]) return (i * 64 + 64 - std::countl_zero(mask[i])) * 16;
  return 0;
}

inline bool NativeConstantMaskContains(const NativeConstantMask& covering,
                                      const NativeConstantMask& required) {
  for (size_t i = 0; i < covering.size(); ++i)
    if ((covering[i] & required[i]) != required[i]) return false;
  return true;
}

// Collect every consumer before preparation. A version gets at most one
// allocation, covering the union of ALL final shaders that read it this frame.
// One unknown consumer disables partial uploads for that entire version.
// Commands own the immutable version keys; the submission fence owns storage.
template <typename Allocation>
class NativeFrameConstantCoverage {
 public:
  struct Entry {
    NativeConstantMask mask{};
    Allocation allocation{};
    bool known = false, prepared = false, fallback = false;
  };
  bool Observe(const ConstantStateVersion* version, const NativeConstantMask& mask, bool known) {
    if (!version) return false;
    auto result = entries_.Insert(version, Entry{mask, {}, known});
    if (!result) return false;
    if (!result.inserted) {
      if (result.value->prepared || result.value->fallback) return false;
      result.value->known &= known;
      for (size_t i = 0; i < mask.size(); ++i) result.value->mask[i] |= mask[i];
    }
    return true;
  }
  Entry* Find(const ConstantStateVersion* version) { return entries_.Find(version); }
  bool CanReset() const { return entries_.CanResetGeneration(); }
  bool Reset() { return entries_.ResetGeneration(); }
  size_t size() const { return entries_.size(); }
 private:
  FrameGenerationMap<const ConstantStateVersion*, Entry> entries_;
};

template <typename F>
inline void ForNativeConstantRegisters(const NativeConstantMask& mask, F&& visit) {
  for (size_t i = 0; i < mask.size(); ++i) {
    uint64_t bits = mask[i];
    while (bits) {
      const size_t reg = i * 64 + std::countr_zero(bits);
      bits &= bits - 1;
      visit(reg);
    }
  }
}

template <typename F>
inline void ForNativeConstantRuns(const NativeConstantMask& mask, F&& visit) {
  size_t first = 0, count = 0;
  for (size_t word = 0; word < mask.size(); ++word) {
    uint64_t bits = mask[word];
    while (bits) {
      const size_t offset = std::countr_zero(bits);
      const size_t length = std::countr_one(bits >> offset);
      const size_t reg = word * 64 + offset;
      if (count && reg != first + count) { visit(first, count); count = 0; }
      if (!count) first = reg;
      count += length;
      if (offset + length == 64) bits = 0;
      else bits &= ~(((uint64_t{1} << length) - 1) << offset);
    }
  }
  if (count) visit(first, count);
}

// A bounded, non-owning replay plan. Its caller retains the current version
// and previous covered allocation throughout Write. Build/Write are owned by
// the same constant-preparation worker; they never mutate version ancestry or
// materialization. Unknown/unaligned/malformed input falls back before writing.
class NativeMaskedConstantPlan {
 public:
  bool Build(const ConstantStateVersion* current, const NativeConstantMask& mask,
             const ConstantStateVersion* previous = nullptr,
             std::span<const uint8_t> previous_host = {}) {
    *this = {};
    mask_ = mask;
    extent_ = NativeMaskedConstantExtent(mask);
    if (!current || !extent_ || extent_ > current->byte_size || current->byte_size > 4096)
      return false;
    const size_t bank_size = current->byte_size;
    for (const auto* cursor = current; cursor; cursor = cursor->parent.get()) {
      if (cursor->byte_size != bank_size) return false;
      if (cursor == previous && previous_host.size() >= extent_) {
        base_ = previous_host;
        base_is_host_ = true;
        return true;
      }
      if (cursor->materialized) {
        if (cursor->materialized->size() != bank_size) return false;
        base_ = *cursor->materialized;
        return true;
      }
      if (!ValidateConstantPayloadDelta(cursor->delta, bank_size)) return false;
      for (const auto& range : cursor->delta.ranges)
        if ((range.destination_offset | range.payload_offset | range.byte_count) & 3)
          return false;
      if (cursor->delta.complete_snapshot) {
        base_ = cursor->delta.payload;
        return true;
      }
      if (count_ == nodes_.size()) return false;
      nodes_[count_++] = cursor;
    }
    return false;
  }

  size_t extent() const { return extent_; }
  size_t ancestors() const { return count_; }

  template <typename CopyGuest>
  uint64_t Write(std::span<uint8_t> output, CopyGuest&& copy_guest) const {
    if (output.size() < extent_ || base_.size() < extent_) return 0;
    uint64_t written = 0;
    ForNativeConstantRuns(mask_, [&](size_t reg, size_t count) {
      const size_t offset = reg * 16, bytes = count * 16;
      if (base_is_host_) std::memcpy(output.data() + offset, base_.data() + offset, bytes);
      else copy_guest(output.data() + offset, base_.data() + offset, bytes);
      written += bytes;
    });
    for (size_t i = count_; i-- > 0;) {
      const auto& delta = nodes_[i]->delta;
      for (const auto& range : delta.ranges) {
        const size_t begin = range.destination_offset;
        const size_t end = begin + range.byte_count;
        // Float4-register masks bound all reads, including scalar/vector loads.
        // Replay supports any word-aligned range and overlapping successive
        // versions in the original order, without copying unread holes.
        // Coalesce consecutive covered registers. Broad updates used to call
        // the endian converter once per float4, defeating its vectorized loop.
        NativeConstantMask clipped{};
        const size_t first_reg=begin/16,last_reg=(end+15)/16;
        for(size_t word=first_reg/64;word<(last_reg+63)/64;++word) {
          const size_t first=std::max(first_reg,word*64)-word*64;
          const size_t last=std::min(last_reg,(word+1)*64)-word*64;
          clipped[word]=mask_[word]&(~uint64_t(0)<<first)&
              (last==64?~uint64_t(0):(uint64_t(1)<<last)-1);
        }
        ForNativeConstantRuns(clipped,[&](size_t reg,size_t count) {
          const size_t first = std::max(begin, reg * 16);
          const size_t last = std::min(end, (reg + count) * 16);
          copy_guest(output.data() + first,
              delta.payload.data() + range.payload_offset + first - begin, last - first);
          written += last - first;
        });
      }
    }
    return written;
  }

 private:
  NativeConstantMask mask_{};
  // Searching an old family through a long journal is more expensive than a
  // complete-bank binding. Keep sparse replay bounded; that binding can also
  // become the next covered base without weakening the coverage contract.
  std::array<const ConstantStateVersion*, 8> nodes_{};
  std::span<const uint8_t> base_{};
  size_t extent_ = 0, count_ = 0;
  bool base_is_host_ = false;
};

}  // namespace rex::graphics::gta4_native
