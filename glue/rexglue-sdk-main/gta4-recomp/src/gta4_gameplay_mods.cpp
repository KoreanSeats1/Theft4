#include "gta4_gameplay_mods.h"
#include "gta4_init.h"
#include <rex/system/kernel_state.h>
#include <atomic>
#include <bit>
#include <cstring>

namespace gta4::mods {
namespace {
std::atomic<unsigned> options{0};
// A publication hint only: never dereferenced. Normal repeated player queries
// perform no heap checks or inventory scan; physics/firing validate fresh state.
std::atomic<uint32_t> published_player{0};
constexpr unsigned kGod = 1, kAmmo = 2;
// TU8 SET_PLAYER_INVINCIBLE, SET_CHAR_PROOFS, and drowning/injury setters.
constexpr uint32_t kProofs = 0x02000000 | 0x01000000 | 0x00080000 | 0x00800000 | 0x00400000;
constexpr uint32_t kInvincible = 0x00200000;
constexpr uint32_t kDrowningAndInjury = 0x10000000 | 0x08000000 | 0x04000000 | 0x10;
constexpr uint32_t kNoCriticalHits = 0x20000000;
// Finite reserve, so disabling the mod never leaves the game's 25000 infinite
// sentinel in a save. Firing preserves this total; magazine +28 is untouched.
constexpr uint16_t kReserveFloor = 1000;

bool Span(uint8_t* base, uint32_t address, size_t size, bool write = false) {
  if (!base || !address || !size || uint64_t(address) + size > uint64_t(UINT32_MAX) + 1)
    return false;
  auto* kernel = REX_KERNEL_STATE();
  auto* memory = kernel ? kernel->memory() : nullptr;
  auto* heap = memory ? memory->LookupHeap(address) : nullptr;
  const auto last = uint32_t(uint64_t(address) + size - 1);
  if (!heap || heap != memory->LookupHeap(last)) return false;
  const auto access = heap->QueryRangeAccess(address, last);
  using rex::memory::PageAccess;
  return access == PageAccess::kReadWrite || access == PageAccess::kExecuteReadWrite ||
      (!write && (access == PageAccess::kReadOnly || access == PageAccess::kExecuteReadOnly));
}
uint32_t Read(uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, rex::memory::GuestPtr(base, address), 4);
  return __builtin_bswap32(value);
}
uint16_t Read16(uint8_t* base, uint32_t address) {
  uint16_t value;
  std::memcpy(&value, rex::memory::GuestPtr(base, address), 2);
  return __builtin_bswap16(value);
}
void Write(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(rex::memory::GuestPtr(base, address), &value, 4);
}
void Write16(uint8_t* base, uint32_t address, uint16_t value) {
  value = __builtin_bswap16(value);
  std::memcpy(rex::memory::GuestPtr(base, address), &value, 2);
}
// Exactly the local-player lookup used by sub_82238C28, with range checks.
uint32_t Player(uint8_t* base) {
  if (!Span(base, 0x82A98778, 4)) return 0;
  const uint32_t index = Read(base, 0x82A98778);
  if (index >= 4 || !Span(base, 0x82C01C70 + index * 4, 4)) return 0;
  const uint32_t info = Read(base, 0x82C01C70 + index * 4);
  if (!info || !Span(base, info, 1404)) return 0;
  const uint32_t ped = Read(base, info + 1400);
  return Span(base, ped, 764, true) ? ped : 0;
}
// Script handles are generation-tagged pool indices, never raw ped pointers.
uint32_t ResolvePed(uint8_t* base, uint32_t handle) {
  if (int32_t(handle) < 0 || !Span(base, 0x831D5388, 4)) return 0;
  const auto pool = Read(base, 0x831D5388);
  if (!Span(base, pool, 20)) return 0;
  const uint32_t index = handle >> 8, tags = Read(base, pool + 4);
  const uint32_t count = Read(base, pool + 8), stride = Read(base, pool + 12);
  if (index >= count || stride < 764 || !Span(base, tags, size_t(index) + 1)) return 0;
  if (*rex::memory::GuestPtr(base, tags + index) != uint8_t(handle)) return 0;
  const uint64_t address = uint64_t(Read(base, pool)) + uint64_t(index) * stride;
  if (address > UINT32_MAX) return 0;
  const uint32_t ped = uint32_t(address);
  return Span(base, ped, 764, true) ? ped : 0;
}
void Protect(uint8_t* base, uint32_t ped) {
  const uint32_t flags = Read(base, ped + 280), injury = Read(base, ped + 560);
  const uint32_t protected_flags = flags | kInvincible | kProofs;
  const uint32_t protected_injury = (injury & ~kDrowningAndInjury) | kNoCriticalHits;
  if (flags != protected_flags) Write(base, ped + 280, protected_flags);
  if (injury != protected_injury) Write(base, ped + 560, protected_injury);
}
uint32_t ActiveWeapon(uint8_t* base, uint32_t ped) {
  const uint32_t manager = ped + 640, slot = Read(base, manager);
  if (slot >= 11) return 0;
  const uint32_t owner = Read(base, manager + 20);
  uint32_t weapon = 0;
  if (owner) {
    if (!Span(base, owner, 604)) return 0;
    weapon = Read(base, owner + 600);
  } else if (slot == 0 || slot == 10) {
    weapon = Read(base, manager + 32);
  }
  if (!Span(base, weapon, 32, true) ||
      Read(base, weapon + 20) != Read(base, manager + 36 + slot * 8)) return 0;
  return weapon;
}
void Supply(uint8_t* base, uint32_t ped) {
  const uint32_t manager = ped + 640;
  // Inventory entries are {weapon type, u16 total ammo, u16 padding}. Skip
  // unarmed/melee, and never grant an empty slot or touch clip/weapon selection.
  for (uint32_t slot = 2; slot < 11; ++slot) {
    const uint32_t entry = manager + 36 + slot * 8;
    if (int32_t(Read(base, entry)) > 0 && Read16(base, entry + 4) < kReserveFloor)
      Write16(base, entry + 4, kReserveFloor);
  }
  if (const uint32_t weapon = ActiveWeapon(base, ped)) {
    const uint32_t slot = Read(base, manager);
    if (slot >= 2 && Read16(base, weapon + 30) < kReserveFloor)
      Write16(base, weapon + 30, kReserveFloor);
  }
}
void Apply(uint8_t* base, uint32_t ped, unsigned enabled) {
  if (!ped) return;
  if (enabled & kGod) Protect(base, ped);
  if (enabled & kAmmo) Supply(base, ped);
}
unsigned Enabled() { return options.load(std::memory_order_relaxed); }
}
void ConfigureGameplay(bool god_mode, bool unlimited_ammo) noexcept {
  published_player.store(0, std::memory_order_relaxed);
  options.store((god_mode ? kGod : 0) | (unlimited_ammo ? kAmmo : 0), std::memory_order_relaxed);
}
void BeforePhysics(uint8_t* base) {
  const unsigned enabled = Enabled();
  if (enabled) Apply(base, Player(base), enabled);
}
}

