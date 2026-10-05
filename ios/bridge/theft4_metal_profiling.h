#pragma once
#include "theft4_retail_mode.h"
#include <string_view>
namespace theft4::metal {
// Detailed GPU counters change the measured workload. Normal long captures
// retain CPU timing and Metal command-buffer GPU envelopes without counters.
constexpr bool DetailedGpuProfilingRequested(bool retail,std::string_view setting) noexcept {
  return !retail&&setting=="1";
}
inline bool DetailedGpuProfilingEnabled() noexcept {
  static const bool enabled=[] {
    const auto* setting=getenv("THEFT4_METAL_PASS_PROFILING");
    return DetailedGpuProfilingRequested(theft4_retail_mode(),setting?setting:"");
  }();
  return enabled;
}
}
