#pragma once
#include "theft4_render_plan.h"
#include "theft4_host_program.h"
#include <algorithm>
#include <cstddef>
#include <memory>
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
// A sampled alias can expose a complete mip chain and several array layers or
// cube faces. Attachment/copy views still select one exact subresource.
struct SampledSurfaceView {
  SurfaceKey surface;
  uint32_t level=0,levels=1,slice=0,slices=1;
  Aspect aspect=Aspect::Color;
  ImageKind kind=ImageKind::Texture2D;
  Format format=Format::Invalid; // Inherit the allocation's format.
  std::array<Swizzle,4> swizzle{Swizzle::Red,Swizzle::Green,Swizzle::Blue,Swizzle::Alpha};
  SampledSurfaceView()=default;
  SampledSurfaceView(SurfaceView view):surface(view.surface),level(view.level),slice(view.slice),aspect(view.aspect){}
  auto operator<=>(const SampledSurfaceView&) const = default;
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
// Most title draws sample static images only. Keep their produced-view packet
// empty; allocate the dense interface only when a real GPU input is assigned.
// Copies retain value semantics so editing a copied plan cannot mutate its
// source. Const inspection never allocates, including the empty iterators.
class ProducedViews {
 public:
  using Values=std::array<std::optional<SampledSurfaceView>,kFetchCount>;
  ProducedViews()=default;
  ProducedViews(const ProducedViews& other):values_(other.values_?std::make_unique<Values>(*other.values_):nullptr){}
  ProducedViews& operator=(const ProducedViews& other) {
    if(this!=&other)values_=other.values_?std::make_unique<Values>(*other.values_):nullptr;
    return *this;
  }
  ProducedViews(ProducedViews&&) noexcept=default;
  ProducedViews& operator=(ProducedViews&&) noexcept=default;
  static constexpr size_t size(){return kFetchCount;}
  const auto& operator[](size_t slot)const{return Read()[slot];}
  auto& operator[](size_t slot){if(!values_)values_=std::make_unique<Values>();return (*values_)[slot];}
  void Set(size_t slot,std::optional<SampledSurfaceView> value) {
    if(value||values_)(*this)[slot]=std::move(value);
  }
  auto begin()const{return Read().begin();}
  auto end()const{return Read().end();}
  bool HasViews()const{return values_&&std::any_of(values_->begin(),values_->end(),[](const auto& v){return bool(v);});}
  size_t AllocatedBytes()const{return values_?sizeof(Values):0;}
 private:
  const Values& Read()const {static const Values empty{};return values_?*values_:empty;}
  std::unique_ptr<Values> values_;
};
struct FrameDraw {
  std::shared_ptr<const Capture> capture;
  // Only GPU-produced inputs appear here. Static images remain in Capture.
  ProducedViews produced;
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
struct HostFetch {
  std::optional<SampledSurfaceView> produced;
  std::shared_ptr<const Image> image;
  std::shared_ptr<const Sampler> sampler;
};
struct HostDraw {
  HostProgram program=HostProgram::Present;
  // Shader and vertex fields are empty: utilities use their named host
  // program and a generated fullscreen triangle, with their own constant ABI.
  Pipeline pipeline;
  Buffer constants;
  std::array<HostFetch,4> fetches{};
  std::array<uint32_t,4> scissor{};
  std::array<float,4> blend_color{};
  uint32_t stencil_front_reference=0,stencil_back_reference=0;
};
// Host utilities are rare compared with title draws. Box their larger packet
// so every title command does not pay for its pipeline and four fetches.
// Copies remain independent; a moved-from host command is rejected at admission.
class HostCommand {
 public:
  HostCommand(const HostDraw& draw):draw_(std::make_unique<HostDraw>(draw)){}
  HostCommand(HostDraw&& draw):draw_(std::make_unique<HostDraw>(std::move(draw))){}
  HostCommand(const HostCommand& other):draw_(other.draw_?std::make_unique<HostDraw>(*other.draw_):nullptr){}
  HostCommand& operator=(const HostCommand& other) {
    if(this!=&other)draw_=other.draw_?std::make_unique<HostDraw>(*other.draw_):nullptr;
    return *this;
  }
  HostCommand(HostCommand&&) noexcept=default;
  HostCommand& operator=(HostCommand&&) noexcept=default;
  HostDraw* Get(){return draw_.get();}
  const HostDraw* Get()const{return draw_.get();}
 private:
  std::unique_ptr<HostDraw> draw_;
};
using PassCommand=std::variant<FrameDraw,RectClear,HostCommand>;
inline HostDraw* GetHostDraw(PassCommand& command) {
  auto* host=std::get_if<HostCommand>(&command);return host?host->Get():nullptr;
}
inline const HostDraw* GetHostDraw(const PassCommand& command) {
  auto* host=std::get_if<HostCommand>(&command);return host?host->Get():nullptr;
}
struct Pass {
  // Explicit dimensions only for passes without attachments (one sample).
  std::array<uint32_t,2> attachmentless_extent{};
  std::array<std::optional<Attachment>,4> colors{};
  std::optional<Attachment> depth,stencil;
  std::vector<PassCommand> commands;
};
struct ImageCopy {
  // Exact-format, unscaled copy. Format conversion and scaled blits require
  // explicit shader passes; they must not be approximated by a raw copy.
  SurfaceView source,destination;
  std::array<uint32_t,2> source_origin{},destination_origin{},extent{};
  // Both aspects of a combined allocation move together, with defined source
  // depth and stencil. A depth-only copy must use an explicit shader pass.
  bool combined_depth_stencil=false;
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
// Join consecutive draws only when their attachment storage is unchanged and
// the new pass simply loads the previous stores. Explicit full clear-only
// initializations can fold into that pass's load actions when their attachment
// extents/samples agree. Rectangle clears, copies, resolves and clears after
// draws remain boundaries; no geometry is reordered or removed.
// The returned pass also lets a producer append one draw directly, avoiding
// a temporary one-element vector when the attachment set can be joined.
Pass& AppendPass(FramePlan&,Pass);
// Explicit clear submission may combine disjoint full-clear attachments.
// Never use this for an empty draw scope that will receive geometry later.
// Partial clears and invalid clear contracts fall back to ordinary boundaries.
Pass& AppendClearPass(FramePlan&,Pass);
// After successful frame admission only: identify stores overwritten before
// any read. Bits0..3=color,4=depth,5=stencil; final cross-frame stores survive.
std::vector<uint8_t> DeadAttachmentStores(const FramePlan&);
// A clear-only pass can disappear only if every attachment result is discarded
// or proven overwritten before its next read. Resolves and all draws remain.
bool DeadClearPass(const Pass&,uint8_t dead_stores);
// Applied after admission. Only host utilities proven to overwrite every
// color pixel can omit the preceding attachment load/clear.
std::vector<uint8_t> RedundantAttachmentLoads(const FramePlan&);
// Worker-owned CPU scratch. Analysis rewrites both result lists, with map
// nodes confined to this reusable arena. Large inputs use ordinary allocator
// overflow for that call; no GPU owner or map node escapes the analysis.
struct FrameAttachmentAnalysis {
  static constexpr size_t kArenaBytes=64*1024;
  std::vector<uint8_t> dead_stores,redundant_loads;
  std::unique_ptr<std::byte[]> arena=std::make_unique<std::byte[]>(kArenaBytes);
};
void AnalyzeFrameAttachments(const FramePlan&,FrameAttachmentAnalysis&);
// Exact, unscaled linear UNORM materialization can use a native image copy.
// All conversions, sample mappings, HDR and partial coverage stay shaders.
std::optional<ImageCopy> IdentityResolveCopy(const FramePlan&,const Pass&);
using SurfaceContents=std::set<SurfaceView>;
const Surface* FindSurface(const FramePlan&,SurfaceKey);
uint32_t SurfaceSlices(const Surface&);
bool SupportsAspect(Format,Aspect);
bool ValidateSampledView(const FramePlan&,const SampledSurfaceView&,std::string& error,bool allow_multisampled=false);
bool SampledViewContains(const SampledSurfaceView&,const SurfaceView&);
bool SampledViewDefined(const SampledSurfaceView&,const SurfaceContents&);
struct DrawVertexRange {const Capture* capture=nullptr;uint64_t maximum_vertex=0;bool index_has_restart=false;};
using DrawVertexRanges=std::vector<DrawVertexRange>;
struct FrameValidationScratch {
  DrawVertexRanges draw_ranges;
  void Clear(){draw_ranges.clear();}
};
// Admission is transactional. Reads/loads of discarded or undefined content,
// render/sample feedback, mismatched resolves and incompatible draw targets
// reject before encoding. Draw coverage never proves whole-target definition:
// a complete clear or an already defined loaded target is required.
bool ValidateFrame(const FramePlan&,const SurfaceContents& initial,
                   SurfaceContents& final,std::string& error,IndexRangeCache* indices=nullptr,
                   DrawVertexRanges* validated_draws=nullptr);
// Reusable worker admission storage. Final contents remain transactional;
// scratch ranges are empty on failure and must be cleared after consumption.
bool ValidateFrame(const FramePlan&,const SurfaceContents& initial,
                   SurfaceContents& final,std::string& error,IndexRangeCache* indices,
                   FrameValidationScratch& scratch);
}
