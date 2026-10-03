#include "theft4_astc_texture.h"

#include <astcenc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <unordered_map>

namespace theft4::astc {
namespace {

constexpr size_t kMaxMipBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxTextureBytes = 256u * 1024u * 1024u;
constexpr uint64_t kMaxCacheBytes = 1024ull * 1024ull * 1024ull;
constexpr uint64_t kMaximumPreparationBudget = 16ull * 1024ull * 1024ull * 1024ull;
constexpr uint64_t kMinimumFreeBytes = 256ull * 1024ull * 1024ull;
constexpr std::array<char, 8> kMagic{'T', '4', 'A', 'S', 'T', 'C', '0', '1'};

struct CacheHeader {
  char magic[8];
  uint32_t schema = 1;
  uint32_t source_format = 0;
  uint64_t source_hash = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mip_count = 0;
  uint32_t reserved = 0;
  uint64_t payload_size = 0;
  uint64_t payload_checksum = 0;
};

struct CacheMip {
  uint32_t level;
  uint32_t width;
  uint32_t height;
  uint32_t base_array_layer;
  uint64_t offset;
  uint64_t size;
};

struct CacheUsage { uint64_t bytes = 0; uint64_t budget = kMaxCacheBytes; bool valid = false; };
std::mutex cache_mutex;
std::unordered_map<std::string, CacheUsage> cache_usage;

CacheUsage& CacheAccounting(const std::filesystem::path& directory) {
  auto& usage = cache_usage[directory.string()];
  if (usage.valid) return usage;
  usage.bytes = 0; usage.budget = kMaxCacheBytes;
  uint64_t saved_budget = 0;
  std::ifstream budget_file(directory.parent_path() / "cache-budget.txt");
  if (budget_file >> saved_budget && saved_budget >= kMaxCacheBytes &&
      saved_budget <= kMaximumPreparationBudget) usage.budget = saved_budget;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
    if (ec) break;
    if (entry.is_regular_file(ec)) usage.bytes += entry.file_size(ec);
    if (ec) break;
  }
  usage.valid = !ec;
  return usage;
}

void SetError(std::string* error, const char* message) {
  if (error) *error = message;
}

uint64_t Checksum(std::span<const uint8_t> bytes) {
  uint64_t hash = 14695981039346656037ull;
  for (uint8_t byte : bytes) hash = (hash ^ byte) * 1099511628211ull;
  return hash;
}

std::string CacheKey(const Input& input) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << input.content_hash
      << '-' << uint32_t(input.format) << '-' << std::dec << input.width << 'x'
      << input.height << '-' << input.mips.size();
  return out.str();
}

bool ValidInput(const Input& input, std::string* error) {
  if (input.format != BcFormat::kBc1 && input.format != BcFormat::kBc2 &&
      input.format != BcFormat::kBc3) {
    SetError(error, "Only BC1, BC2, and BC3 are supported.");
    return false;
  }
  if (!input.width || !input.height || input.mips.empty() || input.mips.size() > 128 ||
      input.payload.empty() || input.payload.size() > kMaxTextureBytes) {
    SetError(error, "Texture dimensions or payload are outside the preparation limit.");
    return false;
  }
  const uint32_t block_bytes = input.format == BcFormat::kBc1 ? 8 : 16;
  for (const Mip& mip : input.mips) {
    if (!mip.width || !mip.height || !mip.depth || mip.width > 8192 ||
        mip.height > 8192 || mip.depth > 256 || mip.layer_count != 1 ||
        uint64_t(mip.width) * mip.height * mip.depth * 4 > kMaxMipBytes ||
        mip.payload_offset > input.payload.size() ||
        mip.payload_size > input.payload.size() - mip.payload_offset) {
      SetError(error, "Unsupported mip layout or size.");
      return false;
    }
    const uint32_t block_width = (mip.width + 3) / 4;
    const uint32_t block_height = (mip.height + 3) / 4;
    const uint32_t row_blocks = mip.buffer_row_length
                                    ? mip.buffer_row_length / 4
                                    : block_width;
    const uint32_t rows_per_slice = mip.buffer_image_height
                                        ? mip.buffer_image_height / 4
                                        : block_height;
    const uint64_t last_block_end =
        (uint64_t(mip.depth - 1) * rows_per_slice * row_blocks +
         uint64_t(block_height - 1) * row_blocks + block_width) * block_bytes;
    if (row_blocks < block_width || rows_per_slice < block_height ||
        last_block_end > mip.payload_size) {
      SetError(error, "BC mip payload is shorter than its declared row layout.");
      return false;
    }
  }
  return true;
}

