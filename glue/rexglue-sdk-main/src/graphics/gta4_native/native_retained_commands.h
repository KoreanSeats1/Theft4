#pragma once
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace rex::graphics::gta4_native {
// Same reference/index API in both modes. Owning mode retains the original
// queue command at a stable address instead of moving its 3KB object again.
template <typename Command>
class NativeRetainedCommands {
 public:
  using Owner = std::unique_ptr<Command>;
  void SetOwned(bool owned) { assert(empty()); owned_ = owned; }
  bool owned() const { return owned_; }
  bool empty() const { return size() == 0; }
  size_t size() const { return owned_ ? owners_.size() : values_.size(); }
  size_t capacity() const { return owned_ ? owners_.capacity() : values_.capacity(); }
  void reserve(size_t count) { if (owned_) owners_.reserve(count); else values_.reserve(count); }
  size_t CapacityStorageBytes() const {
    return owned_ ? owners_.capacity() * sizeof(Owner) + owners_.size() * sizeof(Command)
                  : values_.capacity() * sizeof(Command);
  }
  size_t metadata_bytes() const { return metadata_bytes_; }
  Command& operator[](size_t i) { return owned_ ? *owners_[i] : values_[i]; }
  const Command& operator[](size_t i) const { return owned_ ? *owners_[i] : values_[i]; }
  void push_back(Command&& value) { assert(!owned_); values_.push_back(std::move(value)); }
  void push_back(Owner value) {
    assert(owned_ && value);
    metadata_bytes_ += value->RecyclingMetadataBytes();
    owners_.push_back(std::move(value));
  }
  Command& front() { return (*this)[0]; }
  const Command& front() const { return (*this)[0]; }
  Command& back() { return (*this)[size()-1]; }
  const Command& back() const { return (*this)[size()-1]; }
  // The cleanup task swaps its empty capacity back, so alternating frames reuse
  // both pointer arrays. Metadata measures direct command-owned storage only.
  std::vector<Owner>& Owners() { assert(owned_); return owners_; }
  void ClearMetadata() { assert(owners_.empty()); metadata_bytes_ = 0; }
  template <typename Recycler> void RecycleOwners(Recycler& recycler) {
    assert(owned_);
    for (auto& owner : owners_) recycler.Recycle(std::move(owner));
    owners_.clear(); metadata_bytes_ = 0;
  }
  void clear() { owners_.clear(); values_.clear(); metadata_bytes_ = 0; }
  template <bool IsConst> class Iterator {
    using Container = std::conditional_t<IsConst, const NativeRetainedCommands, NativeRetainedCommands>;
   public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Command;
    using reference = std::conditional_t<IsConst, const Command&, Command&>;
    using pointer = std::conditional_t<IsConst, const Command*, Command*>;
    Iterator() = default;
    Iterator(Container* owner, size_t index) : owner_(owner), index_(index) {}
    reference operator*() const { return (*owner_)[index_]; }
    pointer operator->() const { return &(*owner_)[index_]; }
    Iterator& operator++() { ++index_; return *this; }
    Iterator operator++(int) { auto copy = *this; ++*this; return copy; }
    bool operator==(const Iterator& other) const { return owner_ == other.owner_ && index_ == other.index_; }
   private:
    Container* owner_ = nullptr; size_t index_ = 0;
  };
  auto begin() { return Iterator<false>(this,0); }
  auto end() { return Iterator<false>(this,size()); }
  auto begin() const { return Iterator<true>(this,0); }
  auto end() const { return Iterator<true>(this,size()); }
 private:
  bool owned_ = false;
  size_t metadata_bytes_ = 0;
  std::vector<Owner> owners_;
  std::vector<Command> values_;
};
} // namespace rex::graphics::gta4_native
