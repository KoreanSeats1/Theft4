#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

#include <rex/graphics/gta4_native/title_commands.h>

namespace rex::graphics::gta4_native {

// Used only while command_capture_mutex_ is held. Remember successfully queued
// notifications, not guest resources or their contents. Draw capture still reads
// authoritative guest state and retains resources through the existing path.
// Registry/lifetime/flush boundaries invalidate every entry. Shader/declaration
// commands are never suppressed (their resource identities can change).
class NativeProducerBindingCache {
 public:
  void Reset() { for (auto& entry : entries_) entry.size = 0; }

  bool CanSkip(const void* data, size_t size) {
    const size_t slot = Slot(data, size);
    if (slot == kInvalid) {
      if (!Independent(data, size)) Reset();
      return false;
    }
    const auto& entry = entries_[slot];
    return entry.size == size && std::memcmp(entry.bytes.data(), data, size) == 0;
  }

  // Call only after validation and successful queue insertion. Failed or
  // merely inspected commands must never seed the redundant-state fast path.
  void RememberQueued(const void* data, size_t size) {
    const size_t slot = Slot(data, size);
    if (slot == kInvalid) return;
    auto& entry = entries_[slot];
    std::memcpy(entry.bytes.data(), data, size);
    entry.size = size;
  }

 private:
  static constexpr size_t kInvalid = std::numeric_limits<size_t>::max();
  static constexpr size_t kStreamBase = kTextureStageCount;
  static constexpr size_t kTargetBase = kStreamBase + kVertexStreamCount;
  static constexpr size_t kDepthSlot = kTargetBase + kRenderTargetCount;
  static constexpr size_t kIndexSlot = kDepthSlot + 1;
  struct Entry {
    std::array<unsigned char, 192> bytes;
    size_t size = 0;
  };
  std::array<Entry, kIndexSlot + 1> entries_;

  template <typename T>
  static bool Read(const void* data, size_t size, T& value) {
    static_assert(sizeof(T) <= 192);
    if (!data || size != sizeof(T)) return false;
    std::memcpy(&value, data, size);
    return value.header.size == size && value.device != 0;
  }

  static size_t Slot(const void* data, size_t size) {
    if (!data || size < sizeof(CommandHeader)) return kInvalid;
    CommandHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.size != size) return kInvalid;
    switch (header.type) {
      case CommandType::kSetTexture: {
        SetTextureCommand value{};
        // Font registration has producer-side effects. Keep it on the full
        // path even when its binding bytes are unchanged.
        return Read(data, size, value) && value.stage < kTextureStageCount &&
                       value.vector_font_id == 0
                   ? value.stage : kInvalid;
      }
      case CommandType::kSetVertexStream: {
        SetVertexStreamCommand value{};
        return Read(data, size, value) && value.stream < kVertexStreamCount
                   ? kStreamBase + value.stream : kInvalid;
      }
      case CommandType::kSetRenderTarget: {
        SetRenderTargetCommand value{};
        return Read(data, size, value) && value.index < kRenderTargetCount
                   ? kTargetBase + value.index : kInvalid;
      }
      case CommandType::kSetDepthStencil: {
        SetDepthStencilCommand value{};
        return Read(data, size, value) ? kDepthSlot : kInvalid;
      }
      case CommandType::kSetIndexBuffer: {
        SetIndexBufferCommand value{};
        return Read(data, size, value) ? kIndexSlot : kInvalid;
      }
      default: return kInvalid;
    }
  }

  static bool Independent(const void* data, size_t size) {
    if (!data || size < sizeof(CommandHeader)) return false;
    CommandHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.size != size) return false;
    // These commands do not mutate the worker's cached binding fields.
    switch (header.type) {
      case CommandType::kDrawPrimitive: return size == sizeof(DrawPrimitiveCommand);
      case CommandType::kDrawPrimitiveUp: return size == sizeof(DrawPrimitiveUpCommand);
      case CommandType::kDrawIndexedPrimitive: return size == sizeof(DrawIndexedPrimitiveCommand);
      case CommandType::kSetPixelShader:
      case CommandType::kSetVertexShader: return size == sizeof(SetShaderCommand);
      case CommandType::kSetVertexDeclaration: return size == sizeof(SetVertexDeclarationCommand);
      case CommandType::kSetRenderState: return size == sizeof(SetRenderStateCommand);
      default: return false;
    }
  }
};

}  // namespace rex::graphics::gta4_native