std::array<uint8_t, 3> Rgb565(uint16_t value) {
  const uint32_t r = (value >> 11) & 31;
  const uint32_t g = (value >> 5) & 63;
  const uint32_t b = value & 31;
  return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)),
          uint8_t((b << 3) | (b >> 2))};
}

void DecodeBlock(const uint8_t* block, BcFormat format, uint8_t* rgba,
                 uint32_t width, uint32_t height, uint32_t x0, uint32_t y0) {
  const uint8_t* color = block + (format == BcFormat::kBc1 ? 0 : 8);
  const uint16_t c0 = uint16_t(color[0]) | (uint16_t(color[1]) << 8);
  const uint16_t c1 = uint16_t(color[2]) | (uint16_t(color[3]) << 8);
  std::array<std::array<uint8_t, 4>, 4> palette{};
  for (uint32_t component = 0; component < 3; ++component) {
    const auto a = Rgb565(c0);
    const auto b = Rgb565(c1);
    palette[0][component] = a[component];
    palette[1][component] = b[component];
    if (format == BcFormat::kBc1 && c0 <= c1) {
      palette[2][component] = uint8_t((uint32_t(a[component]) + b[component]) / 2);
      palette[3][component] = 0;
    } else {
      palette[2][component] = uint8_t((2u * a[component] + b[component]) / 3);
      palette[3][component] = uint8_t((a[component] + 2u * b[component]) / 3);
    }
  }
  palette[0][3] = palette[1][3] = palette[2][3] = 255;
  palette[3][3] = format == BcFormat::kBc1 && c0 <= c1 ? 0 : 255;
  const uint32_t color_indices = uint32_t(color[4]) | (uint32_t(color[5]) << 8) |
                                 (uint32_t(color[6]) << 16) | (uint32_t(color[7]) << 24);
  std::array<uint8_t, 8> alpha_palette{};
  uint64_t alpha_indices = 0;
  if (format == BcFormat::kBc3) {
    alpha_palette[0] = block[0];
    alpha_palette[1] = block[1];
    if (block[0] > block[1]) {
      for (uint32_t i = 2; i < 8; ++i)
        alpha_palette[i] = uint8_t(((8 - i) * uint32_t(block[0]) +
                                    (i - 1) * uint32_t(block[1])) / 7);
    } else {
      for (uint32_t i = 2; i < 6; ++i)
        alpha_palette[i] = uint8_t(((6 - i) * uint32_t(block[0]) +
                                    (i - 1) * uint32_t(block[1])) / 5);
      alpha_palette[6] = 0;
      alpha_palette[7] = 255;
    }
    for (uint32_t i = 0; i < 6; ++i) alpha_indices |= uint64_t(block[2 + i]) << (8 * i);
  }
  uint64_t bc2_alpha = 0;
  if (format == BcFormat::kBc2) {
    for (uint32_t i = 0; i < 8; ++i) bc2_alpha |= uint64_t(block[i]) << (8 * i);
  }
  for (uint32_t py = 0; py < 4; ++py) {
    for (uint32_t px = 0; px < 4; ++px) {
      const uint32_t x = x0 + px, y = y0 + py, index = py * 4 + px;
      if (x >= width || y >= height) continue;
      const auto& selected = palette[(color_indices >> (2 * index)) & 3];
      uint8_t* destination = rgba + (size_t(y) * width + x) * 4;
      std::memcpy(destination, selected.data(), 4);
      if (format == BcFormat::kBc2) destination[3] = uint8_t(((bc2_alpha >> (4 * index)) & 15) * 17);
      if (format == BcFormat::kBc3) destination[3] = alpha_palette[(alpha_indices >> (3 * index)) & 7];
    }
  }
}

