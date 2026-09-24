#include "frame_constant_arena.h"

#include <array>
#include <cassert>
#include <cstdint>

using rex::graphics::gta4_native::FrameGenerationMap;

int main() {
  std::array<uint64_t, 4096> versions{};
  FrameGenerationMap<const uint64_t*, uint8_t> seen;
  for (const auto& version : versions) {
    const auto first = seen.Insert(&version, uint8_t{1});
    assert(first && first.inserted);
    const auto repeated = seen.Insert(&version, uint8_t{1});
    assert(repeated && !repeated.inserted);
  }
  assert(seen.size() == versions.size());
  const size_t retained_buckets = seen.bucket_count();
  assert(seen.ResetGeneration());
  assert(seen.size() == 0);
  assert(seen.Find(&versions[0]) == nullptr);
  for (const auto& version : versions) {
    const auto fresh = seen.Insert(&version, uint8_t{1});
    assert(fresh && fresh.inserted);
  }
  assert(seen.size() == versions.size());
  assert(seen.bucket_count() == retained_buckets);
}
