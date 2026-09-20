#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace rex::graphics::gta4_native {

// A queue may store commands inline or transfer a unique owner. Consumers
// always access the same command; moving an owner never moves its payload or
// shared resource references. A queued owner must never be null.
template <typename Command>
Command& NativeQueueCommand(Command& command) { return command; }
template <typename Command>
const Command& NativeQueueCommand(const Command& command) { return command; }
template <typename Command>
Command& NativeQueueCommand(std::unique_ptr<Command>& command) {
  assert(command);
  return *command;
}
template <typename Command>
const Command& NativeQueueCommand(const std::unique_ptr<Command>& command) {
  assert(command);
  return *command;
}

// Worker-owned bounded staging. Slots are reused without allocating container
// blocks. Popping destroys the command immediately, including unmoved owners.
// The caller must cap each queue transfer to capacity(), as with the old batch.
template <typename Command, size_t Capacity>
class NativeWorkerBatch {
  static_assert(Capacity > 0);
 public:
  static constexpr size_t capacity() { return Capacity; }
  bool empty() const { return size_ == 0; }
  size_t size() const { return size_; }
  void push_back(Command&& command) {
    assert(size_ < Capacity);
    slots_[(head_ + size_) % Capacity].emplace(std::move(command));
    ++size_;
  }
  Command& front() { assert(size_); return *slots_[head_]; }
  const Command& front() const { assert(size_); return *slots_[head_]; }
  Command& back() { assert(size_); return *slots_[(head_ + size_ - 1) % Capacity]; }
  void pop_front() {
    assert(size_);
    slots_[head_].reset();
    head_ = (head_ + 1) % Capacity;
    --size_;
  }
  void clear() { while (!empty()) pop_front(); }

  class ConstIterator {
   public:
    const Command& operator*() const { return *batch_->slots_[position_ % Capacity]; }
    ConstIterator& operator++() { ++position_; return *this; }
    bool operator==(const ConstIterator&) const = default;
   private:
    friend class NativeWorkerBatch;
    ConstIterator(const NativeWorkerBatch* batch, size_t position)
        : batch_(batch), position_(position) {}
    const NativeWorkerBatch* batch_;
    size_t position_;
  };
  ConstIterator begin() const { return {this, head_}; }
  ConstIterator end() const { return {this, head_ + size_}; }

 private:
  std::array<std::optional<Command>, Capacity> slots_;
  size_t head_ = 0;
  size_t size_ = 0;
};
}  // namespace rex::graphics::gta4_native
