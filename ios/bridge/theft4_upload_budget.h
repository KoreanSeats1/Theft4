#pragma once
#include <cstddef>
#include <cstdint>
namespace theft4::metal {
// Preserve the allocation ceiling on older and memory-constrained devices.
// The measured M5 geometry set exceeds the old 128 MiB cache; give high-memory
// BC-capable devices enough bounded residency to avoid constant reuploads.
constexpr size_t ImmutableUploadBudget(uint64_t physical_memory, bool bc_textures) {
  return bc_textures && physical_memory >= 10ull * 1024 * 1024 * 1024
      ? size_t(384) * 1024 * 1024 : size_t(128) * 1024 * 1024;
}
}
