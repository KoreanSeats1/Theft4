#include "theft4_metal_shader_store.h"
#include <filesystem>
#include <fstream>

namespace theft4::metal {
namespace {
MTLTextureType TextureType(FetchKind kind) {
  switch(kind) {
    case FetchKind::Texture2D: return MTLTextureType2D;
    case FetchKind::Texture2DArray: return MTLTextureType2DArray;
    case FetchKind::Texture3D: return MTLTextureType3D;
    case FetchKind::TextureCube: return MTLTextureTypeCube;
    default: return MTLTextureType1D;  // Samplers never enter this path.
  }
}
}
bool ShaderStore::Open(const std::string& directory, std::string& error) {
  std::ifstream input(std::filesystem::path(directory) / "manifest.tsv", std::ios::binary | std::ios::ate);
  const auto size = input ? input.tellg() : std::streampos(-1);
  if (size <= 0 || size > 8 * 1024 * 1024) {
    error = "Missing or oversized offline Metal shader manifest"; return false;
  }
  std::string manifest(size_t(size), '\0'); input.seekg(0);
  if (!input.read(manifest.data(), std::streamsize(size))) {
    error = "Unable to read offline Metal shader manifest"; return false;
  }
  ShaderCatalog catalog;
  if (!catalog.Parse(manifest, error)) return false;
  catalog_ = std::move(catalog); directory_ = directory; functions_.clear();libraries_.clear(); return true;
}
const ShaderMetadata* ShaderStore::Metadata(ShaderKey key, Stage stage) const {
  return catalog_.Find(key, stage);
}
Shader ShaderStore::Resolve(ShaderKey key, Stage stage, uint32_t specialization, std::string& error) {
  const auto* metadata = Metadata(key, stage);
  if (!metadata) { error = "Game shader hash/stage/variant is absent from the Metal catalog"; return {}; }
  FunctionKey function{key, metadata->Specialization(specialization)};
  if (auto found = functions_.find(function); found != functions_.end()) {
    error.clear(); return found->second;
  }
  ShaderInterface interface{};
  std::copy(metadata->constant_bytes.begin(),metadata->constant_bytes.end(),interface.constant_bytes.begin());
  for (const auto& binding : metadata->bindings) {
    if (binding.kind == FetchKind::Sampler) interface.samplers |= 1u << binding.index;
    else {
      interface.textures |= 1u << binding.index;
      interface.texture_types[binding.index] = TextureType(binding.kind);
    }
  }
  auto library=libraries_.find(key);
  if(library==libraries_.end()) {
    NSString* path = [NSString stringWithUTF8String:
        (std::filesystem::path(directory_) / (key.Name() + ".metallib")).c_str()];
    NSDictionary* attributes = [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil];
    const auto size = [attributes[NSFileSize] unsignedLongLongValue];
    if (!size || size > 16 * 1024 * 1024) { error = "Missing or oversized offline Metal shader library"; return {}; }
    NSData* bytes = [NSData dataWithContentsOfFile:path];
    if (!bytes || bytes.length != size) { error = "Unable to read offline Metal shader library"; return {}; }
    auto loaded=renderer_.LoadLibrary({static_cast<const uint8_t*>(bytes.bytes),bytes.length},error);
    if(!loaded)return {};library=libraries_.emplace(key,loaded).first;
  }
  auto shader = renderer_.LoadShader(library->second,
      stage, interface, function.specialization, error);
  if (!shader.function) return {};
  functions_.emplace(function, shader); error.clear(); return shader;
}
bool ShaderStore::Bind(ShaderKey key, Stage stage, const std::array<FetchResources, 26>& fetches,
                       Draw& packet, std::string& error) const {
  const auto* metadata = Metadata(key, stage);
  if (!metadata) { error = "Unknown Metal shader resource interface"; return false; }
  return BindMetadata(*metadata,fetches,packet,error);
}
bool ShaderStore::BindMetadata(const ShaderMetadata& metadata,const std::array<FetchResources,26>& fetches,
                              Draw& packet,std::string& error) const {
  const auto stage=metadata.stage;
  for (const auto& binding : packet.textures) if (binding.stage == stage) {
    error = "The draw already contains texture bindings for this stage"; return false;
  }
  for (const auto& binding : packet.samplers) if (binding.stage == stage) {
    error = "The draw already contains sampler bindings for this stage"; return false;
  }
  size_t texture_count=0,sampler_count=0;
  // Validate the entire interface before appending. A missing final binding
  // must leave preceding stages untouched; no temporary owning vectors needed.
  for (const auto& binding : metadata.bindings) {
    const auto& resource = fetches[binding.slot];
    if (binding.kind == FetchKind::Sampler) {
      if (!resource.sampler) { error = "Missing game sampler for a Metal fetch slot"; return false; }
      ++sampler_count;
    } else {
      auto texture = resource.images[size_t(binding.kind)];
      if (!texture || texture.textureType != TextureType(binding.kind)) {
        error = "Missing or incompatible game texture for a Metal fetch slot"; return false;
      }
      ++texture_count;
    }
  }
  packet.textures.reserve(packet.textures.size()+texture_count);
  packet.samplers.reserve(packet.samplers.size()+sampler_count);
  for(const auto& binding:metadata.bindings) {
    const auto& resource=fetches[binding.slot];
    if(binding.kind==FetchKind::Sampler)packet.samplers.push_back({stage,binding.index,resource.sampler});
    else packet.textures.push_back({stage,binding.index,resource.images[size_t(binding.kind)]});
  }
  error.clear(); return true;
}
}
