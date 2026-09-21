#include "gta4_fps_physics_fix.h"

#include "../../glue/rexglue-sdk-main/gta4-recomp/generated/gta4_init.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <cpu/ppc_context.h>
#include <kernel/function.h>

namespace gta4::fps::physics
{
namespace
{
// sub_824797C0 is the Xbox CPhysics outer update. Its generated PPC proves the
// following slice order:
//   sub_82476B58(delta, slice)  - PreSim
//   sub_82476DA0(delta)         - SimUpdate
//   sub_82477EF0()              - manifold iteration
//   sub_82477920(slice, delta)  - PostSim
// Keep the original outer function intact and mark only its dynamic extent so
// the two stage hooks cannot affect an unrelated invocation.
thread_local std::uint32_t gPhysicsUpdateDepth = 0;

class PhysicsUpdateScope final
{
public:
    PhysicsUpdateScope() noexcept
    {
        ++gPhysicsUpdateDepth;
    }

    ~PhysicsUpdateScope()
    {
        --gPhysicsUpdateDepth;
    }

    PhysicsUpdateScope(const PhysicsUpdateScope&) = delete;
    PhysicsUpdateScope& operator=(const PhysicsUpdateScope&) = delete;
};

void BoundTimeStep(PPCContext& ctx, bool floor_pre_post) noexcept
{
    const float timeStep = static_cast<float>(ctx.f1.f64);
    if (!std::isfinite(timeStep))
    {
        ctx.f1.f64 = static_cast<double>(kPrePostMinimumTimeStep);
        return;
    }
    const float lower = floor_pre_post ? kPrePostMinimumTimeStep : 0.0f;
    ctx.f1.f64 = static_cast<double>(std::clamp(timeStep, lower, kMaximumTimeStep));
}
}
}

PPC_FUNC_IMPL(__imp__sub_824797C0);
PPC_FUNC_IMPL(__imp__sub_82476B58);
PPC_FUNC_IMPL(__imp__sub_82476DA0);
PPC_FUNC_IMPL(__imp__sub_82477920);

PPC_FUNC_HOOK(sub_824797C0)
{
    gta4::fps::physics::PhysicsUpdateScope scope;
    __imp__sub_824797C0(ctx, base);
}

PPC_FUNC_HOOK(sub_82476B58)
{
    if (gta4::fps::physics::gPhysicsUpdateDepth != 0)
    {
        gta4::fps::physics::BoundTimeStep(ctx, true);
    }

    __imp__sub_82476B58(ctx, base);
}

PPC_FUNC_HOOK(sub_82476DA0)
{
    if (gta4::fps::physics::gPhysicsUpdateDepth != 0)
    {
        gta4::fps::physics::BoundTimeStep(ctx, false);
    }

    __imp__sub_82476DA0(ctx, base);
}

PPC_FUNC_HOOK(sub_82477920)
{
    if (gta4::fps::physics::gPhysicsUpdateDepth != 0)
    {
        gta4::fps::physics::BoundTimeStep(ctx, true);
    }

    __imp__sub_82477920(ctx, base);
}
