#ifndef GTA4_DRAW_DISTANCE_POLICY_H_
#define GTA4_DRAW_DISTANCE_POLICY_H_

#include <cmath>
#include <limits>

namespace gta4::draw_distance {

// GTA IV's flt_82A931BC is the engine's input multiplier for the published
// world-distance scalar. Keep invalid host configuration out of guest state
// and preserve the retail multiplier as the deterministic fallback.
inline float ResolveEngineScale(double configured_scale) noexcept {
  if (!std::isfinite(configured_scale) || configured_scale < 0.70 ||
      configured_scale > static_cast<double>(std::numeric_limits<float>::max())) {
    return 1.0f;
  }
  return static_cast<float>(configured_scale);
}

}  // namespace gta4::draw_distance

#endif  // GTA4_DRAW_DISTANCE_POLICY_H_
