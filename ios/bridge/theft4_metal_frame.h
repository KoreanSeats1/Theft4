#pragma once
#include "theft4_frame_plan.h"
#include "theft4_metal_plan.h"

namespace theft4::metal {
struct FrameTiming {
  double validation_ms=0,preparation_ms=0,encoding_ms=0;
  uint64_t commands=0,draws=0,pipelines_created=0,buffers_created=0,textures_created=0,uploaded_bytes=0;
  uint64_t render_passes=0,image_copies=0;
  uint64_t avoided_attachment_stores=0,avoided_attachment_loads=0,native_identity_copies=0;
  uint64_t binding_storage_reuses=0,binding_storage_fresh=0,avoided_clear_passes=0;
  EncoderStats encoder;
  PipelineCompilationStats compilation;
};
struct FrameResourceStats {
  size_t surface_creates=0,view_creates=0,retired=0,allocated_bytes=0;
  size_t host_specializations=0,host_specialization_fallbacks=0;
};
// One render worker, one Metal queue. Mutable targets persist by allocation
// generation; submitted command buffers retain them after CPU owners retire.
class FrameAdapter {
 public:
  explicit FrameAdapter(Renderer&,size_t maximum_resolve_specializations=128,size_t buffer_budget=128*1024*1024);
  ~FrameAdapter();
  void ConfigurePipelineCache(const std::string& directory);
  void FlushPipelineCache();
  bool Open(const std::string& libraries,std::string& error);
  Receipt Submit(const std::shared_ptr<const render::FramePlan>&,std::string& error,
                 render::SurfaceContents* published=nullptr,bool profile_gpu=false);
  // The output allocation is supplied by CAMetalLayer. Render and present
  // through one command buffer; no CPU copy or second submission is required.
  Receipt SubmitAndPresent(const std::shared_ptr<const render::FramePlan>&,
                           render::SurfaceKey,id<CAMetalDrawable>,std::string& error,
                           render::SurfaceContents* published=nullptr,bool profile_gpu=false);
  id<MTLTexture> Output(const render::FramePlan&,std::string& error);
  // Retrieve a defined GPU alias of a retained frame allocation. This never
  // uploads pixels or creates a new backing allocation.
  id<MTLTexture> SampledTexture(const render::FramePlan&,const render::SampledSurfaceView&,std::string& error);
  size_t RetireResources(bool bounded=false);
  FrameTiming LastTiming() const;
  FrameResourceStats Stats() const;
  ResourceCacheStats ImmutableStats() const;
  size_t PipelineCount() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  template<bool Diagnostics> Receipt SubmitFrameImpl(const std::shared_ptr<const render::FramePlan>&,
                      render::SurfaceKey,id<CAMetalDrawable>,std::string& error,
                      render::SurfaceContents* published,bool profile_gpu);
  Receipt SubmitFrame(const std::shared_ptr<const render::FramePlan>&,
                      render::SurfaceKey,id<CAMetalDrawable>,std::string& error,
                      render::SurfaceContents* published,bool profile_gpu);
};
}
