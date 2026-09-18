#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>

namespace rex::graphics::gta4_native {

template <size_t InlineCapacity>
class NativeInlineBytes {
 public:
  NativeInlineBytes() = default;
  NativeInlineBytes(const NativeInlineBytes& other) { CopyFrom(other); }
  NativeInlineBytes& operator=(const NativeInlineBytes& other) {
    if (this != &other) {
      resize(other.size_);
      if (size_) std::memcpy(data(), other.data(), size_);
    }
    return *this;
  }
  NativeInlineBytes(NativeInlineBytes&& other) noexcept { MoveFrom(std::move(other)); }
  NativeInlineBytes& operator=(NativeInlineBytes&& other) noexcept {
    if (this != &other) {
      overflow_.reset();
      size_ = 0;
      overflow_capacity_ = 0;
      MoveFrom(std::move(other));
    }
    return *this;
  }

  void resize(size_t new_size) {
    if (new_size <= InlineCapacity) {
      if (overflow_) {
        const size_t preserved = std::min(size_, new_size);
        std::memcpy(inline_.data(), overflow_.get(), preserved);
        overflow_.reset();
        overflow_capacity_ = 0;
      }
      size_ = new_size;
      return;
    }
    if (!overflow_ || overflow_capacity_ < new_size) {
      auto replacement = std::make_unique<uint8_t[]>(new_size);
      const size_t preserved = std::min(size_, new_size);
      if (preserved) std::memcpy(replacement.get(), data(), preserved);
      overflow_ = std::move(replacement);
      overflow_capacity_ = new_size;
    }
    size_ = new_size;
  }

  uint8_t* data() { return overflow_ ? overflow_.get() : inline_.data(); }
  const uint8_t* data() const { return overflow_ ? overflow_.get() : inline_.data(); }
  size_t size() const { return size_; }
  size_t capacity() const { return overflow_ ? overflow_capacity_ : InlineCapacity; }
  size_t heap_capacity() const { return overflow_ ? overflow_capacity_ : 0; }
  bool uses_inline_storage() const { return !overflow_; }
  operator std::span<uint8_t>() { return {data(), size_}; }
  operator std::span<const uint8_t>() const { return {data(), size_}; }

 private:
  void CopyFrom(const NativeInlineBytes& other) {
    resize(other.size_);
    if (size_) std::memcpy(data(), other.data(), size_);
  }
  void MoveFrom(NativeInlineBytes&& other) {
    size_ = other.size_;
    if (other.overflow_) {
      overflow_ = std::move(other.overflow_);
      overflow_capacity_ = other.overflow_capacity_;
    } else if (size_) {
      std::memcpy(inline_.data(), other.inline_.data(), size_);
    }
    other.size_ = 0;
    other.overflow_capacity_ = 0;
  }

  std::unique_ptr<uint8_t[]> overflow_;
  size_t size_ = 0;
  size_t overflow_capacity_ = 0;
  std::array<uint8_t, InlineCapacity> inline_{};
};

}  // namespace rex::graphics::gta4_native