// Player publication/load/respawn: protection is applied before a caller can
// use the newly returned ped. Original lookup and PPC results stay authoritative.
extern "C" void sub_82238C28(PPCContext& ctx, uint8_t* base) {
  const bool local_query = ctx.r3.u32 == 0;
  __imp__sub_82238C28(ctx, base);
  const auto enabled = gta4::mods::Enabled();
  if (!enabled) return;
  if (local_query && !ctx.r3.u32) {
    gta4::mods::published_player.store(0, std::memory_order_relaxed);
  } else if (ctx.r3.u32 &&
             ctx.r3.u32 != gta4::mods::published_player.load(std::memory_order_relaxed) &&
             ctx.r3.u32 == gta4::mods::Player(base)) {
    gta4::mods::Apply(base, ctx.r3.u32, enabled);
    gta4::mods::published_player.store(ctx.r3.u32, std::memory_order_relaxed);
  }
}

// TU8's common CWeapon::Fire path decrements +28 (clip) separately from +30
// (total). Keep all original ballistic, animation, reload and projectile work.
extern "C" void sub_8226F428(PPCContext& ctx, uint8_t* base) {
  const auto enabled = gta4::mods::Enabled();
  const uint32_t ped = enabled & gta4::mods::kAmmo ? gta4::mods::Player(base) : 0;
  const uint32_t weapon = ctx.r3.u32;
  const bool preserve = ped && weapon && ctx.r4.u32 == ped && weapon == gta4::mods::ActiveWeapon(base, ped);
  uint16_t total = 0;
  if (preserve) {
    total = gta4::mods::Read16(base, weapon + 30);
    if (total < gta4::mods::kReserveFloor) {
      total = gta4::mods::kReserveFloor;
      gta4::mods::Write16(base, weapon + 30, total);
    }
  }
  __imp__sub_8226F428(ctx, base);
  if (preserve && gta4::mods::Player(base) == ped &&
      weapon == gta4::mods::ActiveWeapon(base, ped))
    gta4::mods::Write16(base, weapon + 30, total);
}

