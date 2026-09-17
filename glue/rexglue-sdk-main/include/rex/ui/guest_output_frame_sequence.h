#pragma once

#include <cstdint>

namespace rex::ui {

// Producer-owned content identity, independent of reusable image allocations.
// Store the returned value in the mailbox properties before releasing them.
class GuestOutputContentSequence {
 public:
  uint64_t Publish(bool active) { return active ? ++sequence_ : 0; }

 private:
  uint64_t sequence_ = 0;
};

// Consumer-owned, protected by the presenter's existing paint serialization.
// A skipped publication is not a displayed frame; a repeated one counts once.
// This measures successful queue handoffs, not physical display scanout.
class GuestOutputFrameCounter {
 public:
  bool NotePresented(uint64_t sequence, bool succeeded) {
    if (!succeeded || sequence <= last_presented_sequence_) return false;
    last_presented_sequence_ = sequence;
    ++count_;
    return true;
  }
  uint64_t count() const { return count_; }

 private:
  uint64_t last_presented_sequence_ = 0;
  uint64_t count_ = 0;
};

}  // namespace rex::ui
