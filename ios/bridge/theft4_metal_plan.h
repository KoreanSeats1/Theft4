#pragma once
#include "theft4_render_plan.h"
#include "theft4_metal_resources.h"
#include "theft4_metal_shader_store.h"
#include "theft4_vector_storage_pool.h"
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <unordered_map>

namespace theft4::metal {
// Device-local cache recipes contain state only, never texture or geometry data.
nlohmann::json PipelineRecipe(const render::Pipeline&,render::Primitive);
bool ReadPipelineRecipe(const nlohmann::json&,render::Pipeline&,render::Primitive&,bool game_shader=true);
std::shared_ptr<const Pipeline> BuildFixedPipeline(Renderer&,const render::Pipeline&,render::Primitive,
                                                  const Shader&,const Shader*,std::string& error);
// Render-worker-owned realization of the frontend's immutable CPU contract.
// Shader, PSO, sampler and resource hits do no driver creation or disk reads.
class PlanAdapter {
 public:
  explicit PlanAdapter(Renderer& renderer,size_t buffer_budget=128*1024*1024);
  void ConfigurePipelineCache(const std::string& directory) { pipeline_cache_directory_=directory; }
  void FlushPipelineCache();
  bool Open(const std::string& libraries, std::string& error);
  bool Prepare(const render::Capture& capture, Draw& draw, std::string& error);
  std::shared_ptr<const Draw> Realize(const std::shared_ptr<const render::Capture>& capture,std::string& error);
  // Patch only reflected bindings of an already prepared immutable draw.
  // GPU-produced textures remain owned by the ordered frame, never uploaded
  // as static CPU images or retained in the immutable source cache.
  bool BindProduced(const render::Capture&,const std::array<id<MTLTexture>,26>&,
                    Draw&,std::string& error) const;
  render::IndexRangeCache& IndexRanges() { return index_ranges_; }
  size_t PipelineCount() const { return render_pipelines_.size(); }
  size_t SamplerCount() const { return samplers_.size(); }
  ResourceCacheStats ResourceStats() const {
    auto stats=resources_.Stats();stats.prepared_view_hits=prepared_view_hits_;
    stats.prepared_view_misses=prepared_view_misses_;return stats;
  }
  size_t RetireResources(bool bounded=false);
  static MTLPixelFormat PixelFormat(render::Format format);
  void BeginUploadBatch() { EndUploadBatch();resources_.BeginUploadBatch();upload_batch_active_=true; }
  void EndUploadBatch() {
    for(size_t i=0;i<buffer_view_count_;++i)buffer_views_[buffer_view_slots_[i]]={};
    buffer_view_count_=0;upload_batch_active_=false;
    frame_upload_batch_.reset();
  }
  BufferView ConstantFor(const render::Buffer&,std::string& error);
  BufferView BufferFor(const render::Buffer& buffer,std::string& error);
  id<MTLTexture> ImageFor(const render::Image& image,std::string& error);
  id<MTLTexture> ImageFor(const std::shared_ptr<const render::Image>& image,std::string& error);
  id<MTLSamplerState> SamplerFor(const render::Sampler& sampler,std::string& error);
 private:
  friend class FrameAdapter;
  void BeginFrameUploadBatch(std::shared_ptr<FrameUploadPool::Batch> batch) {
    BeginUploadBatch();frame_upload_batch_=std::move(batch);
  }
  std::shared_ptr<FrameUploadPool::Batch> frame_upload_batch_;
  // Only the frame adapter calls this after full transactional admission of
  // the SAME immutable capture in the SAME submission. Public Prepare remains
  // independently strict, and Metal packet validation still runs before encode.
  // The internal destination must be a fresh Draw, owned by its caller's
  // storage cleanup; public Prepare uses a transaction-local destination.
  bool PrepareValidated(const render::Capture&,uint64_t maximum_vertex,bool index_has_restart,Draw&,std::string& error);
  void AcquireDrawBindings(Draw&);
  void RecycleDrawStorage(Draw& draw) noexcept;
  const bool diagnostics_;
  theft4::VectorStoragePool<TextureBinding,SamplerBinding> binding_storage_;
  std::string pipeline_cache_directory_;
  bool pipeline_cache_dirty_=false;
  size_t prepared_bucket_=0,image_bucket_=0;
  Renderer& renderer_;
  ShaderStore shaders_;
  ResourceCache resources_;
  // Bounded, submission-only memo. Avoid repeated
  // generation hashing and global LRU updates when draws share an upload.
  // Separate geometry/constants slots preserve their arena classification;
  // Views are released on every submission exit, including large geometry.
  struct PreparedBufferView {
    const render::Bytes* identity=nullptr;
    std::weak_ptr<const render::Bytes> owner;
    uint64_t generation=0;std::array<uint64_t,4> conversion{};
    BufferView view;
  };
  static constexpr size_t kGeometryViewSlots=4096,kConstantViewSlots=32768;
  // Allocate once with the adapter, rather than putting the working-set
  // table on a caller's stack. Clear only occupied slots after submission.
  std::unique_ptr<PreparedBufferView[]> buffer_views_=
      std::make_unique<PreparedBufferView[]>(kGeometryViewSlots+kConstantViewSlots);
  std::array<uint16_t,kGeometryViewSlots+kConstantViewSlots> buffer_view_slots_{};
  size_t buffer_view_count_=0;
  bool upload_batch_active_=false;
  uint64_t prepared_view_hits_=0,prepared_view_misses_=0;
  BufferView UploadedViewFor(const std::shared_ptr<const render::Bytes>&,bool constants,std::string& error);
  render::IndexRangeCache index_ranges_;
  struct PipelineLookup {const render::Pipeline& pipeline;render::Primitive primitive;};
  struct PipelineLess {
    using is_transparent=void;
    using Key=std::pair<render::Pipeline,render::Primitive>;
    static bool Less(const render::Pipeline& a,render::Primitive ap,const render::Pipeline& b,render::Primitive bp) {
      const auto order=a<=>b;return order!=0?order<0:ap<bp;
    }
    bool operator()(const Key& a,const Key& b)const{return Less(a.first,a.second,b.first,b.second);}
    bool operator()(const Key& a,const PipelineLookup& b)const{return Less(a.first,a.second,b.pipeline,b.primitive);}
    bool operator()(const PipelineLookup& a,const Key& b)const{return Less(a.pipeline,a.primitive,b.first,b.second);}
  };
  std::map<std::pair<render::Pipeline,render::Primitive>,std::shared_ptr<const Pipeline>,PipelineLess> pipelines_;
  // Key omits only the separate depth/stencil test object. Attachment formats,
  // shader specializations, vertex ABI, blending and MSAA remain exact.
  decltype(pipelines_) render_pipelines_;
  render::Pipeline consecutive_source_;
  render::Primitive consecutive_primitive_=render::Primitive::Count;
  std::shared_ptr<const Pipeline> consecutive_pipeline_;
  std::map<render::Sampler,id<MTLSamplerState>> samplers_;
  std::array<id<MTLTexture>,4> dummy_images_{};
  struct Prepared {std::weak_ptr<const render::Capture> owner;std::shared_ptr<const Draw> draw;uint64_t generation=0;};
  std::unordered_map<const render::Capture*,Prepared> prepared_;
  struct ImageEntry {
    std::weak_ptr<const render::Image> owner;
    std::weak_ptr<const render::Bytes> source;
    render::Image description;
    uint64_t generation=0;std::array<uint64_t,4> conversion{};size_t bytes=0;
    id<MTLTexture> texture;
  };
  std::unordered_map<const render::Image*,ImageEntry> images_;
  std::shared_ptr<const Pipeline> PipelineFor(const render::Pipeline& pipeline,render::Primitive primitive,std::string& error);
  bool EnsureDummyImages(std::string& error);
};
}
