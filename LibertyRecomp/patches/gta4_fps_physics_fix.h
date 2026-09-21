#pragma once

namespace gta4::fps::physics
{
// FusionFix floors only the PreSim/PostSim slice delta to 1 / 150 second.
// This is the exact IEEE-754 binary32 value (0x3BDA740E), calculated with Python.
inline constexpr float kPrePostMinimumTimeStep = 0x1.b4e81cp-8f;
// A long frame must not feed an unbounded slice into the collision solver.
// Normal 30 FPS slices are unchanged; longer slices are capped at 1 / 30 second.
inline constexpr float kMaximumTimeStep = 1.0f / 30.0f;
}
