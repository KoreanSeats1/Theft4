#pragma once

#include <bit>
#include <cstdint>

namespace gta4::physics {

// Match the guest's binary32 timestep ABI; normal values are never rounded or
// rewritten. The legacy 1/150 Pre/Post floor is deliberately not applied:
// the outer loop multiplies by a guest scale, so valid tiny steps must survive.
inline constexpr double kMaximumTimeStep = double(1.0f / 30.0f);
inline constexpr double kInvalidTimeStepFallback = double(0x1.b4e81cp-8f);

enum class ClampReason : std::uint8_t { kNone, kOversized, kNegative, kNonfinite };
struct BoundedTimeStep {
  double value;
  ClampReason reason;
};

inline BoundedTimeStep BoundTimeStep(double input) noexcept {
  // Inspect the exponent before comparisons to avoid a floating-point compare
  // on signaling NaNs. No host clock, allocation, or rounding conversion here.
  const auto bits = std::bit_cast<std::uint64_t>(input);
  if ((bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000))
    return {kInvalidTimeStepFallback, ClampReason::kNonfinite};
  if (input < 0.0) return {0.0, ClampReason::kNegative};
  if (input > kMaximumTimeStep) return {kMaximumTimeStep, ClampReason::kOversized};
  return {input, ClampReason::kNone};
}

// Only calls in the dynamic extent of the authoritative outer physics update
// are guarded. Nesting and exceptions restore the previous depth; other guest
// threads and unrelated direct stage calls are unaffected.
inline thread_local std::uint32_t update_depth = 0;
class PhysicsUpdateScope final {
 public:
  explicit PhysicsUpdateScope(bool enabled) noexcept : enabled_(enabled) {
    if (enabled_) ++update_depth;
  }
  ~PhysicsUpdateScope() {
    if (enabled_) --update_depth;
  }
  PhysicsUpdateScope(const PhysicsUpdateScope&) = delete;
  PhysicsUpdateScope& operator=(const PhysicsUpdateScope&) = delete;
 private:
  bool enabled_;
};

inline bool GuardActive() noexcept { return update_depth != 0; }
inline bool ShouldLogClamp(std::uint64_t count) noexcept {
  return count && (count <= 8 || (count & (count - 1)) == 0);
}

}  // namespace gta4::physics
