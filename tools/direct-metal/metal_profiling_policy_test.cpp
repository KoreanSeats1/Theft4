#include "theft4_metal_profiling.h"
#include <cassert>
int main() {
  using theft4::metal::DetailedGpuProfilingRequested;
  for(const auto setting:{"","0","false","2","1 "}) {
    assert(!DetailedGpuProfilingRequested(false,setting));
    assert(!DetailedGpuProfilingRequested(true,setting));
  }
  assert(DetailedGpuProfilingRequested(false,"1"));
  assert(!DetailedGpuProfilingRequested(true,"1"));
}
