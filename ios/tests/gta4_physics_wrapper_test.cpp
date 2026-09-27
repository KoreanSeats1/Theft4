// Compile the actual wrapper implementation with the actual PPC context and
// generated declarations. Only the original game bodies and log sink are stubs.
#include "../../glue/rexglue-sdk-main/gta4-recomp/generated/gta4_init.h"
#include <rex/logging.h>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

static std::atomic<unsigned> log_calls{0};
template <typename... Args> void CaptureClampLog(Args&&...) { ++log_calls; }
#undef REXLOG_WARN
#define REXLOG_WARN(...) CaptureClampLog(__VA_ARGS__)
#include "../../glue/rexglue-sdk-main/gta4-recomp/src/gta4_physics_hooks.cpp"

namespace {
struct Observation { int stage; std::uint64_t input_bits; std::uint32_t depth; };
thread_local std::vector<Observation> observed;
thread_local double next_input=0;
thread_local unsigned original_outer_calls=0;
thread_local bool recurse=false;
thread_local bool throw_from_original=false;
thread_local unsigned original_depth=0;
constexpr std::uint64_t kUntouched=UINT64_C(0x123456789abcdef0);
std::uint8_t* expected_base=reinterpret_cast<std::uint8_t*>(UINT64_C(0x1000));
void Observe(int stage,PPCContext& ctx,std::uint8_t* base) {
  assert(base==expected_base);
  assert(ctx.r3.u64==19 && ctx.r4.u64==7 && ctx.f2.u64==kUntouched);
  observed.push_back({stage,ctx.f1.u64,gta4::physics::update_depth});
  // Original side effects must survive the wrapper's return.
  ctx.r12.u64=std::uint64_t(stage);
}
void Prepare(PPCContext& ctx,double input) {
  ctx.f1.f64=input;
  ctx.r3.u64=19;
  ctx.r4.u64=7;
  ctx.f2.u64=kUntouched;
}
}

extern "C" void __imp__sub_82476B58(PPCContext& ctx,uint8_t* base) { Observe(1,ctx,base); }
extern "C" void __imp__sub_82476DA0(PPCContext& ctx,uint8_t* base) { Observe(2,ctx,base); }
extern "C" void __imp__sub_82477920(PPCContext& ctx,uint8_t* base) { Observe(3,ctx,base); }
extern "C" void __imp__sub_824797C0(PPCContext& ctx,uint8_t* base) {
  ++original_outer_calls;
  if (throw_from_original) throw 42;
  ++original_depth;
  if(recurse && original_depth==1) sub_824797C0(ctx,base);
  Prepare(ctx,next_input); sub_82476B58(ctx,base); assert(ctx.r12.u64==1);
  Prepare(ctx,next_input); sub_82476DA0(ctx,base); assert(ctx.r12.u64==2);
  Prepare(ctx,next_input); sub_82477920(ctx,base); assert(ctx.r12.u64==3);
  --original_depth;
}

int main(int argc,char** argv) {
  assert(argc==2);
  const bool enabled=std::strcmp(argv[1],"on")==0;
  const char* configured=std::getenv("THEFT4_PHYSICS_TIMESTEP_GUARD");
  assert(enabled ? !configured : (configured && std::strcmp(configured,"0")==0));
  PPCContext ctx{};
  auto run=[&](double input) {
    observed.clear(); original_outer_calls=0; next_input=input;
    const auto host_fpcr=rex::platform::FPSCRPlatform::getcsr();
    const auto guest_fpscr=ctx.fpscr;
    sub_824797C0(ctx,expected_base);
    assert(rex::platform::FPSCRPlatform::getcsr()==host_fpcr);
    assert(std::memcmp(&ctx.fpscr,&guest_fpscr,sizeof(guest_fpscr))==0);
    assert(original_outer_calls==1 && observed.size()==3);
    assert(!gta4::physics::GuardActive());
    auto expected=enabled ? gta4::physics::BoundTimeStep(input).value : input;
    for(unsigned i=0;i<3;++i) {
      assert(observed[i].stage==int(i+1));
      assert(observed[i].input_bits==std::bit_cast<std::uint64_t>(expected));
      assert(observed[i].depth==(enabled ? 1u:0u));
    }
  };
  for(double input : {0.0,-0.0,1.0/10000.0,double(1.0f/60.0f),double(1.0f/48.0f),
                      gta4::physics::kMaximumTimeStep}) run(input);
  assert(log_calls==0);
  for(double input : {0.075,-0.1,std::bit_cast<double>(UINT64_C(0x7ff8000000000001))}) run(input);
  assert(enabled ? log_calls>0 : log_calls==0);
  // A direct stage invocation outside the original outer update is unchanged.
  observed.clear();
  Prepare(ctx,0.150); sub_82476B58(ctx,expected_base);
  Prepare(ctx,0.150); sub_82476DA0(ctx,expected_base);
  Prepare(ctx,0.150); sub_82477920(ctx,expected_base);
  for(const auto& call:observed) {
    assert(call.input_bits==std::bit_cast<std::uint64_t>(0.150) && call.depth==0);
  }
  // Nested outer calls retain their own scope and restore the parent's depth.
  observed.clear(); original_outer_calls=0; next_input=0.075; recurse=true;
  sub_824797C0(ctx,expected_base); recurse=false;
  assert(original_outer_calls==2 && observed.size()==6 && !gta4::physics::GuardActive());
  for(unsigned i=0;i<6;++i) assert(observed[i].depth==(enabled ? (i<3 ? 2u:1u):0u));
  throw_from_original=true;
  try { sub_824797C0(ctx,expected_base); assert(false); } catch(int value) { assert(value==42); }
  throw_from_original=false;
  assert(!gta4::physics::GuardActive());
  // Other threads do not inherit an active physics scope.
  {
    gta4::physics::PhysicsUpdateScope local(enabled);
    std::thread other([&] {
      PPCContext other_ctx{};
      Prepare(other_ctx,0.15); sub_82476B58(other_ctx,expected_base);
      assert(observed.back().input_bits==std::bit_cast<std::uint64_t>(0.15));
      next_input=0.075; observed.clear();
      sub_824797C0(other_ctx,expected_base);
      assert(observed.size()==3 && !gta4::physics::GuardActive());
    });
    other.join();
    assert(gta4::physics::update_depth==(enabled ? 1u:0u));
  }
  std::cout << "actual physics wrappers: mode=" << (enabled?"on":"off")
            << " originals/ABI/FPSCR/nesting/TLS/exception checks passed\n";
}
