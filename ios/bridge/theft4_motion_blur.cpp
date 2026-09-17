#include "theft4_motion_blur.h"

#include <atomic>
#include <limits>

#include <rex/logging.h>
#include <rex/runtime.h>
#include "gta4_init.h"

namespace {
namespace blur = theft4::motion_blur;
std::atomic<bool> enabled{true};
std::atomic<bool> trace{false};
std::atomic<uint32_t> events{0};

bool Readable(uint8_t* base, uint32_t address, uint32_t size) {
    if (!base || !address || !size) return false;
    const uint64_t last = uint64_t{address} + size - 1;
    if (last > std::numeric_limits<uint32_t>::max()) return false;
    auto* kernel = REX_KERNEL_STATE();
    auto* memory = kernel ? kernel->memory() : nullptr;
    auto* heap = memory ? memory->LookupHeap(address) : nullptr;
    if (!heap || heap != memory->LookupHeap(static_cast<uint32_t>(last))) return false;
    const auto access = heap->QueryRangeAccess(address, static_cast<uint32_t>(last));
    using rex::memory::PageAccess;
    return access == PageAccess::kReadWrite || access == PageAccess::kReadOnly ||
           access == PageAccess::kExecuteReadWrite || access == PageAccess::kExecuteReadOnly;
}
}  // namespace

void theft4::motion_blur::Configure(bool value, bool diagnostics) noexcept {
    enabled.store(value, std::memory_order_relaxed);
    trace.store(diagnostics, std::memory_order_relaxed);
    events.store(0, std::memory_order_relaxed);
}

// The iOS target does not link desktop gta4_presentation_hooks.cpp. Intercept
// this same pass-selection boundary, not a shader binding: the original helper
// installs the selected variant's own shader, constants and sampler layout.
extern "C" void sub_822CF300(PPCContext& ctx, uint8_t* base) {
    const bool use_blur = enabled.load(std::memory_order_relaxed);
    const bool observe = trace.load(std::memory_order_relaxed);
    const uint32_t requested = ctx.r6.u32;
    if (ctx.lr != blur::kCompositeCaller || ctx.r4.u32 != 0 ||
        (use_blur && !observe)) {
        __imp__sub_822CF300(ctx, base);
        return;
    }
    const uint32_t postfx = ctx.r3.u32;
    bool valid = false;
    if (Readable(base, postfx, blur::kTechniqueOffset + sizeof(uint32_t))) {
        const uint32_t effect = REX_LOAD_U32(postfx + 108);
        valid = ctx.r5.u32 != 0 &&
                ctx.r5.u32 == REX_LOAD_U32(postfx + blur::kTechniqueOffset) &&
                Readable(base, effect, 28);
    }
    const uint32_t selected =
        blur::SelectPass(requested, use_blur, static_cast<uint32_t>(ctx.lr), ctx.r4.u32, valid);
    // Opt-in, bounded evidence only; no per-frame retail logging.
    if (observe && events.fetch_add(1, std::memory_order_relaxed) < 32) {
        REXLOG_INFO("Theft4 motion-blur: enabled={} requested={} selected={} valid={}",
                    use_blur, requested, selected, valid);
    }
    if (selected != requested) ctx.r6.u64 = selected;
    __imp__sub_822CF300(ctx, base);
}