bool DecodeMip(const Input& input, const Mip& mip, std::vector<uint8_t>& rgba) {
  rgba.assign(size_t(mip.width) * mip.height * mip.depth * 4, uint8_t{});
  const uint32_t block_bytes = input.format == BcFormat::kBc1 ? 8 : 16;
  const uint32_t block_width = (mip.width + 3) / 4;
  const uint32_t block_height = (mip.height + 3) / 4;
  const uint32_t row_blocks = mip.buffer_row_length ? mip.buffer_row_length / 4 : block_width;
  const uint32_t rows_per_slice = mip.buffer_image_height
                                      ? mip.buffer_image_height / 4 : block_height;
  const uint8_t* source = input.payload.data() + mip.payload_offset;
  for (uint32_t z = 0; z < mip.depth; ++z) {
    uint8_t* destination = rgba.data() + size_t(z) * mip.width * mip.height * 4;
    for (uint32_t by = 0; by < block_height; ++by) {
      for (uint32_t bx = 0; bx < block_width; ++bx) {
        const size_t block_index =
            (size_t(z) * rows_per_slice + by) * row_blocks + bx;
        DecodeBlock(source + block_index * block_bytes, input.format,
                    destination, mip.width, mip.height, bx * 4, by * 4);
      }
    }
  }
  return true;
}

bool ReadCache(const std::filesystem::path& path, const Input& input, Prepared& output) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return false;
  std::ifstream file(path, std::ios::binary);
  CacheHeader header{};
  file.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!file || std::memcmp(header.magic, kMagic.data(), kMagic.size()) ||
      header.schema != 1 || header.source_format != uint32_t(input.format) ||
      header.source_hash != input.content_hash || header.width != input.width ||
      header.height != input.height || header.mip_count != input.mips.size() ||
      header.payload_size > kMaxTextureBytes) return false;
  const uint64_t expected_size = sizeof(header) +
      uint64_t(header.mip_count) * sizeof(CacheMip) + header.payload_size;
  if (std::filesystem::file_size(path, ec) != expected_size || ec) return false;
  std::vector<CacheMip> disk_mips(header.mip_count);
  file.read(reinterpret_cast<char*>(disk_mips.data()),
            std::streamsize(disk_mips.size() * sizeof(CacheMip)));
  Prepared candidate;
  candidate.format = OutputFormat::kAstc4x4;
  candidate.payload.resize(size_t(header.payload_size));
  file.read(reinterpret_cast<char*>(candidate.payload.data()),
            std::streamsize(candidate.payload.size()));
  if (!file || Checksum(candidate.payload) != header.payload_checksum) return false;
  for (size_t i = 0; i < disk_mips.size(); ++i) {
    const CacheMip& disk = disk_mips[i];
    const Mip& source = input.mips[i];
    const uint64_t expected_mip_bytes =
        uint64_t((source.width + 3) / 4) * ((source.height + 3) / 4) * 16;
    if (disk.level != source.level || disk.width != source.width ||
        disk.height != source.height || disk.base_array_layer != source.base_array_layer ||
        disk.size != expected_mip_bytes || disk.offset > candidate.payload.size() ||
        disk.size > candidate.payload.size() - disk.offset) return false;
    Mip mip = source;
    mip.buffer_row_length = 0;
    mip.buffer_image_height = 0;
    mip.payload_offset = size_t(disk.offset);
    mip.payload_size = size_t(disk.size);
    candidate.mips.push_back(mip);
  }
  candidate.cache_hit = true;
  candidate.cache_persisted = true;
  output = std::move(candidate);
  return true;
}

