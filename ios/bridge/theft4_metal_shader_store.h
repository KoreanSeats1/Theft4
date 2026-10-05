#pragma once
#include "theft4_native_metal.h"

namespace theft4::metal {
struct FetchResources {
  // Same game fetch-slot ordering as NativePipelineState::textures. Supply
  // typed dummy images for unused texture kinds declared by a shader array.
  std::array<id<MTLTexture>, 4> images{};
  id<MTLSamplerState> sampler = nil;
};
class ShaderStore {
 public:
  // Owned by the render worker. Resolve hits perform no file or compiler work.
  explicit ShaderStore(Renderer& renderer) : renderer_(renderer) {}
  ShaderStore(const ShaderStore&) = delete;
  ShaderStore& operator=(const ShaderStore&) = delete;
  bool Open(const std::string& directory, std::string& error);
  const ShaderMetadata* Metadata(ShaderKey key, Stage stage) const;
  Shader Resolve(ShaderKey key, Stage stage, uint32_t specialization, std::string& error);
  bool Bind(ShaderKey key, Stage stage, const std::array<FetchResources, 26>& fetches,
            Draw& packet, std::string& error) const;
  size_t LoadedFunctions() const { return functions_.size(); }
  size_t LoadedLibraries() const { return libraries_.size(); }
  size_t CatalogSize() const { return catalog_.Size(); }
 private:
  friend class PlanAdapter;
  bool BindMetadata(const ShaderMetadata&,const std::array<FetchResources,26>&,Draw&,std::string&) const;
  struct FunctionKey {
    ShaderKey shader{};
    uint32_t specialization = 0;
    bool operator==(const FunctionKey&) const = default;
  };
  struct FunctionKeyHash {
    size_t operator()(const FunctionKey& key) const noexcept {
      return ShaderKeyHash{}(key.shader) ^ (uint64_t(key.specialization) * 0x9e3779b97f4a7c15ull);
    }
  };
  Renderer& renderer_;
  ShaderCatalog catalog_;
  std::string directory_;
  std::unordered_map<FunctionKey, Shader, FunctionKeyHash> functions_;
  std::unordered_map<ShaderKey,id<MTLLibrary>,ShaderKeyHash> libraries_;
};
}
