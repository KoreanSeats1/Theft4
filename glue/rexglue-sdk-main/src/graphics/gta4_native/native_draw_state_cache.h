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
  struct Statistics { uint64_t dynamic_requested = 0, dynamic_emitted = 0; };
  const Statistics& statistics() const { return statistics_; }
  // Statistics are cumulative; command state never survives an external encoder.
  void Reset() {
    pipeline_ = {}; vertex_buffers_ = {}; index_buffer_ = {}; descriptors_ = {};
    viewport_ = {}; scissor_ = {}; depth_bias_ = {}; stencil_ = {};
    blend_constants_ = {}; push_constants_ = {};
  }

  bool UpdatePipeline(uint64_t pipeline, bool uniform_dynamic_state = false) {
    if (!pipeline_.Update(pipeline)) {
      return false;
    }
    // All title draw pipelines declare the same seven dynamic states. Binding
    // another such pipeline does not invalidate these values in Vulkan. Helper
    // pipelines still Reset(), including failures after partially recording.
    // Push constants are separately keyed by their exact pipeline layout.
    if (uniform_dynamic_state) return true;
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
  bool UpdateViewport(const std::array<uint32_t, 6>& bits) { return UpdateDynamic(viewport_, bits); }
  bool UpdateScissor(const std::array<int64_t, 4>& rectangle) {
    return UpdateDynamic(scissor_, rectangle);
  }
  bool UpdateDepthBias(const std::array<uint32_t, 3>& bits) { return UpdateDynamic(depth_bias_, bits); }
  bool UpdateStencil(const std::array<uint32_t, 6>& faces) { return UpdateDynamic(stencil_, faces); }
  bool UpdateBlendConstants(const std::array<uint32_t, 4>& bits) {
    return UpdateDynamic(blend_constants_, bits);
  }
  bool UpdatePushConstants(uint64_t layout, const std::array<uint64_t, 3>& addresses) {
    return push_constants_.Update({layout, addresses});
  }

 private:
  Statistics statistics_{};
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
  template <typename Value>
  bool UpdateDynamic(Tracked<Value>& state, const Value& value) {
    ++statistics_.dynamic_requested;
    const bool changed = state.Update(value);
    statistics_.dynamic_emitted += changed;
    return changed;
  }
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
