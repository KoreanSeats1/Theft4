#pragma once
#include "theft4_render_plan.h"
#include "theft4_metal_resources.h"
#include "theft4_metal_shader_store.h"
#include <map>
#include <unordered_map>

namespace theft4::metal {
// Render-worker-owned realization of the frontend's immutable CPU contract.
// Shader, PSO, sampler and resource hits do no driver creation or disk reads.
class PlanAdapter {
 public:
  explicit PlanAdapter(Renderer& renderer);
  bool Open(const std::string& libraries, std::string& error);
  bool Prepare(const render::Capture& capture, Draw& draw, std::string& error);
  std::shared_ptr<const Draw> Realize(const std::shared_ptr<const render::Capture>& capture,std::string& error);
  // Patch only reflected bindings of an already prepared immutable draw.
  // GPU-produced textures remain owned by the ordered frame, never uploaded
  // as static CPU images or retained in the immutable source cache.
  bool BindProduced(const render::Capture&,const std::array<id<MTLTexture>,26>&,
                    Draw&,std::string& error) const;
  size_t PipelineCount() const { return pipelines_.size(); }
  size_t SamplerCount() const { return samplers_.size(); }
  ResourceCacheStats ResourceStats() const { return resources_.Stats(); }
  size_t RetireResources();
  static MTLPixelFormat PixelFormat(render::Format format);
 private:
  Renderer& renderer_;
  ShaderStore shaders_;
  ResourceCache resources_;
  std::map<std::pair<render::Pipeline,render::Primitive>,std::shared_ptr<const Pipeline>> pipelines_;
  std::map<render::Sampler,id<MTLSamplerState>> samplers_;
  std::array<id<MTLTexture>,4> dummy_images_{};
  struct Prepared {std::weak_ptr<const render::Capture> owner;std::shared_ptr<const Draw> draw;};
  std::unordered_map<const render::Capture*,Prepared> prepared_;
  std::shared_ptr<const Pipeline> PipelineFor(render::Pipeline pipeline,render::Primitive primitive,std::string& error);
  BufferView BufferFor(const render::Buffer& buffer,std::string& error);
  id<MTLTexture> ImageFor(const render::Image& image,std::string& error);
  id<MTLSamplerState> SamplerFor(const render::Sampler& sampler,std::string& error);
  bool EnsureDummyImages(std::string& error);
};
}
