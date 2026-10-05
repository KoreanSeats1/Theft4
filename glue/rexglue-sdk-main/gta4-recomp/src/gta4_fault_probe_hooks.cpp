#include "gta4_init.h"

#include <rex/diagnostics/runtime_probe.h>
#include <rex/fault_diagnostics.h>
#include <rex/diagnostics/policy.h>

namespace {
using rex::diagnostics::runtime_probe::Scope;
using rex::diagnostics::runtime_probe::Stage;

// Only register values are copied. In particular, this does not dereference a
// suspect owner/count/record pointer or change guest registers to recover it.
class GuestFaultScope {
 public:
  GuestFaultScope(uint32_t tag, const PPCContext& ctx, const uint8_t* base)
      : active_(rex::diagnostics::IsEnabled(rex::diagnostics::Category::kLogging)) {
    if(!active_)return;
    had_previous_=RexReadCurrentGuestFaultContext(&previous_) != 0;
    const RexGuestFaultContext current{
        tag, ctx.r3.u32, ctx.r29.u32, ctx.r30.u32, ctx.r31.u32,
        static_cast<uint32_t>(ctx.lr), reinterpret_cast<uint64_t>(base)};
    RexSetGuestFaultContext(&current);
  }
  ~GuestFaultScope() {
    if(!active_)return;
    if (had_previous_) {
      RexSetGuestFaultContext(&previous_);
    } else {
      RexClearGuestFaultContext();
    }
  }
  GuestFaultScope(const GuestFaultScope&) = delete;
  GuestFaultScope& operator=(const GuestFaultScope&) = delete;

 private:
  RexGuestFaultContext previous_{};
  bool active_=false,had_previous_=false;
};
}  // namespace

extern "C" void sub_8296E638(PPCContext& ctx, uint8_t* base) {
  // At owner-update entry, r3 is the six-record owner; r31 is still the
  // caller's register value. The context tag distinguishes this from below.
  GuestFaultScope scope(0x8296E638u, ctx, base);
  __imp__sub_8296E638(ctx, base);
}

extern "C" void sub_8296B0E8(PPCContext& ctx, uint8_t* base) {
  if (static_cast<uint32_t>(ctx.lr) != 0x8296E99Cu) {
    __imp__sub_8296B0E8(ctx, base);
    return;
  }
  // Only this call site establishes r31=owner, r30=descending record index,
  // r3=r29=record. Valid index is [0,5], with record=owner+128+176*index.
  // Preserve the raw values, including invalid ones, for the fatal record.
  GuestFaultScope scope(0x8296B0E8u, ctx, base);
  __imp__sub_8296B0E8(ctx, base);
}

// These scopes retain the original guest call and all register side effects.
// Counters are bounded and timing is disabled when the runtime probe is off.
extern "C" void sub_8284BF50(PPCContext& ctx, uint8_t* base) {
  Scope scope(Stage::StreamRequest);
  __imp__sub_8284BF50(ctx, base);
}

extern "C" void sub_82679140(PPCContext& ctx, uint8_t* base) {
  Scope scope(Stage::StreamComplete);
  __imp__sub_82679140(ctx, base);
}

extern "C" void sub_82679A90(PPCContext& ctx, uint8_t* base) {
  Scope scope(Stage::StreamPump);
  __imp__sub_82679A90(ctx, base);
}

extern "C" void sub_82513A10(PPCContext& ctx, uint8_t* base) {
  Scope scope(Stage::StreamUnload);
  __imp__sub_82513A10(ctx, base);
}

extern "C" void gta4_fault_probe_hooks_link_anchor() {}
