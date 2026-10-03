#include "theft4_texture_manifest.h"

#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/system/lzx.h>
#include <xxhash.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <unordered_set>
extern "C" {
#include <rijndael-alg-fst.h>
}

namespace theft4::astc {
namespace {
using Bytes = std::vector<uint8_t>;
using Json = nlohmann::json;
constexpr uint64_t kResourceLimit = 128ull * 1024 * 1024;
constexpr uint64_t kTocLimit = 32ull * 1024 * 1024;

uint32_t U32(std::span<const uint8_t> b, size_t p, bool be = false) {
  if (p > b.size() || b.size() - p < 4) throw std::runtime_error("Truncated resource field.");
  uint32_t v; std::memcpy(&v, b.data() + p, 4);
  return be ? __builtin_bswap32(v) : v;
}
uint16_t U16(std::span<const uint8_t> b, size_t p, bool be = false) {
  if (p > b.size() || b.size() - p < 2) throw std::runtime_error("Truncated resource field.");
  uint16_t v; std::memcpy(&v, b.data() + p, 2);
  return be ? __builtin_bswap16(v) : v;
}
std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}
bool ResourceExtension(const std::string& name) {
  const auto ext = Lower(std::filesystem::path(name).extension().string());
  return ext == ".xtd" || ext == ".xdr" || ext == ".xdd" || ext == ".xft";
}
bool SourceExtension(const std::filesystem::path& p) {
  const auto ext = Lower(p.extension().string());
  return ResourceExtension(p.string()) || ext == ".rpf" || ext == ".img";
}
Bytes Read(const std::filesystem::path& path, uint64_t offset, uint64_t count,
           uint64_t limit = kResourceLimit) {
  const auto size = std::filesystem::file_size(path);
  if (!count || count > limit || offset > size || count > size - offset)
    throw std::runtime_error("Source extent is outside its file or memory limit.");
  std::ifstream stream(path, std::ios::binary);
  stream.seekg(std::streamoff(offset));
  Bytes result(size_t(count), uint8_t{});
  if (!stream.read(reinterpret_cast<char*>(result.data()), std::streamsize(count)))
    throw std::runtime_error("Cannot read texture source.");
  return result;
}
std::string Name(std::span<const uint8_t> bytes, size_t p, size_t limit = 256) {
  if (p >= bytes.size()) throw std::runtime_error("Name pointer is outside its resource.");
  std::string result;
  for (; p < bytes.size() && result.size() <= limit; ++p) {
    const auto c = bytes[p];
    if (!c) return result;
    if (c < 32 || c > 126) throw std::runtime_error("Invalid texture name.");
    result += char(c);
  }
  throw std::runtime_error("Unterminated texture name.");
}
size_t Pointer(uint32_t address, size_t cpu_size, size_t total) {
  const auto section = address >> 28;
  if (section != 5 && section != 6) throw std::runtime_error("Unsupported resource pointer.");
  const uint64_t p = (address & 0x0FFFFFFF) + (section == 6 ? uint64_t(cpu_size) : 0);
  if (p >= total || (section == 5 && p >= cpu_size))
    throw std::runtime_error("Resource pointer is outside its section.");
  return size_t(p);
}
void Decrypt(Bytes& bytes, std::span<const uint8_t> key) {
  if (key.size() != 32) throw std::runtime_error("Archive AES key is unavailable.");
  std::array<u32, 4 * (MAXNR + 1)> keys{};
  const int rounds = rijndaelKeySetupDec(keys.data(), key.data(), 256);
  std::array<uint8_t, 16> block{};
  for (int pass = 0; pass < 16; ++pass) {
    for (size_t p = 0; p + 16 <= bytes.size(); p += 16) {
      rijndaelDecrypt(keys.data(), rounds, bytes.data() + p, block.data());
      std::memcpy(bytes.data() + p, block.data(), 16);
    }
  }
}
std::vector<std::filesystem::path> Sources(const std::filesystem::path& game) {
  if (std::filesystem::is_symlink(game) || !std::filesystem::is_directory(game))
    throw std::runtime_error("Game directory is unavailable.");
  std::vector<std::filesystem::path> result;
  size_t entries = 0;
  for (auto it = std::filesystem::recursive_directory_iterator(game);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (++entries > 500000) throw std::runtime_error("Too many game directory entries.");
    if (it->is_symlink()) { it.disable_recursion_pending(); continue; }
    if (it->path().filename().string().starts_with('.')) {
      it.disable_recursion_pending(); continue;
    }
    if (it->is_regular_file() && SourceExtension(it->path())) result.push_back(it->path());
  }
  std::sort(result.begin(), result.end());
  return result;
}

struct Resource { Bytes data; size_t cpu = 0; };
Resource Unpack(std::span<const uint8_t> file, const std::atomic<bool>& cancel) {
  const auto magic = U32(file, 0, true);
  const bool be = magic == 0x05435352 || magic == 0x85435352;
  if (!be && magic != 0x52534305 && magic != 0x52534385)
    throw std::runtime_error("Unsupported resource header.");
  const bool extended = (magic & 0xFF) == 0x85 || magic == 0x85435352;
  const auto flags = U32(file, 8, be);
  uint64_t cpu = uint64_t(flags & 0x7FF) << (((flags >> 11) & 15) + 8);
  uint64_t gpu = uint64_t((flags >> 15) & 0x7FF) << (((flags >> 26) & 15) + 8);
  size_t p = extended ? 20 : 16;
  if (extended) {
    const auto flags2 = U32(file, 12, be);
    if (flags2 & 0x80000000) {
      cpu = uint64_t(flags2 & 0x3FFF) << 12;
      gpu = (flags2 >> 2) & 0x3FFF000;
    }
  }
  const uint64_t compressed_size = U32(file, p, true); p += 4;
  if (!cpu || cpu + gpu > kResourceLimit || p > file.size() ||
      compressed_size > file.size() - p) throw std::runtime_error("Invalid resource size.");
  const size_t end = p + size_t(compressed_size);
  Resource resource{Bytes(size_t(cpu + gpu)), size_t(cpu)};
  auto decoder = rex::lzx::PersistentDecoder::Create(1 << 17);
  if (!decoder) throw std::runtime_error("Cannot allocate resource decoder.");
  size_t out = 0;
  while (out < resource.data.size()) {
    if (cancel.load()) throw std::runtime_error("Preparation paused.");
    size_t output_size = 0x8000, input_size;
    if (p >= end) throw std::runtime_error("Truncated LZX frames.");
    if (file[p] == 0xFF) {
      output_size = U16(file, p + 1, true); input_size = U16(file, p + 3, true); p += 5;
    } else { input_size = U16(file, p, true); p += 2; }
    if (!output_size || output_size > 0x8000 || !input_size || p > end ||
        input_size > end - p || output_size > resource.data.size() - out)
      throw std::runtime_error("Invalid LZX frame extent.");
    const auto result = decoder->DecodeFrame(file.subspan(p, input_size),
        std::span<uint8_t>(resource.data).subspan(out, output_size));
    if (!result || result.bytes_written != output_size)
      throw std::runtime_error("Resource LZX decompression failed.");
    p += input_size; out += output_size;
  }
  return resource;
}

bool TextureAt(const Resource& resource, size_t object, Json& metadata,
               Bytes& payload, std::vector<Mip>& mips, BcFormat& format) {
  using namespace rex::graphics;
  const std::span<const uint8_t> data = resource.data;
  if (object + 36 > resource.cpu) return false;
  // GTA IV grcTextureXenon: name +20, D3DBaseTexture +24, width/height +28.
  const auto name_ptr = U32(data, object + 20, true);
  const auto desc_ptr = U32(data, object + 24, true);
  if ((name_ptr >> 28) != 5 || (desc_ptr >> 28) != 5) return false;
  const size_t name_pos = Pointer(name_ptr, resource.cpu, data.size());
  const size_t desc = Pointer(desc_ptr, resource.cpu, data.size());
  if (desc + 52 > resource.cpu) return false;
  const auto name = Name(data.first(resource.cpu), name_pos);
  if (name.empty()) return false;
  xenos::xe_gpu_texture_fetch_t fetch{};
  std::array<uint32_t, 6> words{};
  for (size_t i = 0; i < words.size(); ++i) words[i] = U32(data, desc + 28 + i * 4, true);
  std::memcpy(&fetch, words.data(), sizeof(fetch));
  const auto base = GetBaseFormat(fetch.format);
  if (base == xenos::TextureFormat::k_DXT1) format = BcFormat::kBc1;
  else if (base == xenos::TextureFormat::k_DXT2_3) format = BcFormat::kBc2;
  else if (base == xenos::TextureFormat::k_DXT4_5) format = BcFormat::kBc3;
  else return false;
  if (fetch.type != xenos::FetchConstantType::kTexture ||
      fetch.dimension != xenos::DataDimension::k2DOrStacked || fetch.stacked ||
      !fetch.base_address || fetch.mip_min_level || fetch.mip_max_level > 13)
    return false;
  TextureInfo info{};
  if (!TextureInfo::Prepare(fetch, &info)) return false;
  if (info.width + 1 != U16(data, object + 28, true) ||
      info.height + 1 != U16(data, object + 30, true) ||
      info.width >= 8192 || info.height >= 8192 ||
      info.mip_max_level >= info.GetMaxMipLevels()) return false;
  const auto* fi = info.format_info();
  const uint32_t block_bytes = fi->bytes_per_block();
  payload.clear(); mips.clear();
  for (uint32_t level = 0; level <= info.mip_max_level; ++level) {
    uint32_t width, height, ox, oy;
    info.GetMipSize(level, &width, &height);
    const auto extent = info.GetMipExtent(level, true);
    const auto host = TextureExtent::Calculate(fi, width, height, 1, false, false);
    const auto address = info.GetMipLocation(level, &ox, &oy, true);
    const size_t source = Pointer(address, resource.cpu, data.size());
    const uint64_t size = uint64_t(host.visible_blocks()) * block_bytes;
    if (payload.size() + size > kResourceLimit) throw std::runtime_error("Texture is too large.");
    const size_t start = payload.size(); payload.resize(start + size_t(size));
    for (uint32_t y = 0; y < host.block_height; ++y) {
      for (uint32_t x = 0; x < host.block_width; ++x) {
        const int64_t offset = info.is_tiled
          ? texture_util::GetTiledOffset2D(ox + x, oy + y, extent.block_pitch_h,
                                          std::countr_zero(block_bytes))
          : int64_t((uint64_t(oy + y) * extent.block_pitch_h + ox + x) * block_bytes);
        if (offset < 0 || uint64_t(source) + uint64_t(offset) + block_bytes > data.size())
          throw std::runtime_error("Texture block is outside the resource.");
        texture_conversion::CopySwapBlock(info.endianness,
          payload.data() + start + (size_t(y) * host.block_pitch_h + x) * block_bytes,
          data.data() + source + size_t(offset), block_bytes);
      }
    }
    mips.push_back({level, width, height, 1, 0, 1,
                   host.block_pitch_h * fi->block_width, host.block_height * fi->block_height,
                   start, size_t(size)});
  }
  const Input input{format, XXH3_64bits(payload.data(), payload.size()), info.width + 1,
                    info.height + 1, payload, mips};
  uint64_t output_bytes = 0;
  for (const auto& mip : mips) output_bytes += uint64_t((mip.width + 3) / 4) * ((mip.height + 3) / 4) * 16;
  metadata.update({{"name", name}, {"objectOffset", object}, {"key", TextureCacheKey(input)},
                   {"sourceFormat", uint32_t(format)}, {"width", input.width},
                   {"height", input.height}, {"mips", mips.size()},
                   {"sourceBytes", payload.size()}, {"astcBytes", output_bytes}});
  return true;
}

struct Entry { std::string name; uint64_t offset, size; };
std::vector<Entry> ArchiveEntries(const std::filesystem::path& path,
                                  std::span<const uint8_t> key) {
  auto header = Read(path, 0, 20);
  const bool rpf = Lower(path.extension().string()) == ".rpf";
  bool encrypted;
  uint32_t count, toc_size; uint64_t toc_offset;
  if (rpf) {
    if (U32(header, 0) != 0x32465052) throw std::runtime_error("Unsupported RPF archive.");
    toc_size = U32(header, 4); count = U32(header, 8); toc_offset = 0x800;
    encrypted = U32(header, 16) != 0;
  } else {
    encrypted = U32(header, 0) != 0xA94E2A52;
    if (encrypted) Decrypt(header, key);
    if (U32(header, 0) != 0xA94E2A52 || U32(header, 4) != 3 || U16(header, 16) != 16)
      throw std::runtime_error("Unsupported IMG archive.");
    count = U32(header, 8); toc_size = U32(header, 12); toc_offset = 20;
  }
  if (!count || count > 1000000 || toc_size < uint64_t(count) * 16 || toc_size > kTocLimit)
    throw std::runtime_error("Invalid archive table dimensions.");
  auto toc = Read(path, toc_offset, toc_size, kTocLimit);
  if (encrypted) Decrypt(toc, key);
  std::vector<Entry> result;
  const auto file_size = std::filesystem::file_size(path);
  size_t cursor = size_t(count) * 16;
  // Only names are used for diagnostics; no archive pathname is ever written.
  for (uint32_t i = 0; i < count; ++i) {
    const size_t p = size_t(i) * 16;
    std::string name;
    uint64_t offset, size;
    if (rpf) {
      if (U32(toc, p + 8) & 0x80000000) continue;
      name = Name(toc, size_t(count) * 16 + U32(toc, p));
      const auto flags = U32(toc, p + 12);
      const bool resource = (flags & 0xC0000000) == 0xC0000000;
      if (!resource || !ResourceExtension(name)) continue;
      offset = U32(toc, p + 8) & 0x7FFFFF00; size = U32(toc, p + 4);
    } else {
      name = Name(toc, cursor); cursor += name.size() + 1;
      if (!ResourceExtension(name)) continue;
      const auto allocated = uint64_t(U16(toc, p + 12)) * 0x800;
      const auto padding = U16(toc, p + 14) & 0x7FF;
      if (padding > allocated) throw std::runtime_error("Invalid IMG entry padding.");
      size = (U32(toc, p) & 0xC0000000) ? allocated - padding : U32(toc, p);
      offset = uint64_t(U32(toc, p + 8)) * 0x800;
    }
    if (offset < toc_offset + toc_size || offset > file_size || size > file_size - offset)
      throw std::runtime_error("Archive entry is outside its data section.");
    result.push_back({std::move(name), offset, size});
  }
  return result;
}
}  // namespace

