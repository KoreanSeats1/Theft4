#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace rex::graphics::gta4_native {

// Reverse lookup for immutable captures of overlapping guest buffer storage.
// Normalized addresses have a 32-bit offset and one domain bit at bit 32:
// virtual and physical ranges never alias merely because their offsets match.
// The caller supplies physical normalization and owns the buffer-resource lock.
// Callbacks must not mutate or recursively query this index.
class NativeBufferAliasIndex {
 public:
  static constexpr uint64_t kPhysicalDomain = uint64_t{1} << 32;
  static constexpr uint64_t kPageSize = uint64_t{1} << 16;

  void Clear() {
    pages_.clear();
    records_.clear();
    query_stamp_ = 0;
  }

  bool Erase(uint32_t handle) {
    const auto found = records_.find(handle);
    if (found == records_.end()) return false;
    const Record& record = found->second;
    for (uint64_t page = record.start >> 16; page <= (record.end - 1) >> 16; ++page) {
      const auto bucket = pages_.find(page);
      if (bucket == pages_.end()) continue;
      auto& handles = bucket->second;
      const auto member = std::find(handles.begin(), handles.end(), handle);
      if (member != handles.end()) {
        *member = handles.back();
        handles.pop_back();
      }
      if (handles.empty()) pages_.erase(bucket);
    }
    records_.erase(found);
    return true;
  }

  // Invalid replacement ranges remove any old registration and return false.
  // The caller must conservatively handle resources it cannot index.
  bool Insert(uint32_t handle, uint64_t normalized_start, uint32_t size) {
    uint64_t end = 0;
    if (!handle || !RangeEnd(normalized_start, size, end)) {
      Erase(handle);
      return false;
    }
    const auto existing = records_.find(handle);
    if (existing != records_.end() && existing->second.start == normalized_start &&
        existing->second.end == end) {
      return true;
    }
    Erase(handle);
    records_.emplace(handle, Record{normalized_start, end, 0});
    for (uint64_t page = normalized_start >> 16; page <= (end - 1) >> 16; ++page)
      pages_[page].push_back(handle);
    return true;
  }

  // Returns false for an invalid range, so the caller can use its conservative
  // fallback. A valid query calls callback(handle) once per exact overlap.
  // Query stamps deduplicate captures spanning several pages without a scratch
  // set or any allocation. Stamp wrap requires a rare allocation-free reset.
  template <typename Callback>
  bool ForEachOverlap(uint64_t normalized_start, uint32_t size, Callback&& callback) {
    uint64_t end = 0;
    if (!RangeEnd(normalized_start, size, end)) return false;
    if (query_stamp_ == std::numeric_limits<uint64_t>::max()) {
      for (auto& [handle, record] : records_) record.query_stamp = 0;
      query_stamp_ = 0;
    }
    const uint64_t stamp = ++query_stamp_;
    for (uint64_t page = normalized_start >> 16; page <= (end - 1) >> 16; ++page) {
      const auto bucket = pages_.find(page);
      if (bucket == pages_.end()) continue;
      for (uint32_t handle : bucket->second) {
        const auto found = records_.find(handle);
        if (found == records_.end()) continue;
        Record& record = found->second;
        if (record.query_stamp == stamp) continue;
        record.query_stamp = stamp;
        if (record.start < end && normalized_start < record.end) callback(handle);
      }
    }
    return true;
  }

  size_t size() const { return records_.size(); }

 private:
  struct Record {
    uint64_t start;
    uint64_t end;
    uint64_t query_stamp;
  };

  static bool RangeEnd(uint64_t start, uint32_t size, uint64_t& end) {
    // Reject bits above the single domain bit and reject a range crossing out
    // of its domain. An exclusive end exactly at the domain boundary is valid.
    if (!size || start >= (kPhysicalDomain << 1)) return false;
    const uint64_t domain_end = (start & kPhysicalDomain) + kPhysicalDomain;
    if (uint64_t(size) > domain_end - start) return false;
    end = start + size;
    return true;
  }

  std::unordered_map<uint32_t, Record> records_;
  std::unordered_map<uint64_t, std::vector<uint32_t>> pages_;
  uint64_t query_stamp_ = 0;
};

}  // namespace rex::graphics::gta4_native
