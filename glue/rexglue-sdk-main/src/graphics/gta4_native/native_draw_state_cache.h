#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rex::graphics::gta4_native {

// Tracks commands actually emitted into one command buffer. This owns no GPU
// resources. Reset at recording boundaries and after any external draw path.
// Floating-point state is passed as bits, preserving signed zero and NaN values.
template <size_t DescriptorSetCount, size_t VertexBindingCount = 16>
class NativeDrawStateCache {
 public:
  void Reset() { *this = {}; }

  bool UpdatePipeline(uint64_t pipeline) {
    if (!pipeline_.Update(pipeline)) {
      return false;
    }
    // A different pipeline can contain static state that invalidates dynamic
    // values. Conservatively re-emit them even for compatible native pipelines.
    viewport_ = {};
    scissor_ = {};
    depth_bias_ = {};
    stencil_ = {};
    blend_constants_ = {};
    push_constants_ = {};
    return true;
  }

  // Resource uploads/validation must happen before these checks. An unchanged
  // binding says nothing about whether the resource's contents have changed.
  // Buffer state survives pipeline switches, but never Reset/command boundaries.
  bool UpdateVertexBuffer(size_t binding, uint64_t buffer, uint64_t offset) {
    if (binding >= vertex_buffers_.size()) return true;
    return vertex_buffers_[binding].Update({buffer, offset});
  }
  bool UpdateIndexBuffer(uint64_t buffer, uint64_t offset, uint32_t type) {
    return index_buffer_.Update({buffer, offset, type});
  }

  bool UpdateDescriptors(uint64_t layout,
                         const std::array<uint64_t, DescriptorSetCount>& sets) {
    return descriptors_.Update({layout, sets});
  }
  bool UpdateViewport(const std::array<uint32_t, 6>& bits) { return viewport_.Update(bits); }
  bool UpdateScissor(const std::array<int64_t, 4>& rectangle) {
    return scissor_.Update(rectangle);
  }
  bool UpdateDepthBias(const std::array<uint32_t, 3>& bits) { return depth_bias_.Update(bits); }
  bool UpdateStencil(const std::array<uint32_t, 6>& faces) { return stencil_.Update(faces); }
  bool UpdateBlendConstants(const std::array<uint32_t, 4>& bits) {
    return blend_constants_.Update(bits);
  }
  bool UpdatePushConstants(uint64_t layout, const std::array<uint64_t, 3>& addresses) {
    return push_constants_.Update({layout, addresses});
  }

 private:
  template <typename Value>
  struct Tracked {
    std::optional<Value> last;
    bool Update(const Value& value) {
      if (last && *last == value) {
        return false;
      }
      last = value;
      return true;
    }
  };
  template <size_t Count>
  struct LayoutValues {
    uint64_t layout;
    std::array<uint64_t, Count> values;
    bool operator==(const LayoutValues&) const = default;
  };

  Tracked<uint64_t> pipeline_;
  std::array<Tracked<std::array<uint64_t, 2>>, VertexBindingCount> vertex_buffers_;
  Tracked<std::array<uint64_t, 3>> index_buffer_;
  Tracked<LayoutValues<DescriptorSetCount>> descriptors_;
  Tracked<std::array<uint32_t, 6>> viewport_;
  Tracked<std::array<int64_t, 4>> scissor_;
  Tracked<std::array<uint32_t, 3>> depth_bias_;
  Tracked<std::array<uint32_t, 6>> stencil_;
  Tracked<std::array<uint32_t, 4>> blend_constants_;
  Tracked<LayoutValues<3>> push_constants_;
};

}  // namespace rex::graphics::gta4_native
