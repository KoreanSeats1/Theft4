#pragma once
// Runtime metadata from the offline shader exporter. No SPIR-V or Vulkan types.
#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace theft4::metal {
enum class Stage { Vertex, Fragment };
enum class NumericType { Float, SignedInteger, UnsignedInteger };
enum class FetchKind : uint32_t { Texture2D, Texture2DArray, Texture3D, TextureCube, Sampler };
struct ShaderKey {
  uint64_t hash = 0;
  bool late = false;
  bool negative_one_to_one = false;
  bool operator==(const ShaderKey&) const = default;
  std::string Name() const;
};
struct ShaderKeyHash {
  size_t operator()(const ShaderKey& key) const noexcept;
};
struct ShaderInput {
  uint32_t location = 0;
  NumericType type = NumericType::Float;
  uint32_t components = 0;
  bool operator==(const ShaderInput&) const = default;
};
struct ShaderFetchBinding {
  FetchKind kind = FetchKind::Texture2D;
  uint32_t slot = 0, index = 0;
  bool operator==(const ShaderFetchBinding&) const = default;
};
struct ShaderMetadata {
  ShaderKey key{};
  Stage stage = Stage::Vertex;
  uint32_t used_texture_mask = 0, specialization_mask = 0;
  std::string filename;
  std::vector<ShaderInput> inputs;
  std::vector<ShaderFetchBinding> bindings;
  // Conservative read bounds from the executing SPIR-V. Old catalogs retain
  // the full-bank contract; dynamic/unknown reads also use these defaults.
  std::array<uint32_t,3> constant_bytes{4096,3584,1056};
  uint32_t Specialization(uint32_t requested) const { return requested & specialization_mask; }
};
class ShaderCatalog {
 public:
  // Transactional: a rejected manifest leaves the preceding catalog intact.
  bool Parse(std::string_view manifest, std::string& error);
  const ShaderMetadata* Find(ShaderKey key, Stage stage) const;
  size_t Size() const { return entries_.size(); }
 private:
  std::unordered_map<ShaderKey, ShaderMetadata, ShaderKeyHash> entries_;
};
}
