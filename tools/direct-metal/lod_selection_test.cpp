// Differential test against the actual generated title selector and the
// preceding host wrapper. The two included bodies are extracted by CMake.
#include <rex/ppc/context.h>
#include "gta4_lod_selection_policy.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <chrono>
#include <iostream>
#include <limits>
#include <sys/mman.h>

static uint32_t LoadU32(uint8_t* base,uint32_t address) {
  return __builtin_bswap32(*reinterpret_cast<volatile uint32_t*>(base+address));
}
static void StoreU32(uint8_t* base,uint32_t address,uint32_t value) {
  *reinterpret_cast<volatile uint32_t*>(base+address)=__builtin_bswap32(value);
}
#define DEFINE_REX_FUNC(name) void __imp__sub_824F3418(PPCContext& ctx,uint8_t* base)
#define REX_FUNC_PROLOGUE() ((void)0)
#define REX_LOAD_U32(address) LoadU32(base,uint32_t(address))
#define REX_LOAD_U8(address) base[uint32_t(address)]
#define REX_STORE_U32(address,value) StoreU32(base,uint32_t(address),value)
#include "lod-title-reference.inc"

static constexpr uint32_t kHighestLodDrawableOffset=0x40;
static constexpr uint32_t kDefaultLodBlendGlobal=0x82000A34;
static constexpr uint32_t kDistanceScaleOutputGlobal=0x82A931B4;
static bool native_mode=true,gta4_force_highest_lod=false;
static double gta4_lod_selection_distance_scale=1;
#define REXCVAR_GET(name) name
#define REXLOG_INFO(...) ((void)0)
static bool IsNativeMode(){return native_mode;}
static uint64_t NextNativeHookDiagnosticCall(std::atomic<uint64_t>&){return 0;}
static bool ShouldLogNativeHookCall(uint64_t){return false;}
#include "lod-native-hook.inc"

static std::array<uint32_t,3> Outputs(uint8_t* base) {
  return {LoadU32(base,0x20000),LoadU32(base,0x20004),LoadU32(base,0x20008)};
}
static PPCContext Context(double distance) {
  PPCContext ctx{};ctx.r4.u32=0x10000;ctx.r5.u32=0x20000;
  ctx.r6.u32=0x20004;ctx.r7.u32=0x20008;ctx.f1.f64=distance;
  ctx.fpscr.loadFromHost();return ctx;
}
int main() {
  auto* base=static_cast<uint8_t*>(mmap(nullptr,size_t(1)<<32,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANON,-1,0));assert(base!=MAP_FAILED);
  const auto nan=std::numeric_limits<double>::quiet_NaN();
  const auto inf=std::numeric_limits<double>::infinity();
  const std::array distances{-inf,-1.0,0.0,0.1,19.999999,20.0,20.000001,35.0,
      50.0,59.99999,60.0,60.00001,100.0,300.0,inf,nan};
  const std::array<float,8> thresholds{-1,0,20,60,100,1e30f,
      std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()};
  size_t comparisons=0;
  for(double bias:{1.0,1.75})for(float scale:{0.7f,1.0f,3.0f})
  for(float first:thresholds)for(float second:thresholds)
  for(unsigned resident=0;resident<8;++resident)for(unsigned force=0;force<2;++force)
  for(double distance:distances) {
    gta4_lod_selection_distance_scale=bias;
    StoreU32(base,0x10000+64,(resident&1)?0x1000:0);
    StoreU32(base,0x10000+68,(resident&2)?0x2000:0);
    StoreU32(base,0x10000+72,(resident&4)?0x3000:0);
    StoreU32(base,0x10000+80,std::bit_cast<uint32_t>(first));
    StoreU32(base,0x10000+84,std::bit_cast<uint32_t>(second));
    StoreU32(base,kDistanceScaleOutputGlobal,std::bit_cast<uint32_t>(scale));
    StoreU32(base,kDefaultLodBlendGlobal,0x3F800000);base[0x83016A20]=force;
    auto reference=Context(distance);
    if(bias>1&&std::isfinite(distance)&&distance>=0)reference.f1.f64=double(float(distance*bias));
    __imp__sub_824F3418(reference,base);const auto expected=Outputs(base);
    auto candidate=Context(distance);sub_824F3418(candidate,base);
    assert(Outputs(base)==expected);
    assert(std::bit_cast<uint64_t>(candidate.f1.f64)==std::bit_cast<uint64_t>(distance));
    ++comparisons;
  }
  // Invalid default blends use the reference path, preserving IEEE payloads.
  for(uint32_t blend:{0x7F800000u,0x7FC01234u,0x7F801234u}) {
    StoreU32(base,kDefaultLodBlendGlobal,blend);auto a=Context(25);
    __imp__sub_824F3418(a,base);const auto expected=Outputs(base);
    auto b=Context(25);sub_824F3418(b,base);assert(Outputs(base)==expected);
  }
  native_mode=false;auto original=Context(25);__imp__sub_824F3418(original,base);
  const auto expected=Outputs(base);auto passthrough=Context(25);
  sub_824F3418(passthrough,base);assert(Outputs(base)==expected);
  munmap(base,size_t(1)<<32);
  std::cout<<"PASS: "<<comparisons<<" actual-title differential LOD cases, threshold boundaries, "
      "script flags, resident fallback, IEEE inputs, blend fallback and original-mode passthrough\n";
}
