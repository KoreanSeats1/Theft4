#pragma once
#include "theft4_frame_plan.h"
#include "theft4_metal_plan.h"

namespace theft4::metal {
struct FrameResourceStats {
  size_t surface_creates=0,view_creates=0,retired=0,allocated_bytes=0;
};
// One render worker, one Metal queue. Mutable targets persist by allocation
// generation; submitted command buffers retain them after CPU owners retire.
class FrameAdapter {
 public:
  explicit FrameAdapter(Renderer&);
  ~FrameAdapter();
  bool Open(const std::string& libraries,std::string& error);
  Receipt Submit(const std::shared_ptr<const render::FramePlan>&,std::string& error);
  // The output allocation is supplied by CAMetalLayer. Render and present
  // through one command buffer; no CPU copy or second submission is required.
  Receipt SubmitAndPresent(const std::shared_ptr<const render::FramePlan>&,
                           render::SurfaceKey,id<CAMetalDrawable>,std::string& error);
  id<MTLTexture> Output(const render::FramePlan&,std::string& error);
  size_t RetireResources();
  FrameResourceStats Stats() const;
  ResourceCacheStats ImmutableStats() const;
  size_t PipelineCount() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  Receipt SubmitFrame(const std::shared_ptr<const render::FramePlan>&,
                      render::SurfaceKey,id<CAMetalDrawable>,std::string& error);
};
}