bool DecodeTextureResource(std::span<const uint8_t> file, std::vector<uint8_t>& data,
                           size_t& cpu_size, std::string& error) {
  try {
    const std::atomic<bool> cancel{false};
    auto resource = Unpack(file, cancel);
    cpu_size = resource.cpu; data = std::move(resource.data); return true;
  } catch (const std::exception& failure) { error = failure.what(); return false; }
}

nlohmann::json TextureSourceFingerprint(const std::filesystem::path& game) {
  Json files = Json::array();
  for (const auto& p : Sources(game)) {
    files.push_back({p.lexically_relative(game).generic_string(), std::filesystem::file_size(p),
      std::filesystem::last_write_time(p).time_since_epoch().count()});
  }
  const auto text = files.dump();
  return {{"schema", 1}, {"files", files.size()}, {"digest", XXH3_64bits(text.data(), text.size())}};
}

bool ScanTextureManifest(const std::filesystem::path& game, const std::filesystem::path& aes_key,
                         const TextureVisitor& visitor, const ScanObserver& observer,
                         const std::atomic<bool>& cancel, Json& manifest, std::string& error) {
  try {
    const auto sources = Sources(game);
    Bytes key;
    if (std::filesystem::is_regular_file(aes_key)) key = Read(aes_key, 0, 32, 32);
    manifest = {{"schemaVersion", 1}, {"sourceFingerprint", TextureSourceFingerprint(game)},
                {"textures", Json::array()}, {"warnings", Json::array()}};
    ScanProgress progress{}; progress.total_containers = sources.size();
    std::unordered_set<std::string> unique;
    uint64_t astc_bytes = 0;
    for (const auto& path : sources) {
      if (cancel.load()) throw std::runtime_error("Preparation paused.");
      progress.current_source = path.lexically_relative(game).generic_string();
      if (observer) observer(progress);
      try {
        std::vector<Entry> entries;
        if (ResourceExtension(path.string()))
          entries.push_back({path.filename().string(), 0, std::filesystem::file_size(path)});
        else entries = ArchiveEntries(path, key);
        for (const auto& entry : entries) {
          if (cancel.load()) throw std::runtime_error("Preparation paused.");
          try {
            auto resource = Unpack(Read(path, entry.offset, entry.size), cancel);
            ++progress.resources;
            Bytes payload; std::vector<Mip> mips; BcFormat format{};
            // Also finds validated embedded grcTextureXenon objects in drawables
            // and fragments. Pointer, descriptor, dimensions and block extents
            // all must agree before an object is accepted.
            for (size_t object = 0; object + 36 <= resource.cpu; object += 4) {
              if ((object & 0xFFF) == 0 && cancel.load())
                throw std::runtime_error("Preparation paused.");
              if ((resource.data[object + 20] >> 4) != 5 ||
                  (resource.data[object + 24] >> 4) != 5) continue;
              Json metadata = Json::object();
              bool found = false;
              try { found = TextureAt(resource, object, metadata, payload, mips, format); }
              catch (const std::runtime_error&) { continue; }
              if (!found) continue;
              metadata.update({{"container", progress.current_source}, {"entry", entry.name},
                               {"entryOffset", entry.offset}, {"entryBytes", entry.size}});
              manifest["textures"].push_back(metadata);
              if (manifest["textures"].size() > 250000)
                throw std::length_error("Texture manifest exceeds the installation limit.");
              const Input input{format, XXH3_64bits(payload.data(), payload.size()),
                  metadata["width"].get<uint32_t>(), metadata["height"].get<uint32_t>(), payload, mips};
              if (unique.insert(metadata["key"].get<std::string>()).second) {
                ++progress.textures; astc_bytes += metadata["astcBytes"].get<uint64_t>();
                if (visitor && !visitor(metadata, input)) throw std::runtime_error("Preparation stopped.");
              }
            }
            if (observer) observer(progress);
          } catch (const std::runtime_error& failure) {
            if (cancel.load() || std::string(failure.what()) == "Preparation stopped.") throw;
            manifest["warnings"].push_back({{"container", progress.current_source},
                {"entry", entry.name}, {"error", failure.what()}});
          }
        }
      } catch (const std::runtime_error& failure) {
        if (cancel.load() || std::string(failure.what()) == "Preparation stopped.") throw;
        manifest["warnings"].push_back({{"container", progress.current_source}, {"error", failure.what()}});
      }
      ++progress.containers;
    }
    manifest["uniqueTextures"] = progress.textures;
    manifest["astcPayloadBytes"] = astc_bytes;
    manifest["resourcesRead"] = progress.resources;
    if (observer) observer(progress);
    return true;
  } catch (const std::exception& failure) { error = failure.what(); return false; }
}

