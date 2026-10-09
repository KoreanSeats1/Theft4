#include "gta4_init.h"
#include "gta4_gameplay_mods.h"
#include <rex/memory.h>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sys/mman.h>
#include "gameplay_extracted.h"
using namespace gta4::mods;
constexpr uint32_t ped = 0x10000, npc = 0x20000, info = 0x30000,
                   weapon = 0x40000, npc_weapon = 0x41000, owner = 0x50000,
                   weapon_info = 0x60000, pool = 0x70000, tags = 0x71000;
static unsigned script_health_calls = 0, head_explosion_calls = 0;
extern "C" void sub_82299AD0(PPCContext& ctx, uint8_t*) { ctx.r3.u32 = weapon_info; }
extern "C" void sub_8225CF68(PPCContext& ctx, uint8_t* base) {
  ctx.r3.u32 = Read(base, 0x82C01C70 + ctx.r3.u32 * 4);
}
extern "C" void __imp__sub_825BB5F0(PPCContext&, uint8_t*) { ++script_health_calls; }
extern "C" void __imp__sub_825BBFE0(PPCContext&, uint8_t*) { ++head_explosion_calls; }
static PPCContext Context(uint32_t arg = ped) {
  PPCContext ctx{}; ctx.r1.u32 = 0x80000; ctx.r3.u32 = arg; return ctx;
}
static void Setup(uint8_t* base, uint32_t actor, uint32_t gun, unsigned slot, unsigned total, unsigned clip) {
  std::memset(base+actor, 0, 764);
  std::memset(base+gun, 0, 48);
  const uint32_t manager = actor + 640;
  Write(base, manager, slot);
  Write(base, manager + 20, actor == ped ? owner : owner + 0x1000);
  Write(base, (actor == ped ? owner : owner + 0x1000) + 600, gun);
  Write(base, manager + 36 + slot * 8, 7); // a weapon already owned
  Write16(base, manager + 40 + slot * 8, total);
  Write(base, gun + 20, 7);
  Write16(base, gun + 28, clip);
  Write16(base, gun + 30, total);
  Write(base, actor + 484, std::bit_cast<uint32_t>(100.0f));
}
int main() {
  const auto size = UINT64_C(1)<<32;
  auto* base = static_cast<uint8_t*>(mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON,-1,0));
  assert(base != MAP_FAILED);
  Write(base, 0x82A98778, 0); Write(base, 0x82C01C70, info); Write(base, info+1400, ped);
  Write(base, 0x831D5388, pool); Write(base, pool, ped); Write(base, pool+4, tags);
  Write(base, pool+8, 2); Write(base, pool+12, npc-ped);
  base[tags] = 7; base[tags+1] = 9;
  Write16(base, weapon_info+134, 30);
  Setup(base,ped,weapon,2,40,30); Setup(base,npc,npc_weapon,2,40,30);

  // Off preserves baseline gameplay: health reductions and clip/total both fall.
  ConfigureGameplay(false,false);
  auto ctx=Context(weapon); ctx.r4.u32=ped; sub_8226F428(ctx,base);
  assert(Read16(base,weapon+28)==29 && Read16(base,weapon+30)==39);
  ctx=Context(); ctx.f1.f64=25; sub_8247E8A0(ctx,base);
  assert(std::bit_cast<float>(Read(base,ped+484))==25);
  BeforePhysics(base); assert(Read(base,ped+280)==0);

  // God Mode blocks reductions/death, permits healing, and leaves NPCs alone.
  Setup(base,ped,weapon,2,40,30); ConfigureGameplay(true,false);
  ctx=Context(0); sub_82238C28(ctx,base); assert(ctx.r3.u32==ped);
  assert((Read(base,ped+280)&(kProofs|kInvincible))==(kProofs|kInvincible));
  assert(Read16(base,weapon+28)==30 && Read16(base,weapon+30)==40); // God-only leaves ammo alone
  for(double value : {50.0,0.0,-10.0,std::numeric_limits<double>::quiet_NaN()}) {
    ctx=Context(); ctx.f1.f64=value; sub_8247E8A0(ctx,base);
    assert(std::bit_cast<float>(Read(base,ped+484))==100);
  }
  ctx=Context(); ctx.f1.f64=150; sub_8247E8A0(ctx,base);
  assert(std::bit_cast<float>(Read(base,ped+484))==150);
  ctx=Context(npc); ctx.f1.f64=0; sub_8247E8A0(ctx,base);
  assert(Read(base,npc+484)==0 && Read(base,npc+280)==0);
  ctx=Context(7); ctx.r4.s32=0; sub_825BB5F0(ctx,base); assert(script_health_calls==0);
  ctx=Context(0x109); ctx.r4.s32=0; sub_825BB5F0(ctx,base); assert(script_health_calls==1);
  ctx=Context(7); sub_825BBFE0(ctx,base); assert(head_explosion_calls==0);
  // A stale handle cannot be mistaken for the player.
  ctx=Context(6); ctx.r4.s32=0; sub_825BB5F0(ctx,base); assert(script_health_calls==2);
  ctx=Context(7); ctx.r4.s32=200; sub_825BB5F0(ctx,base); assert(script_health_calls==3);

  // Original mission setters cannot turn off protection or enable drowning.
  Write(base,0x9000+8,0x9200); Write(base,0x9200,7); Write(base,0x9204,0);
  ctx=Context(0x9000); sub_825C85E8(ctx,base); assert(Read(base,ped+280)&kInvincible);
  Write(base,0x9200,0); // player-index native, not a ped handle
  ctx=Context(0x9000); sub_825B5E80(ctx,base); assert(Read(base,ped+280)&kInvincible);
  Write(base,0x9200,7);
  ctx=Context(7); ctx.r4.u32=ctx.r5.u32=ctx.r6.u32=ctx.r7.u32=ctx.r8.u32=0;
  sub_825BBC50(ctx,base); assert((Read(base,ped+280)&kProofs)==kProofs);
  Write(base,0x9204,1);
  ctx=Context(0x9000); sub_825C4690(ctx,base);
  ctx=Context(0x9000); sub_825C4710(ctx,base);
  ctx=Context(0x9000); sub_825C7CD0(ctx,base);
  ctx=Context(0x9000); sub_825C90E8(ctx,base);
  ctx=Context(0x9000); sub_825C3C38(ctx,base);
  assert(Read(base,ped+560)&kNoCriticalHits);
  assert(!(Read(base,ped+560)&kDrowningAndInjury));
  ctx=Context(7); sub_825BBFE0(ctx,base); assert(head_explosion_calls==0);
  ctx=Context(0x109); sub_825BBFE0(ctx,base); assert(head_explosion_calls==1);
  // Original NPC setters still take effect, without granting NPC protection.
  Write(base,0x9200,0x109);
  ctx=Context(0x9000); sub_825C4690(ctx,base);
  assert(Read(base,npc+560)&0x10000000);

  // Ammo-only mode never grants invincibility or an unowned weapon.
  Setup(base,ped,weapon,2,0,0); ConfigureGameplay(false,true); BeforePhysics(base);
  assert(Read(base,ped+280)==0 && Read16(base,weapon+28)==0);
  assert(Read16(base,weapon+30)==kReserveFloor);
  assert(Read(base,ped+640+36+3*8)==0 && Read16(base,ped+640+40+3*8)==0);
  // Repeated local-player queries don't rescan/replenish the inventory.
  ctx=Context(0); sub_82238C28(ctx,base);
  Write16(base,ped+640+40+2*8,10);
  ctx=Context(0); sub_82238C28(ctx,base);
  assert(Read16(base,ped+640+40+2*8)==10);
  BeforePhysics(base); assert(Read16(base,ped+640+40+2*8)==kReserveFloor);
  // Full original reload uses weapon-info capacity, without modifying total.
  ctx=Context(weapon); sub_82267588(ctx,base);
  assert(Read16(base,weapon+28)==30 && Read16(base,weapon+30)==kReserveFloor);
  for(unsigned round=0;round<70;++round) {
    for(unsigned shot=0;shot<30;++shot) {
      ctx=Context(weapon); ctx.r4.u32=ped; sub_8226F428(ctx,base);
      assert(ctx.r3.u32==0x55 && ctx.r4.u32==0x66); // original output retained
      assert(Read16(base,weapon+28)==29-shot && Read16(base,weapon+30)==kReserveFloor);
    }
    assert(Read16(base,weapon+28)==0);
    ctx=Context(weapon); sub_82267588(ctx,base); assert(Read16(base,weapon+28)==30);
  }
  Setup(base,npc,npc_weapon,2,40,30);
  ctx=Context(npc_weapon);ctx.r4.u32=npc;sub_8226F428(ctx,base);
  assert(Read16(base,npc_weapon+28)==29 && Read16(base,npc_weapon+30)==39);
  // Normal consumption resumes when disabled; no permanent 25000 sentinel.
  ConfigureGameplay(false,false);ctx=Context(weapon);ctx.r4.u32=ped;sub_8226F428(ctx,base);
  assert(Read16(base,weapon+28)==29 && Read16(base,weapon+30)==kReserveFloor-1);
  // Different slots and magazine sizes, including one-round projectile weapons.
  for(unsigned slot=2;slot<11;++slot) for(unsigned capacity : {1u,6u,17u,30u,50u}) {
    Setup(base,ped,weapon,slot,capacity,capacity);Write16(base,weapon_info+134,capacity);
    ConfigureGameplay(true,true);BeforePhysics(base);
    ctx=Context(weapon);ctx.r4.u32=ped;sub_8226F428(ctx,base);
    assert(Read16(base,weapon+28)==capacity-1 && Read16(base,weapon+30)==kReserveFloor);
    ctx=Context(weapon);sub_82267588(ctx,base);assert(Read16(base,weapon+28)==capacity);
  }
  // Higher existing ammo totals, stale weapon owner, missing/invalid player.
  Setup(base,ped,weapon,2,5000,30);BeforePhysics(base);
  ctx=Context(weapon);ctx.r4.u32=ped;sub_8226F428(ctx,base);assert(Read16(base,weapon+30)==5000);
  Write(base,weapon+20,99);assert(ActiveWeapon(base,ped)==0);BeforePhysics(base);
  ctx=Context(weapon);ctx.r4.u32=ped;sub_8226F428(ctx,base);
  assert(Read16(base,weapon+30)==4999); // mismatched identity never gets preservation
  Write(base,0x82A98778,UINT32_MAX);BeforePhysics(base);assert(Player(base)==0);
  Write(base,0x82A98778,0);denied_address=ped+560;BeforePhysics(base);assert(Player(base)==0);
  denied_address=0;Write(base,info+1400,npc);BeforePhysics(base);
  assert(Read(base,npc+280)&kInvincible); // new local ped publication/respawn
  assert(ResolvePed(base,0x207)==0);assert(ResolvePed(base,UINT32_MAX)==0);
  assert(!Span(base,0xfffffff0,32));
  munmap(base,size);
  std::puts("Gameplay mods: Off/NPC isolation, damage/proofs, stale handles, 2100 shots/70 reloads, 45 slot/capacity cases and respawn passed.");
}
