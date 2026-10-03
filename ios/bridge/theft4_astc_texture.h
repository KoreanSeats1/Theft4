#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace theft4::astc {

enum class BcFormat : uint32_t { kBc1 = 1, kBc2 = 2, kBc3 = 3 };
enum class OutputFormat : uint32_t { kAstc4x4 = 1, kRgba8 = 2 };

struct Mip {
  uint32_t level = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth = 1;
  uint32_t base_array_layer = 0;
  uint32_t layer_count = 1;
  uint32_t buffer_row_length = 0;
  uint32_t buffer_image_height = 0;
  size_t payload_offset = 0;
  size_t payload_size = 0;
};

struct Input {
  BcFormat format;
  uint64_t content_hash;
  uint32_t width;
  uint32_t height;
  std::span<const uint8_t> payload;
  std::span<const Mip> mips;
};

struct Prepared {
  OutputFormat format = OutputFormat::kAstc4x4;
  std::vector<uint8_t> payload;
  std::vector<Mip> mips;
  bool cache_hit = false;
  bool cache_persisted = false;
};

std::string TextureCacheKey(const Input& input);
// Read cache storage accounting before gameplay. No payloads are decoded or
// converted; this avoids enumerating a prepared installation on its first miss.
bool PrimeRuntimeCache(const std::filesystem::path& root, std::string* error);
// Persistent, bounded budget for a prepared installation. No eviction occurs.
bool SetPreparationCacheBudget(const std::filesystem::path& root, uint64_t bytes,
                               std::string* error);
// Caller must stop preparation/gameplay first. Retains manifest and diagnostics.
bool DeletePreparedCache(const std::filesystem::path& root, std::string* error);

// The input is the renderer's already-untiled, endian-corrected BC payload.
// Unsupported layouts fail closed so the caller can retain its existing path.
bool DecodeToRgba8(const Input& input, Prepared& output, std::string* error);
bool PrepareAstc4x4(const Input& input, const std::filesystem::path& preparation_root,
                    Prepared& output, std::string* error);

// Append a bounded per-installation list of unique textures encountered while
// playing. This records metadata only, never copyrighted texture bytes.
void RecordObservedTexture(const std::filesystem::path& preparation_root,
                           const Input& input, const Prepared* result,
                           const char* outcome, uint64_t elapsed_ms);

}  // namespace theft4::astc