bool VisitManifestTextures(const std::filesystem::path& game, const Json& manifest,
                           const TextureVisitor& visitor, const ScanObserver& observer,
                           const std::atomic<bool>& cancel, std::string& error) {
  try {
    if (manifest.at("sourceFingerprint") != TextureSourceFingerprint(game))
      throw std::runtime_error("Game files changed. Scan textures again.");
    std::unordered_set<std::string> seen;
    std::string loaded_id;
    Resource resource;
    ScanProgress progress{};
    progress.total_containers = manifest.at("uniqueTextures").get<size_t>();
    for (const auto& record : manifest.at("textures")) {
      if (cancel.load()) throw std::runtime_error("Preparation paused.");
      const auto expected_key = record.at("key").get<std::string>();
      if (!seen.insert(expected_key).second) continue;
      const std::filesystem::path relative = record.at("container").get<std::string>();
      if (relative.is_absolute() || relative != relative.lexically_normal() ||
          relative.empty()) throw std::runtime_error("Invalid manifest source path.");
      for (const auto& part : relative) if (part == "..")
        throw std::runtime_error("Invalid manifest source path.");
      auto path = game;
      for (const auto& part : relative) {
        path /= part;
        if (std::filesystem::is_symlink(path)) throw std::runtime_error("Symbolic-link texture source.");
      }
      const auto offset = record.at("entryOffset").get<uint64_t>();
      const auto bytes = record.at("entryBytes").get<uint64_t>();
      const auto id = path.string() + ':' + std::to_string(offset) + ':' + std::to_string(bytes);
      if (loaded_id != id) { resource = Unpack(Read(path, offset, bytes), cancel); loaded_id = id; }
      Json actual = Json::object(); Bytes payload; std::vector<Mip> mips; BcFormat format;
      if (!TextureAt(resource, record.at("objectOffset").get<size_t>(), actual, payload, mips, format) ||
          actual.at("key") != expected_key)
        throw std::runtime_error("Texture source no longer matches its manifest.");
      const Input input{format, XXH3_64bits(payload.data(), payload.size()),
        actual.at("width").get<uint32_t>(), actual.at("height").get<uint32_t>(), payload, mips};
      if (!visitor(record, input)) throw std::runtime_error("Preparation stopped.");
      ++progress.textures; progress.containers = progress.textures;
      progress.current_source = record.at("name").get<std::string>();
      if (observer) observer(progress);
    }
    if (TextureSourceFingerprint(game) != manifest.at("sourceFingerprint"))
      throw std::runtime_error("Game files changed during preparation. Scan textures again.");
    return true;
  } catch (const std::exception& failure) { error = failure.what(); return false; }
}
}  // namespace theft4::astc