// The health leaf is also used by scripts and damage services. Allow healing
// and initialization; block reductions to the current live local player.
extern "C" void sub_8247E8A0(PPCContext& ctx, uint8_t* base) {
  if (gta4::mods::Enabled() & gta4::mods::kGod) {
    const uint32_t ped = gta4::mods::Player(base);
    if (ped && ctx.r3.u32 == ped) {
      const float health = std::bit_cast<float>(gta4::mods::Read(base, ped + 484));
      if (health > 0 && !(ctx.f1.f64 >= health)) return;
    }
  }
  __imp__sub_8247E8A0(ctx, base);
}
// SET_CHAR_HEALTH can schedule death tasks before reaching the health leaf.
extern "C" void sub_825BB5F0(PPCContext& ctx, uint8_t* base) {
  if (gta4::mods::Enabled() & gta4::mods::kGod) {
    const uint32_t ped = gta4::mods::Player(base);
    if (ped && gta4::mods::ResolvePed(base, ctx.r3.u32) == ped) {
      const float health = std::bit_cast<float>(gta4::mods::Read(base, ped + 484));
      if (health > 0 && ctx.r4.s32 < health) return;
    }
  }
  __imp__sub_825BB5F0(ctx, base);
}

// Mission scripts sometimes reset these flags. Reassert only the local player's
// mod-owned bits after the original setter, leaving the script arguments and
// every NPC's flags unchanged.
#define THEFT4_KEEP_PLAYER_PROTECTED(name) \
  extern "C" void name(PPCContext& ctx, uint8_t* base) { \
    __imp__##name(ctx, base); \
    if (gta4::mods::Enabled() & gta4::mods::kGod) { \
      if (const uint32_t ped = gta4::mods::Player(base)) gta4::mods::Protect(base, ped); \
    } \
  }
THEFT4_KEEP_PLAYER_PROTECTED(sub_825B5E80)  // SET_PLAYER_INVINCIBLE
THEFT4_KEEP_PLAYER_PROTECTED(sub_825C85E8)  // SET_CHAR_INVINCIBLE
THEFT4_KEEP_PLAYER_PROTECTED(sub_825BBC50)  // SET_CHAR_PROOFS core
THEFT4_KEEP_PLAYER_PROTECTED(sub_825C4690)  // SET_CHAR_DROWNS_IN_WATER
THEFT4_KEEP_PLAYER_PROTECTED(sub_825C4710)  // SET_CHAR_DROWNS_IN_SINKING_VEHICLE
THEFT4_KEEP_PLAYER_PROTECTED(sub_825C7CD0)  // SET_PED_DIES_WHEN_INJURED
THEFT4_KEEP_PLAYER_PROTECTED(sub_825C3C38)  // SET_CHAR_SUFFERS_CRITICAL_HITS
THEFT4_KEEP_PLAYER_PROTECTED(sub_825C90E8)  // SET_CHAR_DIES_INSTANTLY_IN_WATER
#undef THEFT4_KEEP_PLAYER_PROTECTED

// EXPLODE_CHAR_HEAD bypasses ordinary damage/proof checks.
extern "C" void sub_825BBFE0(PPCContext& ctx, uint8_t* base) {
  if (gta4::mods::Enabled() & gta4::mods::kGod) {
    const uint32_t ped = gta4::mods::Player(base);
    if (ped && gta4::mods::ResolvePed(base, ctx.r3.u32) == ped) return;
  }
  __imp__sub_825BBFE0(ctx, base);
}
