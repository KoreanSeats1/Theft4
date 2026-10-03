#include "theft4_metal_shader_catalog.h"
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace theft4::metal {
namespace {
void Require(bool truth, const char* reason) {
  if (!truth) throw std::runtime_error(reason);
}
template<class T> T Number(std::string_view text, int base = 10) {
  T value{};
  auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
  Require(!text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size(),
          "Invalid Metal shader metadata number");
  return value;
}
std::vector<std::string_view> Split(std::string_view text, char separator) {
  std::vector<std::string_view> fields;
  do {
    auto end = text.find(separator);
    fields.push_back(text.substr(0, end));
    if (end == std::string_view::npos) break;
    text.remove_prefix(end + 1);
  } while (true);
  return fields;
}
std::vector<std::string_view> List(std::string_view text) {
  if (text.empty()) return {};
  Require(text.back() == ',', "Metal metadata list must end with a comma");
  auto entries = Split(text.substr(0, text.size() - 1), ',');
  for (auto entry : entries) Require(!entry.empty(), "Empty Metal metadata list entry");
  return entries;
}
ShaderMetadata ParseRow(std::string_view line) {
  Require(line.size() <= 8192, "Metal shader metadata row exceeds its bound");
  auto fields = Split(line, '\t');
  Require(fields.size() == 7, "Unexpected Metal shader manifest schema");
  ShaderMetadata result;
  auto key = fields[0];
  result.key.late = key.ends_with("-late");
  if (result.key.late) key.remove_suffix(5);
  Require(key.size() == 16, "Invalid Metal shader key");
  result.key.hash = Number<uint64_t>(key, 16);
  Require(result.key.hash && result.key.Name() == fields[0], "Noncanonical Metal shader key");
  Require(fields[1] == "vertex" || fields[1] == "fragment", "Invalid Metal shader stage");
  result.stage = fields[1] == "vertex" ? Stage::Vertex : Stage::Fragment;
  Require(!result.key.late || result.stage == Stage::Fragment, "Late vertex shader is invalid");
  result.used_texture_mask = Number<uint32_t>(fields[2]);
  result.specialization_mask = Number<uint32_t>(fields[3]);
  Require(!(result.used_texture_mask & ~0x03ffffffu) &&
          std::popcount(result.used_texture_mask) <= 16, "Unsupported Metal shader fetch mask");
  Require(!fields[4].empty() && fields[4].size() <= 255, "Invalid Metal shader source identity");
  result.filename = fields[4];
  uint32_t locations = 0;
  for (auto input : List(fields[5])) {
    auto parts = Split(input, ':');
    Require(parts.size() == 3, "Invalid Metal shader input");
    const auto location = Number<uint32_t>(parts[0]), type = Number<uint32_t>(parts[1]);
    const auto components = Number<uint32_t>(parts[2]);
    // Schema 1 uses the vendored offline SPIRV-Cross scalar codes. These three
    // correspond to float/int/uint; unsupported types reject before GPU use.
    Require(location < 31 && !(locations & (1u << location)) && components >= 1 &&
            components <= 4 && (type == 13 || type == 7 || type == 8),
            "Unsupported or repeated Metal shader input");
    locations |= 1u << location;
    result.inputs.push_back({location, type == 13 ? NumericType::Float :
        type == 7 ? NumericType::SignedInteger : NumericType::UnsignedInteger, components});
  }
  uint32_t textures = 0, samplers = 0;
  std::array<uint32_t, 5> slots{};
  std::array<uint32_t, 5> bases;
  bases.fill(std::numeric_limits<uint32_t>::max());
  for (auto binding : List(fields[6])) {
    auto parts = Split(binding, ':');
    Require(parts.size() == 3, "Invalid Metal shader binding");
    const auto kind = Number<uint32_t>(parts[0]), slot = Number<uint32_t>(parts[1]);
    const auto index = Number<uint32_t>(parts[2]);
    Require(kind < 5 && slot < 26 && (result.used_texture_mask & (1u << slot)) &&
            !(slots[kind] & (1u << slot)), "Unreflected or repeated Metal fetch slot");
    auto& indices = kind == 4 ? samplers : textures;
    Require(index < (kind == 4 ? 16u : 31u) && !(indices & (1u << index)),
            "Invalid or repeated Metal resource index");
    indices |= 1u << index;
    const uint32_t rank = std::popcount(result.used_texture_mask & ((1u << slot) - 1));
    Require(index >= rank, "Metal fetch index precedes its compact rank");
    if (bases[kind] == std::numeric_limits<uint32_t>::max()) bases[kind] = index - rank;
    Require(index == bases[kind] + rank && (kind != 4 || bases[kind] == 0),
            "Metal fetch array does not preserve compact slot ordering");
    slots[kind] |= 1u << slot;
    result.bindings.push_back({FetchKind(kind), slot, index});
  }
  for (uint32_t mask : slots) Require(!mask || mask == result.used_texture_mask,
                                     "Incomplete Metal resource array");
  Require(textures == ((1u << std::popcount(textures)) - 1) &&
          samplers == ((1u << std::popcount(samplers)) - 1),
          "Metal resource indices are not compact");
  Require((!result.used_texture_mask && !textures && !samplers) ||
          (result.used_texture_mask && textures && slots[4] == result.used_texture_mask),
          "Incomplete Metal texture/sampler contract");
  return result;
}
}
std::string ShaderKey::Name() const {
  std::ostringstream text;
  text << std::hex << std::setw(16) << std::setfill('0') << hash;
  if (late) text << "-late";
  return text.str();
}
size_t ShaderKeyHash::operator()(const ShaderKey& key) const noexcept {
  uint64_t value = key.hash ^ (key.late ? 0x9e3779b97f4a7c15ull : 0);
  value ^= value >> 33; value *= 0xff51afd7ed558ccdull; value ^= value >> 33;
  return size_t(value);
}
bool ShaderCatalog::Parse(std::string_view manifest, std::string& error) {
  try {
    Require(!manifest.empty() && manifest.size() <= 8 * 1024 * 1024,
            "Invalid Metal shader manifest size");
    decltype(entries_) parsed;
    for (auto line : Split(manifest, '\n')) {
      if (line.empty()) continue;
      auto entry = ParseRow(line);
      Require(parsed.emplace(entry.key, std::move(entry)).second, "Duplicate Metal shader key");
      Require(parsed.size() <= 65536, "Too many Metal shader manifest entries");
    }
    Require(!parsed.empty(), "Empty Metal shader catalog");
    for (const auto& [key, entry] : parsed) if (key.late) {
      const auto early = parsed.find({key.hash, false});
      Require(early != parsed.end(), "Late Metal shader has no early variant");
      const auto& base = early->second;
      Require(base.stage == entry.stage && base.filename == entry.filename &&
              base.specialization_mask == entry.specialization_mask &&
              base.used_texture_mask == entry.used_texture_mask &&
              base.inputs == entry.inputs && base.bindings == entry.bindings,
              "Metal shader variants disagree about their interface");
    }
    entries_.swap(parsed); error.clear(); return true;
  } catch (const std::exception& reason) { error = reason.what(); return false; }
}
const ShaderMetadata* ShaderCatalog::Find(ShaderKey key, Stage stage) const {
  const auto found = entries_.find(key);
  return found != entries_.end() && found->second.stage == stage ? &found->second : nullptr;
}
}
