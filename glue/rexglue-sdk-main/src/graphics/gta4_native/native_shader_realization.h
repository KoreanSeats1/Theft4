#pragma once

#include <cstdint>

namespace rex::graphics::gta4_native {
enum class NativeShaderRealizationFailure : uint32_t {
  kNone, kStockEarly, kStockLate, kOverrideEarly, kOverrideLate,
};

template <typename Handle>
struct NativeShaderRealization {
  Handle stock_early{}, stock_late{}, override_early{}, override_late{};
  NativeShaderRealizationFailure failure = NativeShaderRealizationFailure::kNone;
  bool stock_ready() const { return bool(stock_early); }
};

// Realization begins only after the CPU interfaces and code identities have
// been admitted. Creation/destruction are injected by the backend. Stock is
// atomic; an optional override failure retains stock and owns no override.
template <typename Handle, typename Create, typename Destroy>
NativeShaderRealization<Handle> RealizeNativeShaderVariants(
    bool stock_late, bool override, bool override_late, Create&& create, Destroy&& destroy) {
  using Failure = NativeShaderRealizationFailure;
  NativeShaderRealization<Handle> result;
  result.stock_early = create(0);
  if (!result.stock_early) { result.failure = Failure::kStockEarly; return result; }
  if (stock_late) {
    result.stock_late = create(1);
    if (!result.stock_late) {
      destroy(result.stock_early); result.stock_early = {};
      result.failure = Failure::kStockLate; return result;
    }
  }
  if (!override) return result;
  result.override_early = create(2);
  if (!result.override_early) { result.failure = Failure::kOverrideEarly; return result; }
  if (override_late) {
    result.override_late = create(3);
    if (!result.override_late) {
      destroy(result.override_early); result.override_early = {};
      result.failure = Failure::kOverrideLate;
    }
  }
  return result;
}
}