bool WriteCache(const std::filesystem::path& path, const Input& input,
                const Prepared& prepared) {
  std::lock_guard lock(cache_mutex);
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) return false;
  const auto space = std::filesystem::space(path.parent_path(), ec);
  if (ec || space.available < prepared.payload.size() + kMinimumFreeBytes) return false;
  // The experiment keeps a hard per-installation disk bound. If it is reached,
  // conversion still works for the current session but is not persisted.
  auto& usage = CacheAccounting(path.parent_path());
  if (!usage.valid) return false;
  const uint64_t file_bytes = sizeof(CacheHeader) + prepared.mips.size() * sizeof(CacheMip) +
                              prepared.payload.size();
  const uint64_t old_bytes = std::filesystem::exists(path, ec)
                              ? std::filesystem::file_size(path, ec) : 0;
  if (ec || usage.bytes < old_bytes || file_bytes > usage.budget ||
      usage.bytes - old_bytes > usage.budget - file_bytes) return false;
  CacheHeader header{};
  std::memcpy(header.magic, kMagic.data(), kMagic.size());
  header.source_format = uint32_t(input.format);
  header.source_hash = input.content_hash;
  header.width = input.width;
  header.height = input.height;
  header.mip_count = uint32_t(prepared.mips.size());
  header.payload_size = prepared.payload.size();
  header.payload_checksum = Checksum(prepared.payload);
  const auto temp = path.string() + ".tmp-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (const Mip& mip : prepared.mips) {
      const CacheMip disk{mip.level, mip.width, mip.height, mip.base_array_layer,
                          mip.payload_offset, mip.payload_size};
      file.write(reinterpret_cast<const char*>(&disk), sizeof(disk));
    }
    file.write(reinterpret_cast<const char*>(prepared.payload.data()),
               std::streamsize(prepared.payload.size()));
    file.flush();
    if (!file) {
      std::filesystem::remove(temp, ec);
      return false;
    }
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) { std::filesystem::remove(temp, ec); return false; }
  usage.bytes = usage.bytes - old_bytes + file_bytes;
  return true;
}

}  // namespace

std::string TextureCacheKey(const Input& input) { return CacheKey(input); }

bool PrimeRuntimeCache(const std::filesystem::path& root, std::string* error) {
  std::lock_guard lock(cache_mutex);
  bool valid = false;
  try {
    valid = CacheAccounting(root / "astc-v1").valid;
  } catch (const std::filesystem::filesystem_error&) {
    cache_usage[(root / "astc-v1").string()].valid = false;
  }
  if (!valid) SetError(error, "Cannot inspect texture cache storage.");
  return valid;
}

bool SetPreparationCacheBudget(const std::filesystem::path& root, uint64_t bytes,
                               std::string* error) {
  if (bytes < kMaxCacheBytes || bytes > kMaximumPreparationBudget) {
    SetError(error, "Prepared textures exceed the 16 GiB cache limit."); return false;
  }
  std::lock_guard lock(cache_mutex);
  std::error_code ec;
  std::filesystem::create_directories(root / "astc-v1", ec);
  if (ec) { SetError(error, "Cannot create texture cache."); return false; }
  const auto temp = root / "cache-budget.txt.tmp";
  { std::ofstream file(temp); file << bytes << '\n'; file.flush();
    if (!file) { SetError(error, "Cannot save texture cache budget."); return false; } }
  std::filesystem::rename(temp, root / "cache-budget.txt", ec);
  if (ec) { SetError(error, "Cannot publish texture cache budget."); return false; }
  cache_usage[(root / "astc-v1").string()].valid = false;
  auto& usage = CacheAccounting(root / "astc-v1"); usage.budget = bytes;
  if (!usage.valid) SetError(error, "Cannot inspect texture cache storage.");
  return usage.valid;
}

bool DeletePreparedCache(const std::filesystem::path& root, std::string* error) {
  std::lock_guard lock(cache_mutex);
  std::error_code ec;
  for (const char* name : {"preparation-state.json", "prepared-cache-index.json"}) {
    std::filesystem::remove(root / name, ec);
    if (ec) { SetError(error, "Cannot reset texture preparation status."); return false; }
  }
  std::filesystem::remove_all(root / "astc-v1", ec);
  cache_usage.erase((root / "astc-v1").string());
  if (ec) { SetError(error, "Some prepared textures could not be deleted. Try again before Play."); return false; }
  return true;
}

bool DecodeToRgba8(const Input& input, Prepared& output, std::string* error) {
  if (!ValidInput(input, error)) return false;
  Prepared candidate;
  candidate.format = OutputFormat::kRgba8;
  std::vector<uint8_t> rgba;
  for (const Mip& source : input.mips) {
    DecodeMip(input, source, rgba);
    if (candidate.payload.size() + rgba.size() > kMaxTextureBytes) {
      SetError(error, "Decoded texture exceeds the memory limit.");
      return false;
    }
    Mip mip = source;
    mip.buffer_row_length = 0;
    mip.buffer_image_height = 0;
    mip.payload_offset = candidate.payload.size();
    mip.payload_size = rgba.size();
    candidate.payload.insert(candidate.payload.end(), rgba.begin(), rgba.end());
    candidate.mips.push_back(mip);
  }
  output = std::move(candidate);
  return true;
}

