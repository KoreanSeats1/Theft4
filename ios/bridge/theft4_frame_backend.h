#pragma once
// Worker-owned live submission boundary. No graphics API types or handles.
#include "theft4_frame_plan.h"
#include <string>
#include <vector>
namespace theft4::render {
struct BackendCapabilities {
  uint32_t max_image_dimension_2d=0;
  bool bc_textures=false, astc_textures=false, float32_filtering=false;
  bool mirror_clamp_to_edge=false;
  uint32_t maximum_anisotropy=16;
  float maximum_sampler_lod_bias=16;
  uint32_t sample_counts=0; // Bit N means sample count N is supported.
};
struct PresentationTarget {
  uint32_t width=0,height=0;
  Format format=Format::Invalid;
};
class FrameBackend {
 public:
  virtual ~FrameBackend()=default;
  // Capabilities are immutable; reading them is allowed before worker startup.
  virtual BackendCapabilities Capabilities() const=0;
  virtual bool HasPresentation() const=0;
  virtual bool Open(std::string& error)=0;
  virtual bool Target(PresentationTarget&,std::string& error)=0;
  // present=true supplies the plan's final allocation from the native layer.
  // present=false commits an internal flush without acquiring a drawable.
  virtual bool Submit(std::shared_ptr<const FramePlan>,bool present,std::string& error)=0;
  virtual bool Drain(std::string& error)=0;
  virtual void Close()=0;
  // Explicit guest/validation readback only; never called by normal publication.
  virtual bool ReadRGBA8(const FramePlan&,SurfaceView,std::vector<uint8_t>&,
                        std::string& error)=0;
};
}
