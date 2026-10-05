#pragma once
#include <array>
#include <cstdint>

namespace gta4::lod {
struct Selection {
  uint32_t desired=0,secondary=0;
  bool operator==(const Selection&) const = default;
};
// Native form of sub_824F3418. Keep comparison strictness (including unordered
// IEEE inputs), single-precision threshold products, script FORCE_HIGH_LOD,
// and missing-mesh fallback exactly as the title selector. Blending/fading and
// streaming admission remain title-owned; this never asks for a missing mesh.
inline Selection Select(double comparison_distance,float first,float second,float scale,
                        std::array<uint32_t,3> resident,bool force_high) noexcept {
  Selection result;
  if(!resident[1]||force_high)return result;
  const float near_threshold=float(double(first)*double(scale));
  const float far_threshold=float(double(second)*double(scale));
  result.desired=1;
  if(comparison_distance<near_threshold)result.desired=0;
  else if(comparison_distance>far_threshold) {
    result.desired=2;
    if(!resident[2])result.desired=result.secondary=1;
  }
  if(!resident[result.secondary])result.secondary=0;
  return result;
}
} // namespace gta4::lod
