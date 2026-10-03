#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace rex::graphics::gta4_native {

// Captured deltas are unique during transport. Diagnostic draw copies retain
// the previous deep-copy behavior; consumed draws have no capture to copy.
template <typename T>
class NativeConsumedCapture {
 public:
  NativeConsumedCapture() = default;
  NativeConsumedCapture(const NativeConsumedCapture& other)
      : value_(other.value_ ? std::make_unique<T>(*other.value_) : nullptr) {}
  NativeConsumedCapture& operator=(const NativeConsumedCapture& other) {
    if (this != &other) {
      auto replacement = other.value_ ? std::make_unique<T>(*other.value_) : nullptr;
      value_ = std::move(replacement);
    }
    return *this;
  }
  NativeConsumedCapture(NativeConsumedCapture&&) noexcept = default;
  NativeConsumedCapture& operator=(NativeConsumedCapture&&) noexcept = default;
  NativeConsumedCapture& operator=(std::unique_ptr<T> value) {
    value_ = std::move(value); return *this;
  }
  T* operator->() const { return value_.get(); }
  explicit operator bool() const { return bool(value_); }
  std::unique_ptr<T> Take() { return std::move(value_); }
 private:
  std::unique_ptr<T> value_;
};

// A record refers to an immutable owner cell. The containing command retains
// the page. Cells stay at stable addresses when its hash table grows; inserting
// other cells does not mutate an already published shared_ptr.
template <typename T>
class NativeFrameResourceRef {
 public:
  NativeFrameResourceRef() = default;
  NativeFrameResourceRef(std::nullptr_t) {}
  T* get() const {
    return cell_ ? static_cast<T*>(cell_->get()) : nullptr;
  }
  T* operator->() const { return get(); }
  T& operator*() const { return *get(); }
  explicit operator bool() const { return get() != nullptr; }
  void reset() { cell_ = nullptr; }
  // Retaining consumers (GPU caches, asynchronous jobs) acquire a real owner.
  // Ordinary record traversal reads the cell without any reference counting.
  std::shared_ptr<T> Share() const {
    return cell_ ? std::shared_ptr<T>(*cell_, get()) : std::shared_ptr<T>{};
  }
  operator std::shared_ptr<T>() const { return Share(); }
  bool operator==(const NativeFrameResourceRef& other) const { return get() == other.get(); }
  bool operator==(std::nullptr_t) const { return !get(); }
  template <typename U>
  bool operator==(const std::shared_ptr<U>& other) const { return get() == other.get(); }

 private:
  friend class NativeFrameResourceOwners;
  explicit NativeFrameResourceRef(const std::shared_ptr<const void>* cell) : cell_(cell) {}
  const std::shared_ptr<const void>* cell_ = nullptr;
};

// A synchronous lookup borrows its caller's owner. It takes a shared owner
// only when a newly created GPU/cache object actually needs to retain one.
template <typename T>
class NativeResourceView {
 public:
  template <typename U>
  NativeResourceView(const std::shared_ptr<U>& owner)
      : pointer_(owner.get()), context_(&owner), retain_([](const void* p) {
          return std::shared_ptr<const T>(*static_cast<const std::shared_ptr<U>*>(p));
        }) {}
  NativeResourceView(const NativeFrameResourceRef<const T>& owner)
      : pointer_(owner.get()), context_(&owner), retain_([](const void* p) {
          return static_cast<const NativeFrameResourceRef<const T>*>(p)->Share();
        }) {}
  const T* get() const { return pointer_; }
  const T* operator->() const { return pointer_; }
  const T& operator*() const { return *pointer_; }
  explicit operator bool() const { return pointer_ != nullptr; }
  std::shared_ptr<const T> Share() const { return retain_(context_); }
 private:
  const T* pointer_;
  const void* context_;
  std::shared_ptr<const T> (*retain_)(const void*);
};

class NativeFrameResourceOwners {
 public:
  explicit NativeFrameResourceOwners(size_t reserve = 128) { owners_.reserve(reserve); }
  NativeFrameResourceOwners(const NativeFrameResourceOwners&) = delete;
  NativeFrameResourceOwners& operator=(const NativeFrameResourceOwners&) = delete;

  template <typename T>
  NativeFrameResourceRef<T> Capture(const std::shared_ptr<T>& owner, bool* inserted = nullptr) {
    static_assert(std::is_const_v<T>, "Published resource cells expose immutable resources");
    if (!owner) {
      if (inserted) *inserted = false;
      return {};
    }
    // Keeping the first owner prevents its address from being reused by a new
    // resource generation while this page is alive. Guest handles are not keys.
    auto [entry, fresh] = owners_.try_emplace(owner.get(), owner);
    if (inserted) *inserted = fresh;
    return NativeFrameResourceRef<T>(&entry->second);
  }
  size_t size() const { return owners_.size(); }

 private:
  std::unordered_map<const void*, std::shared_ptr<const void>> owners_;
};

static_assert(sizeof(NativeFrameResourceRef<const int>) == sizeof(void*));

#ifdef THEFT4_LAB_BUILD
template <typename T> using NativeCommandResourceRef = NativeFrameResourceRef<T>;
#else
template <typename T> using NativeCommandResourceRef = std::shared_ptr<T>;
#endif
}  // namespace rex::graphics::gta4_native
