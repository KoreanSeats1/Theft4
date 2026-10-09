#pragma once
#include <cstdint>
namespace gta4::mods {
// Configured once before starting the guest, never from the rendering thread.
void ConfigureGameplay(bool god_mode, bool unlimited_ammo) noexcept;
void BeforePhysics(uint8_t* base);
}
