#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <rex/graphics/gta4_native/title_commands.h>
#include "native_cpu_profile.h"

namespace rex::graphics::gta4_native {

// State notifications have no owned draw resources. Keep them out of the large
// NativeCommand constructor/destructor path without changing FIFO semantics.
struct NativeStateCommand {
  CommandType type = CommandType::kPresent;
  std::array<unsigned char, 64> bytes;
  size_t size = 0;
  profile::CommandTransport transport;
  uint64_t sequence = 0;
  uint32_t epoch = 0;
};

inline bool IsCompactNativeStateCommand(const void* data, size_t size) {
  if (!data || size < sizeof(CommandHeader)) return false;
  CommandHeader header{};
  std::memcpy(&header, data, sizeof(header));
  if (header.size != size) return false;
  const auto read = [&]<typename T>(T& value) {
    static_assert(sizeof(T) <= sizeof(NativeStateCommand::bytes));
    if (size != sizeof(T)) return false;
    std::memcpy(&value, data, size);
    return value.device != 0;
  };
  switch (header.type) {
    case CommandType::kSetPixelShader:
    case CommandType::kSetVertexShader: { SetShaderCommand v{}; return read(v); }
    case CommandType::kSetVertexDeclaration: { SetVertexDeclarationCommand v{}; return read(v); }
    case CommandType::kSetTexture: {
      SetTextureCommand v{};
      // Font registration has producer-side resource effects; use the full path.
      return read(v) && v.stage < kTextureStageCount && v.vector_font_id == 0;
    }
    case CommandType::kSetDepthStencil: { SetDepthStencilCommand v{}; return read(v); }
    case CommandType::kSetRenderTarget: {
      SetRenderTargetCommand v{}; return read(v) && v.index < kRenderTargetCount;
    }
    case CommandType::kSetVertexStream: {
      SetVertexStreamCommand v{}; return read(v) && v.stream < kVertexStreamCount;
    }
    case CommandType::kSetIndexBuffer: { SetIndexBufferCommand v{}; return read(v); }
    default: return false;
  }
}

// A packet owns a bounded FIFO run, never resources. Optional slots avoid
// clearing unused command payloads when the recycler constructs a new packet.
class NativeStatePacket {
 public:
  static constexpr size_t kCapacity = 8;
  NativeStatePacket() {} // Do not value-initialize the unused payload storage.
  size_t size() const { return size_; }
  bool HasMeasuredEntries() const { return measured_entries_; }
  bool TryAppend(const NativeStateCommand& command) {
    if (size_ == kCapacity || (size_ && entries_[0]->epoch != command.epoch)) return false;
    entries_[size_++].emplace(command);
    measured_entries_ |= command.transport.enqueued != 0;
    return true;
  }
  const NativeStateCommand& operator[](size_t index) const {
    assert(index < size_); return *entries_[index];
  }
 private:
  std::array<std::optional<NativeStateCommand>, kCapacity> entries_;
  size_t size_ = 0;
  bool measured_entries_ = false;
};

// Both payloads have stable addresses and unique ownership across queue/batch
// transfer. Only full packets participate in texture generation protection.
template <typename Full>
class NativeCommandPacket {
 public:
  NativeCommandPacket(std::unique_ptr<Full> full) : full_(std::move(full)) { assert(full_); }
  NativeCommandPacket(std::unique_ptr<NativeStatePacket> state) : state_(std::move(state)) { assert(state_); }
  size_t LogicalSize() const { return state_ ? state_->size() : 1; }
  Full* FullCommand() const { return full_.get(); }
  NativeStatePacket* State() const { return state_.get(); }
  std::unique_ptr<Full> TakeFull() { assert(full_); return std::move(full_); }
  std::unique_ptr<NativeStatePacket> TakeState() { assert(state_); return std::move(state_); }
 private:
  std::unique_ptr<Full> full_;
  std::unique_ptr<NativeStatePacket> state_;
};

// render_mutex_ protects every operation. size() is deliberately the logical
// command count: batching must not enlarge producer backpressure limits.
// take_front transfers exclusive ownership before another producer may append.
template <typename Full>
class NativeCommandQueue {
 public:
  using Packet = NativeCommandPacket<Full>;
  bool empty() const { return packets_.empty(); }
  size_t size() const { return logical_size_; }
  size_t packet_count() const { return packets_.size(); }
  const Packet& front() const { return packets_.front(); }
  bool CanAccept(bool running, size_t maximum_commands, uint32_t queued_presents,
                 uint32_t maximum_presents = 2) const {
    return running && logical_size_ < maximum_commands && queued_presents < maximum_presents;
  }
  bool TryAppendState(const NativeStateCommand& command) {
    if (packets_.empty()) return false;
    auto* tail = packets_.back().State();
    if (!tail || !tail->TryAppend(command)) return false;
    ++logical_size_;
    return true;
  }
  void push_back(Packet&& packet) {
    const size_t count = packet.LogicalSize();
    assert(count);
    packets_.push_back(std::move(packet));
    logical_size_ += count;
  }
  Packet take_front() {
    assert(!empty());
    const size_t count = packets_.front().LogicalSize();
    Packet packet = std::move(packets_.front());
    packets_.pop_front();
    assert(logical_size_ >= count);
    logical_size_ -= count;
    return packet;
  }
  template <typename Batch>
  size_t TransferTo(Batch& batch, size_t maximum_logical_commands) {
    assert(batch.empty());
    assert(maximum_logical_commands >= NativeStatePacket::kCapacity);
    assert(maximum_logical_commands <= batch.capacity());
    size_t transferred = 0;
    while (!empty() && transferred + front().LogicalSize() <= maximum_logical_commands) {
      transferred += front().LogicalSize();
      batch.push_back(take_front());
    }
    return transferred;
  }
  void clear() { packets_.clear(); logical_size_ = 0; }
  auto begin() const { return packets_.begin(); }
  auto end() const { return packets_.end(); }
 private:
  std::deque<Packet> packets_;
  size_t logical_size_ = 0;
};

template <typename Full>
Full& NativeQueueCommand(NativeCommandPacket<Full>& packet) {
  assert(packet.FullCommand()); return *packet.FullCommand();
}
template <typename Full>
const Full& NativeQueueCommand(const NativeCommandPacket<Full>& packet) {
  assert(packet.FullCommand()); return *packet.FullCommand();
}
// Non-Lab builds queue the full command directly rather than wrapping it in a
// compact state packet. Keep the same access helper valid in both builds.
template <typename Command>
Command& NativeQueueCommand(Command& command) { return command; }
template <typename Command>
const Command& NativeQueueCommand(const Command& command) { return command; }
template <typename Command>
bool NativeQueueHasResources(const Command&) { return true; }
template <typename Full>
bool NativeQueueHasResources(const NativeCommandPacket<Full>& packet) { return packet.FullCommand() != nullptr; }

}  // namespace rex::graphics::gta4_native
