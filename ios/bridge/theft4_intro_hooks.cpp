#include <atomic>
#include <cstdint>
#include <limits>
#include <span>

#include <rex/logging.h>
#include <rex/runtime.h>

#include "gta4_init.h"
#include "gta4_presentation_policy.h"

namespace {
namespace policy = gta4::presentation::policy;
constexpr uint32_t kActive = 0x831D5335;
constexpr uint32_t kScreenCount = 0x831D5344;
constexpr uint32_t kDefinitions = 0x831D5498;
std::atomic<bool> skip_intro{true};
thread_local bool cold_parser_scope = false;

bool GuestSpan(uint8_t* base, uint32_t address, size_t size, bool writable = false) {
  if (!base || !address || !size || size > std::numeric_limits<uint32_t>::max()) return false;
  const uint64_t last = uint64_t(address) + size - 1;
  if (last > std::numeric_limits<uint32_t>::max()) return false;
  auto* kernel = REX_KERNEL_STATE();
  auto* memory = kernel ? kernel->memory() : nullptr;
  auto* heap = memory ? memory->LookupHeap(address) : nullptr;
  if (!heap || heap != memory->LookupHeap(uint32_t(last))) return false;
  const auto access = heap->QueryRangeAccess(address, uint32_t(last));
  using rex::memory::PageAccess;
  return access == PageAccess::kReadWrite || access == PageAccess::kExecuteReadWrite ||
      (!writable && (access == PageAccess::kReadOnly || access == PageAccess::kExecuteReadOnly));
}

struct ParserScope {
  bool previous = cold_parser_scope;
  explicit ParserScope(bool enabled) { cold_parser_scope = enabled; }
  ~ParserScope() { cold_parser_scope = previous; }
};
}  // namespace

// Referencing this function also retains the strong wrappers in the iOS archive.
extern "C" void theft4_intro_configure(bool enabled) {
  skip_intro.store(enabled, std::memory_order_relaxed);
}

extern "C" void sub_82145420(PPCContext& ctx, uint8_t* base) {
  const bool eligible = skip_intro.load(std::memory_order_relaxed) &&
      policy::IsColdStart(ctx.lr, ctx.r3.u32, ctx.r4.u32) &&
      GuestSpan(base, kActive, 1) && !REX_LOAD_U8(kActive);
  const ParserScope scope(eligible);
  // Preserve initialization, asset loading, audio readiness and screen markers.
  __imp__sub_82145420(ctx, base);
}

extern "C" void sub_82145968(PPCContext& ctx, uint8_t* base) {
  const bool apply = cold_parser_scope && ctx.lr == policy::kParserCaller;
  __imp__sub_82145968(ctx, base);
  if (!apply || !GuestSpan(base, kScreenCount, sizeof(uint32_t))) return;
  const uint32_t count = REX_LOAD_U32(kScreenCount);
  if (!count || count > policy::kMaxScreens ||
      !GuestSpan(base, kDefinitions, size_t(count) * policy::kScreenStride, true)) return;
  const auto plan = policy::CollapseIntro(
      std::span<uint8_t>(base + kDefinitions, size_t(count) * policy::kScreenStride), count, true);
  REXLOG_INFO("Theft4 intro: skip-applied={} records={} prefix={} initialization=retained",
      plan.status == policy::IntroStatus::kReady, count, plan.prefix_count);
}
