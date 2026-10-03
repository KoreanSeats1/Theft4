#pragma once
#include "theft4_render_plan.h"
#include <optional>
#include <set>
#include <variant>

namespace theft4::render {
// A GPU allocation's immutable identity. IDs are frontend-assigned, never API
// handles. A new allocation/placement owner requires a new generation.
struct SurfaceKey {
  uint64_t id=0,generation=0;
  auto operator<=>(const SurfaceKey&) const = default;
};
struct Surface {
  SurfaceKey key;
  Format format=Format::Invalid;
  ImageKind kind=ImageKind::Texture2D;
  uint32_t width=0,height=0,levels=1,layers=1,samples=1;
  auto operator<=>(const Surface&) const = default;
};
enum class Aspect : uint32_t { Color,Depth,Stencil,Count };
struct SurfaceView {
  SurfaceKey surface;
  uint32_t level=0,slice=0;
  Aspect aspect=Aspect::Color;
  auto operator<=>(const SurfaceView&) const = default;
};
enum class Load : uint32_t { Discard,Load,Clear,Count };
enum class Store : uint32_t { Discard,Store,Resolve,StoreAndResolve,Count };
enum class ResolveFilter : uint32_t { Average,Sample0,Min,Max,Count };
struct Attachment {
  SurfaceView view;
  std::optional<SurfaceView> resolve;
  Load load=Load::Discard;
  Store store=Store::Discard;
  ResolveFilter filter=ResolveFilter::Average;
  std::array<double,4> clear_color{};
  double clear_depth=1;
  uint32_t clear_stencil=0;
};
struct FrameDraw {
  std::shared_ptr<const Capture> capture;
  // Only GPU-produced inputs appear here. Static images remain in Capture.
  // The initial boundary samples one 2D mip/slice; wider views are explicit
  // future extensions, rather than silently flattening cube/volume inputs.
  std::array<std::optional<SurfaceView>,kFetchCount> produced{};
};
struct RectClear {
  // Attachment slots are independent of the current draw's color write mask.
  uint32_t colors=0;
  bool depth=false,stencil=false;
  std::array<uint32_t,4> rectangle{}; // x,y,width,height in host pixels.
  std::array<float,4> color{};
  float depth_value=1;
  uint32_t stencil_value=0;
};
using PassCommand=std::variant<FrameDraw,RectClear>;
struct Pass {
  std::array<std::optional<Attachment>,4> colors{};
  std::optional<Attachment> depth,stencil;
  std::vector<PassCommand> commands;
};
struct ImageCopy {
  // Exact-format, unscaled copy. Format conversion and scaled blits require
  // explicit shader passes; they must not be approximated by a raw copy.
  SurfaceView source,destination;
  std::array<uint32_t,2> source_origin{},destination_origin{},extent{};
};
using FrameCommand=std::variant<Pass,ImageCopy>;
struct FramePlan {
  uint64_t sequence=0;
  // Shared declarations are the lifetime owners of mutable GPU targets. A
  // frontend must retain the declaration while later frames can reuse it.
  std::vector<std::shared_ptr<const Surface>> surfaces;
  std::vector<FrameCommand> commands;
  std::optional<SurfaceView> output;
};
using SurfaceContents=std::set<SurfaceView>;
const Surface* FindSurface(const FramePlan&,SurfaceKey);
uint32_t SurfaceSlices(const Surface&);
bool SupportsAspect(Format,Aspect);
// Admission is transactional. Reads/loads of discarded or undefined content,
// render/sample feedback, mismatched resolves and incompatible draw targets
// reject before encoding. Draw coverage never proves whole-target definition:
// a complete clear or an already defined loaded target is required.
bool ValidateFrame(const FramePlan&,const SurfaceContents& initial,
                   SurfaceContents& final,std::string& error);
}