bool PrepareAstc4x4(const Input& input, const std::filesystem::path& preparation_root,
                    Prepared& output, std::string* error) {
  if (!ValidInput(input, error)) return false;
  for (const Mip& mip : input.mips) {
    if (mip.depth != 1) {
      SetError(error, "3D BC textures use the RGBA8 fallback in this experiment.");
      return false;
    }
  }
  const auto path = preparation_root / "astc-v1" / (CacheKey(input) + ".bin");
  if (ReadCache(path, input, output)) return true;

  astcenc_config config{};
  if (astcenc_config_init(ASTCENC_PRF_LDR, 4, 4, 1, ASTCENC_PRE_FAST, 0, &config) !=
      ASTCENC_SUCCESS) {
    SetError(error, "ASTC encoder configuration failed.");
    return false;
  }
  astcenc_context* context = nullptr;
  if (astcenc_context_alloc(&config, 1, &context) != ASTCENC_SUCCESS || !context) {
    SetError(error, "ASTC encoder allocation failed.");
    return false;
  }
  Prepared candidate;
  candidate.format = OutputFormat::kAstc4x4;
  std::vector<uint8_t> rgba;
  const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
  bool success = true;
  for (const Mip& source : input.mips) {
    DecodeMip(input, source, rgba);
    const uint64_t encoded_bytes =
        uint64_t((source.width + 3) / 4) * ((source.height + 3) / 4) * 16;
    if (encoded_bytes > kMaxMipBytes ||
        candidate.payload.size() + encoded_bytes > kMaxTextureBytes) {
      SetError(error, "ASTC texture exceeds the preparation limit.");
      success = false;
      break;
    }
    Mip mip = source;
    mip.buffer_row_length = 0;
    mip.buffer_image_height = 0;
    mip.payload_offset = candidate.payload.size();
    mip.payload_size = size_t(encoded_bytes);
    candidate.payload.resize(candidate.payload.size() + size_t(encoded_bytes));
    void* slice = rgba.data();
    astcenc_image image{source.width, source.height, 1, ASTCENC_TYPE_U8, &slice};
    if (astcenc_compress_image(context, &image, &swizzle,
                              candidate.payload.data() + mip.payload_offset,
                              mip.payload_size, 0) != ASTCENC_SUCCESS) {
      SetError(error, "ASTC compression failed.");
      success = false;
      break;
    }
    candidate.mips.push_back(mip);
    astcenc_compress_reset(context);
  }
  astcenc_context_free(context);
  if (!success) return false;
  candidate.cache_persisted = WriteCache(path, input, candidate);
  output = std::move(candidate);
  return true;
}

void RecordObservedTexture(const std::filesystem::path& preparation_root,
                           const Input& input, const Prepared* result,
                           const char* outcome, uint64_t elapsed_ms) {
  static std::mutex mutex;
  static std::unordered_set<std::string> seen;
  std::lock_guard lock(mutex);
  const std::string key = CacheKey(input);
  if (seen.size() >= 20000 || !seen.insert(key).second) return;
  std::error_code ec;
  std::filesystem::create_directories(preparation_root, ec);
  if (ec) return;
  const auto path = preparation_root / "observed-textures.jsonl";
  if (std::filesystem::exists(path, ec) &&
      std::filesystem::file_size(path, ec) > 8u * 1024u * 1024u) return;
  std::ofstream file(path, std::ios::app);
  if (!file) return;
  file << "{\"schemaVersion\":1,\"key\":\"" << key
       << "\",\"sourceFormat\":" << uint32_t(input.format)
       << ",\"width\":" << input.width << ",\"height\":" << input.height
       << ",\"mips\":" << input.mips.size()
       << ",\"sourceBytes\":" << input.payload.size()
       << ",\"resultBytes\":" << (result ? result->payload.size() : 0)
       << ",\"cacheHit\":" << (result && result->cache_hit ? "true" : "false")
       << ",\"elapsedMs\":" << elapsed_ms
       << ",\"outcome\":\"" << outcome << "\"}\n";
}

}  // namespace theft4::astc
