#pragma once
#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace rex::graphics::gta4_native {
// Describes the writes made after whole-buffer endian conversion, rather than
// the identities of shaders that happen to require those same writes.
struct NativeMetalVertexConversionPlan {
  struct Write {
    uint32_t offset=0,components_16=0,kind=0; // 1: halfwords, 2: DEC3N, 3: uint color
    auto operator<=>(const Write&) const = default;
  };
  std::array<Write,32> writes{};
  uint32_t count=0,stride=0;
  bool whole_records=true;
  auto operator<=>(const NativeMetalVertexConversionPlan&) const = default;
  uint32_t Offset(uint32_t offset) const {
    if(!count)return 0; // Only the shader-independent endian copy is needed.
    return whole_records?offset%stride:offset;
  }
};
struct NativeMetalVertexConversion {
  NativeMetalVertexConversionPlan plan;
  uint64_t identity=0; // Monotonic allocation identity, never a content hash.
};

template<class Declaration,class Shader,class ComponentCount,class Location>
std::optional<NativeMetalVertexConversionPlan> BuildNativeMetalVertexConversionPlan(
    const Declaration& declaration,const Shader& shader,uint32_t stream,uint32_t stride,
    ComponentCount component_count,Location location) {
  if(!stride)return std::nullopt;
  NativeMetalVertexConversionPlan plan;plan.stride=stride;
  for(const auto& element:declaration.elements) {
    if(element.stream!=stream||element.offset>=stride)continue;
    const auto input=std::find_if(shader.vertex_inputs.begin(),shader.vertex_inputs.end(),
        [&](const auto& value){return value.location==location(element.usage,element.usage_index);});
    if(input==shader.vertex_inputs.end())continue;
    const uint32_t components=component_count(element.type);
    using Numeric=std::remove_cvref_t<decltype(input->numeric_type)>;
    const uint32_t kind=components?1:element.type==0x1A2187?2:
        element.type==0x182886&&input->numeric_type==Numeric::kUnsignedInteger?3:0;
    if(!kind)continue;
    if(plan.count==plan.writes.size())return std::nullopt;
    plan.writes[plan.count++]={element.offset,components,kind};
    const uint32_t width=components?components*2:4;
    // Earlier records may be converted too only when their writes cannot
    // extend into the requested record. Overlapping writes WITHIN a record
    // retain their original declaration order.
    if(width>stride-element.offset)plan.whole_records=false;
  }
  if(!plan.count)plan.stride=0;
  return plan;
}
} // namespace rex::graphics::gta4_native
