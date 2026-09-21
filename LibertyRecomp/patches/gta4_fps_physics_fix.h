#pragma once

#include <chrono>
#include <cstdint>

namespace gta4::fps::physics
{
// FusionFix floors only the PreSim/PostSim slice delta to 1 / 150 second.
// This is the exact IEEE-754 binary32 value (0x3BDA740E), calculated with Python.
inline constexpr float kPrePostMinimumTimeStep = 0x1.b4e81cp-8f;
// A long frame must not feed an unbounded slice into the collision solver.
// Normal 30 FPS slices are unchanged; longer slices are capped at 1 / 30 second.
inline constexpr float kMaximumTimeStep = 1.0f / 30.0f;

#ifdef THEFT4_LAB_BUILD
// sub_8297CE78 normally completes well inside one 30 Hz frame, but a damaged
// collision graph can make its nested walks cycle indefinitely. Check time
// only once per 1024 traversal steps so the healthy path stays effectively
// free. The generated function uses this guard only around its collision-list
// construction and continues through the title's normal cleanup path when it
// trips.
class CollisionTraversalWatchdog final
{
public:
    bool Continue() noexcept
    {
        ++iterations_;
        if (!started_)
        {
            start_ = std::chrono::steady_clock::now();
            started_ = true;
            return true;
        }
        if (iterations_ < nextCheck_)
        {
            return true;
        }
        nextCheck_ += kCheckInterval;
        return std::chrono::steady_clock::now() - start_ <= kMaximumDuration;
    }

    std::uint32_t iterations() const noexcept { return iterations_; }

private:
    static constexpr std::uint32_t kCheckInterval = 1024;
    static constexpr auto kMaximumDuration = std::chrono::milliseconds(50);
    std::chrono::steady_clock::time_point start_{};
    std::uint32_t iterations_ = 0;
    std::uint32_t nextCheck_ = kCheckInterval;
    bool started_ = false;
};

void ReportCollisionTraversalAbort(std::uint32_t iterations) noexcept;
#endif
}
