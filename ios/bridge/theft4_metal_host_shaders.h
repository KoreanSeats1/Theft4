#pragma once
#include "theft4_native_metal.h"
#include <map>

namespace theft4::metal {
struct HostBinding {
  uint32_t binding=0,texture_index=0,sampler_index=0;
  MTLTextureType type=MTLTextureType2D;
};
struct HostShaderMetadata {
  Stage stage=Stage::Fragment;
  bool resolve_specialization=false;
  bool present_specialization=false;
  bool depth_specialization=false;
  size_t constant_bytes=0;
  std::vector<HostBinding> textures;
};
struct HostInput { uint32_t binding;id<MTLTexture> texture;id<MTLSamplerState> sampler; };
// Separate ABI from guest draw shaders. Libraries are translated and compiled
// offline; lookup hits perform no file IO or compilation during encoding.
class HostShaderStore {
 public:
  explicit HostShaderStore(Renderer& r):renderer_(r){}
  bool Open(const std::string& directory,std::string& error);
  Shader Resolve(const std::string& name,std::string& error,std::span<const uint32_t> constants={});
  const HostShaderMetadata* Metadata(const std::string& name) const;
  bool Bind(const std::string& name,std::span<const HostInput> inputs,
            const BufferView& constants,Draw& draw,std::string& error) const;
  size_t Size() const{return catalog_.size();}
  size_t LoadedFunctions() const{return functions_.size();}
  size_t LoadedLibraries() const{return libraries_.size();}
 private:
  Renderer& renderer_;
  std::string directory_;
  std::map<std::string,HostShaderMetadata> catalog_;
  std::map<std::string,Shader> functions_;
  std::map<std::string,id<MTLLibrary>> libraries_;
};
}
