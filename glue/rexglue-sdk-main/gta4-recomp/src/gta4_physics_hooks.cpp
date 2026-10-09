#include "gta4_physics_hooks.h"
#include "gta4_init.h"
#include "gta4_gameplay_mods.h"
#include <rex/logging.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace {

bool PhysicsGuardEnabled() noexcept {
  static const bool enabled = [] {
    const char* value = std::getenv("THEFT4_PHYSICS_TIMESTEP_GUARD");
    return !value || std::strcmp(value, "0") != 0;
  }();
  return enabled;
}

void BoundPhysicsArgument(PPCContext& ctx, const char* stage) {
  if (!gta4::physics::GuardActive()) return;
  const double input = ctx.f1.f64;
  const auto bounded = gta4::physics::BoundTimeStep(input);
  if (bounded.reason == gta4::physics::ClampReason::kNone) return;
  ctx.f1.f64 = bounded.value;

  // Only a changed argument incurs an atomic increment or logging. The first
  // eight events and exponentially spaced aggregate counts keep persistent
  // bad inputs from producing a per-frame log stream. There are no clock reads.
  static std::atomic<std::uint64_t> clamp_count{0};
  const auto count = clamp_count.fetch_add(1, std::memory_order_relaxed) + 1;
  if (gta4::physics::ShouldLogClamp(count)) {
    const char* reason = bounded.reason == gta4::physics::ClampReason::kOversized
                             ? "oversized"
                         : bounded.reason == gta4::physics::ClampReason::kNegative
                             ? "negative" : "nonfinite";
    REXLOG_WARN("gta4-physics-timestep-guard: count={} stage={} reason={} "
                "input-seconds={} output-seconds={} cap-seconds={} "
                "original-slices-preserved=true",
                count, stage, reason, input, bounded.value,
                gta4::physics::kMaximumTimeStep);
  }
}

}  // namespace

// The original PPC function chooses and reloads its own slice count, computes
// the scaled per-slice f1, and invokes Pre/Sim/manifold/Post in order. Keep that
// entire loop and all original call side effects, rather than reproducing it.
extern "C" void sub_824797C0(PPCContext& ctx, uint8_t* base) {
  gta4::mods::BeforePhysics(base);
  gta4::physics::PhysicsUpdateScope scope(PhysicsGuardEnabled());
  __imp__sub_824797C0(ctx, base);
}

// Actual generated ABI: PreSim timestep=f1, slice=r4.
extern "C" void sub_82476B58(PPCContext& ctx, uint8_t* base) {
  BoundPhysicsArgument(ctx, "pre");
  __imp__sub_82476B58(ctx, base);
}

// Actual generated ABI: SimUpdate timestep=f1.
extern "C" void sub_82476DA0(PPCContext& ctx, uint8_t* base) {
  BoundPhysicsArgument(ctx, "sim");
  __imp__sub_82476DA0(ctx, base);
}

// Actual generated ABI: PostSim timestep=f1, slice=r3 (not f2).
extern "C" void sub_82477920(PPCContext& ctx, uint8_t* base) {
  BoundPhysicsArgument(ctx, "post");
  __imp__sub_82477920(ctx, base);
}
