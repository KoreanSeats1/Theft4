#pragma once

#include <array>
#include <cassert>
#include <cstring>
#include <memory>
#include <utility>
#include <rex/graphics/gta4_native/title_commands.h>
#include "native_cpu_profile.h"

namespace rex::graphics::gta4_native {

// State notifications have no owned draw resources. Keep them out of the large
// NativeCommand constructor/destructor path without changing FIFO semantics.
struct NativeStatePacket {
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
    static_assert(sizeof(T) <= sizeof(NativeStatePacket::bytes));
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

// Both payloads have stable addresses and unique ownership across queue/batch
// transfer. Only full packets participate in texture generation protection.
template <typename Full>
class NativeCommandPacket {
 public:
  NativeCommandPacket(std::unique_ptr<Full> full) : full_(std::move(full)) { assert(full_); }
  NativeCommandPacket(std::unique_ptr<NativeStatePacket> state) : state_(std::move(state)) { assert(state_); }
  Full* FullCommand() const { return full_.get(); }
  NativeStatePacket* State() const { return state_.get(); }
  std::unique_ptr<Full> TakeFull() { assert(full_); return std::move(full_); }
  std::unique_ptr<NativeStatePacket> TakeState() { assert(state_); return std::move(state_); }
 private:
  std::unique_ptr<Full> full_;
  std::unique_ptr<NativeStatePacket> state_;
};

template <typename Full>
Full& NativeQueueCommand(NativeCommandPacket<Full>& packet) {
  assert(packet.FullCommand()); return *packet.FullCommand();
}
template <typename Full>
const Full& NativeQueueCommand(const NativeCommandPacket<Full>& packet) {
  assert(packet.FullCommand()); return *packet.FullCommand();
}
template <typename Command>
bool NativeQueueHasResources(const Command&) { return true; }
template <typename Full>
bool NativeQueueHasResources(const NativeCommandPacket<Full>& packet) { return packet.FullCommand() != nullptr; }

}  // namespace rex::graphics::gta4_native
