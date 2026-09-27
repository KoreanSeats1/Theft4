#include "../../glue/rexglue-sdk-main/gta4-recomp/src/gta4_physics_hooks.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

using namespace gta4::physics;

int main() {
  // Normal 30 FPS and 24 FPS with the initialized two physical slices,
  // zero/pause, slow motion, subnormals, and the exact cap are untouched.
  for (const double input : {0.0, -0.0, 1.0/10000.0, 1.0/300.0,
                            double(1.0f/60.0f), double(1.0f/48.0f),
                            kMaximumTimeStep,
                            std::numeric_limits<double>::denorm_min(),
                            std::nextafter(kMaximumTimeStep, 0.0)}) {
    const auto result=BoundTimeStep(input);
    assert(result.reason==ClampReason::kNone);
    assert(std::bit_cast<std::uint64_t>(result.value)==std::bit_cast<std::uint64_t>(input));
  }
  for (const double input : {double(0.150f/2.0f), 1.0,
                            std::numeric_limits<double>::max(),
                            std::nextafter(kMaximumTimeStep, 1.0)}) {
    const auto result=BoundTimeStep(input);
    assert(result.reason==ClampReason::kOversized && result.value==kMaximumTimeStep);
  }
  for (const double input : {-0.001, -1.0, -std::numeric_limits<double>::max()}) {
    const auto result=BoundTimeStep(input);
    assert(result.reason==ClampReason::kNegative && result.value==0.0);
  }
  for (const auto bits : {UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000),
                         UINT64_C(0x7ff8000000000001), UINT64_C(0x7ff0000000000001),
                         UINT64_C(0xfff8000000000001)}) {
    const auto result=BoundTimeStep(std::bit_cast<double>(bits));
    assert(result.reason==ClampReason::kNonfinite && result.value==kInvalidTimeStepFallback);
  }
  assert(!GuardActive());
  { PhysicsUpdateScope off(false); assert(!GuardActive()); }
  {
    PhysicsUpdateScope outer(true);
    assert(GuardActive() && update_depth==1);
    { PhysicsUpdateScope nested(true); assert(update_depth==2); }
    assert(update_depth==1);
    { PhysicsUpdateScope off(false); assert(update_depth==1); }
    bool isolated=false;
    std::thread other([&] {
      assert(!GuardActive());
      { PhysicsUpdateScope own(true); assert(update_depth==1); }
      isolated=!GuardActive();
    });
    other.join();
    assert(isolated && update_depth==1);
    try { PhysicsUpdateScope nested(true); throw 1; } catch (int) {}
    assert(update_depth==1);
  }
  assert(!GuardActive());
  assert(!ShouldLogClamp(0));
  for (std::uint64_t n=1;n<=1024;++n)
    assert(ShouldLogClamp(n)==(n<=8 || (n&(n-1))==0));
  std::cout << "physics guard policy, unchanged values, scope, and bounded logging passed\n";
}
