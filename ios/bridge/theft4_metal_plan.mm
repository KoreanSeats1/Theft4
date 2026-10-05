#include "theft4_retail_mode.h"
#include "theft4_metal_plan.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include "theft4_bounded_cache_sweep.h"
#include <algorithm>
#include <bit>
#include <cstring>

namespace theft4::metal {
namespace {
bool Error(std::string& error,const char* text){error=text;return false;}
MTLVertexFormat VertexFormat(render::VertexFormat format) {
  using F=render::VertexFormat;
  switch(format) {
#define V(name) case F::name:return MTLVertexFormat##name
    V(Float);V(Float2);V(Float3);V(Float4);V(Int);V(Int2);V(Int3);V(Int4);
    V(UInt);V(UInt2);V(UInt3);V(UInt4);V(Half2);V(Half4);V(Short2);V(Short4);
    V(UShort2);V(UShort4);V(Short2Normalized);V(Short4Normalized);
    V(UShort2Normalized);V(UShort4Normalized);V(UChar4);V(UChar4Normalized);
    V(Int1010102Normalized);
#undef V
    case F::UChar4NormalizedBGRA:return MTLVertexFormatUChar4Normalized_BGRA;
    default:return MTLVertexFormatInvalid;
  }
}
MTLTextureType ImageType(render::ImageKind kind) {
  using K=render::ImageKind;
  switch(kind){case K::Texture2D:return MTLTextureType2D;case K::Texture2DArray:return MTLTextureType2DArray;
    case K::Texture3D:return MTLTextureType3D;case K::TextureCube:return MTLTextureTypeCube;
    case K::TextureCubeArray:return MTLTextureTypeCubeArray;default:return MTLTextureType1D;}
}
MTLBlendFactor BlendFactor(render::BlendFactor factor) {
  using B=render::BlendFactor;
  switch(factor) {
#define B(name) case B::name:return MTLBlendFactor##name
    B(Zero);B(One);B(SourceColor);B(OneMinusSourceColor);B(DestinationColor);B(OneMinusDestinationColor);
    B(SourceAlpha);B(OneMinusSourceAlpha);B(DestinationAlpha);B(OneMinusDestinationAlpha);
    B(SourceAlphaSaturated);B(Source1Color);B(OneMinusSource1Color);B(Source1Alpha);B(OneMinusSource1Alpha);
#undef B
    case B::ConstantColor:return MTLBlendFactorBlendColor;
    case B::OneMinusConstantColor:return MTLBlendFactorOneMinusBlendColor;
    case B::ConstantAlpha:return MTLBlendFactorBlendAlpha;
    case B::OneMinusConstantAlpha:return MTLBlendFactorOneMinusBlendAlpha;
    default:return MTLBlendFactorZero;
  }
}
MTLColorWriteMask WriteMask(uint32_t mask) {
  return MTLColorWriteMask(((mask&1)?MTLColorWriteMaskRed:0)|((mask&2)?MTLColorWriteMaskGreen:0)|
      ((mask&4)?MTLColorWriteMaskBlue:0)|((mask&8)?MTLColorWriteMaskAlpha:0));
}
ResourceVersion Version(const std::shared_ptr<const render::Bytes>& source) {
  return {source,source->generation,source->conversion};
}
}
PlanAdapter::PlanAdapter(Renderer& renderer,size_t budget):renderer_(renderer),shaders_(renderer),resources_(renderer,budget){}
Draw PlanAdapter::AcquireDrawStorage() {
  auto storage=binding_storage_.Acquire();Draw draw;
  draw.textures=std::move(storage.first);draw.samplers=std::move(storage.second);return draw;
}
void PlanAdapter::RecycleDrawStorage(Draw& draw) noexcept {
  binding_storage_.Recycle(draw.textures,draw.samplers);
}
namespace {
using PipelineJson=nlohmann::json;
PipelineJson PipelineRecipe(const render::Pipeline& p,render::Primitive primitive) {
  PipelineJson attributes=PipelineJson::array(),streams=PipelineJson::array(),blends=PipelineJson::array();
  for(const auto& a:p.attributes)attributes.push_back({a.location,a.stream,a.offset,uint32_t(a.format)});
  for(const auto& a:p.streams)streams.push_back({a.stride,a.per_instance});
  for(const auto& b:p.blends)blends.push_back({b.enabled,uint32_t(b.rgb),uint32_t(b.alpha),uint32_t(b.source_rgb),
    uint32_t(b.destination_rgb),uint32_t(b.source_alpha),uint32_t(b.destination_alpha),b.write_mask});
  std::array<uint32_t,4> colors{};for(size_t i=0;i<4;++i)colors[i]=uint32_t(p.colors[i]);
  return {{"primitive",uint32_t(primitive)},{"vertex",{p.vertex.hash,p.vertex.variant,p.vertex.specialization}},
    {"fragment",{p.fragment.hash,p.fragment.variant,p.fragment.specialization}},{"attributes",attributes},
    {"streams",streams},{"colors",colors},{"blends",blends},{"depth",uint32_t(p.depth)},
    {"stencil",uint32_t(p.stencil)},{"samples",p.samples},{"mask",p.sample_mask},{"negative_clip",p.negative_one_to_one}};
}
bool ReadPipelineRecipe(const PipelineJson& j,render::Pipeline& p,render::Primitive& primitive) {
  const auto pr=j.at("primitive").get<uint32_t>();if(pr>=uint32_t(render::Primitive::Count))return false;
  primitive=render::Primitive(pr);
  const auto shader=[](const PipelineJson& a) {if(a.size()!=3)throw std::runtime_error("pipeline shader recipe");
    return render::Shader{a.at(0).get<uint64_t>(),a.at(1).get<uint32_t>(),a.at(2).get<uint32_t>()};};
  p.vertex=shader(j.at("vertex"));p.fragment=shader(j.at("fragment"));
  if(j.at("attributes").size()>31||j.at("streams").size()!=p.streams.size()||j.at("colors").size()!=4||j.at("blends").size()!=4)return false;
  for(const auto& a:j.at("attributes")) {
    if(a.size()!=4)return false;render::Attribute value;value.location=a.at(0).get<uint32_t>();value.stream=a.at(1).get<uint32_t>();
    value.offset=a.at(2).get<uint32_t>();value.format=render::VertexFormat(a.at(3).get<uint32_t>());
    if(value.location>=31||value.stream>=p.streams.size()||value.format>=render::VertexFormat::Count)return false;
    p.attributes.push_back(value);
  }
  for(size_t i=0;i<p.streams.size();++i){const auto& a=j.at("streams").at(i);if(a.size()!=2)return false;
    p.streams[i]={a.at(0).get<uint32_t>(),a.at(1).get<bool>()};}
  for(size_t i=0;i<4;++i) {
    p.colors[i]=render::Format(j.at("colors").at(i).get<uint32_t>());const auto& b=j.at("blends").at(i);if(b.size()!=8)return false;
    auto& o=p.blends[i];o.enabled=b.at(0).get<bool>();o.rgb=render::BlendOp(b.at(1).get<uint32_t>());o.alpha=render::BlendOp(b.at(2).get<uint32_t>());
    o.source_rgb=render::BlendFactor(b.at(3).get<uint32_t>());o.destination_rgb=render::BlendFactor(b.at(4).get<uint32_t>());
    o.source_alpha=render::BlendFactor(b.at(5).get<uint32_t>());o.destination_alpha=render::BlendFactor(b.at(6).get<uint32_t>());o.write_mask=b.at(7).get<uint32_t>();
    if(p.colors[i]>=render::Format::Depth32Float||o.rgb>=render::BlendOp::Count||o.alpha>=render::BlendOp::Count||
       o.source_rgb>=render::BlendFactor::Count||o.destination_rgb>=render::BlendFactor::Count||
       o.source_alpha>=render::BlendFactor::Count||o.destination_alpha>=render::BlendFactor::Count||o.write_mask>15)return false;
  }
  p.depth=render::Format(j.at("depth").get<uint32_t>());p.stencil=render::Format(j.at("stencil").get<uint32_t>());
  if(p.depth!=render::Format::Invalid&&p.depth!=render::Format::Depth32Float&&p.depth!=render::Format::Depth32FloatStencil8)return false;
  if(p.stencil!=render::Format::Invalid&&p.stencil!=render::Format::Stencil8&&p.stencil!=render::Format::Depth32FloatStencil8)return false;
  p.samples=j.at("samples").get<uint32_t>();p.sample_mask=j.at("mask").get<uint32_t>();p.negative_one_to_one=j.at("negative_clip").get<bool>();
  return p.samples&&p.samples<=8&&std::has_single_bit(p.samples)&&p.vertex.hash&&p.vertex.variant<=1&&p.fragment.variant<=1;
}
}
void PlanAdapter::FlushPipelineCache() {
  if(!pipeline_cache_dirty_||pipeline_cache_directory_.empty())return;
  try {
    PipelineJson rows=PipelineJson::array();for(const auto& [key,entry]:render_pipelines_) {
      if(rows.size()==1024)break;rows.push_back(PipelineRecipe(key.first,key.second));
    }
    const auto path=std::filesystem::path(pipeline_cache_directory_)/"recipes.json";const auto temporary=path.string()+".tmp";
    std::filesystem::create_directories(path.parent_path());std::ofstream stream(temporary);stream<<PipelineJson{{"schema",1},{"pipelines",rows}};stream.close();
    if(stream&&std::rename(temporary.c_str(),path.c_str())==0)pipeline_cache_dirty_=false;
  }catch(...){} // A cache failure cannot reject an otherwise valid game frame.
}
bool PlanAdapter::Open(const std::string& libraries,std::string& error) {
  if(!shaders_.Open(libraries,error))return false;
  consecutive_pipeline_.reset();consecutive_source_={};consecutive_primitive_=render::Primitive::Count;
  pipelines_.clear();render_pipelines_.clear();prepared_.clear();images_.clear();
  if(!pipeline_cache_directory_.empty())try {
    const auto path=std::filesystem::path(pipeline_cache_directory_)/"recipes.json";
    if(std::filesystem::exists(path)&&std::filesystem::file_size(path)<=2*1024*1024) {
      std::ifstream stream(path);const auto cached=PipelineJson::parse(stream);
      if(cached.at("schema")==1&&cached.at("pipelines").size()<=1024)for(const auto& row:cached.at("pipelines")) {
        try {render::Pipeline p;render::Primitive primitive;if(ReadPipelineRecipe(row,p,primitive)&&render::ValidateFixedPipeline(p,error)) {
          std::string ignored;PipelineFor(p,primitive,ignored);
        }}catch(...){}
      }
      if(!theft4_retail_mode()) std::fprintf(stderr,"gta4-metal-pipeline-preparation: prepared=%zu archive-hits=%llu before-gameplay=true\n",
        render_pipelines_.size(),(unsigned long long)renderer_.PipelineArchiveHits());
    }
  }catch(...){}
  pipeline_cache_dirty_=false;error.clear();return true;
}
MTLPixelFormat PlanAdapter::PixelFormat(render::Format format) {
  using F=render::Format;
  switch(format) {
    case F::Invalid:return MTLPixelFormatInvalid;
#define P(name) case F::name:return MTLPixelFormat##name
    P(R8Unorm);P(RG8Unorm);P(RGBA8Unorm);P(BGRA8Unorm);P(R16Unorm);P(RG16Unorm);P(RGBA16Unorm);
    P(R16Float);P(RG16Float);P(RGBA16Float);P(R32Float);P(RG32Float);P(RGBA32Float);
    P(RGB10A2Unorm);P(Depth32Float);P(Stencil8);
#undef P
    case F::RGBA8Srgb:return MTLPixelFormatRGBA8Unorm_sRGB;
    case F::BGRA8Srgb:return MTLPixelFormatBGRA8Unorm_sRGB;
    case F::Depth32FloatStencil8:return MTLPixelFormatDepth32Float_Stencil8;
    case F::BC1Unorm:return MTLPixelFormatBC1_RGBA;case F::BC1Srgb:return MTLPixelFormatBC1_RGBA_sRGB;
    case F::BC2Unorm:return MTLPixelFormatBC2_RGBA;case F::BC2Srgb:return MTLPixelFormatBC2_RGBA_sRGB;
    case F::BC3Unorm:return MTLPixelFormatBC3_RGBA;case F::BC3Srgb:return MTLPixelFormatBC3_RGBA_sRGB;
    case F::BC4Unorm:return MTLPixelFormatBC4_RUnorm;case F::BC4Snorm:return MTLPixelFormatBC4_RSnorm;
    case F::BC5Unorm:return MTLPixelFormatBC5_RGUnorm;case F::BC5Snorm:return MTLPixelFormatBC5_RGSnorm;
    case F::ASTC4x4:return MTLPixelFormatASTC_4x4_LDR;case F::ASTC4x4Srgb:return MTLPixelFormatASTC_4x4_sRGB;
    default:return MTLPixelFormatInvalid;
  }
}
namespace {
MTLDepthStencilDescriptor* DepthDescriptor(const render::Pipeline& p) {
  auto depth=[MTLDepthStencilDescriptor new];depth.depthCompareFunction=p.depth_test ? MTLCompareFunction(p.depth_compare) : MTLCompareFunctionAlways;
  depth.depthWriteEnabled=p.depth_write;
  if(p.stencil_test) {
    const auto stencil=[](const render::Stencil& s) {
      auto d=[MTLStencilDescriptor new];d.stencilCompareFunction=MTLCompareFunction(s.compare);
      d.stencilFailureOperation=MTLStencilOperation(s.fail);d.depthStencilPassOperation=MTLStencilOperation(s.pass);
      d.depthFailureOperation=MTLStencilOperation(s.depth_fail);d.readMask=s.read_mask;d.writeMask=s.write_mask;return d;
    };
    depth.frontFaceStencil=stencil(p.front);depth.backFaceStencil=stencil(p.back);
  }
  return depth;
}
}
std::shared_ptr<const Pipeline> BuildFixedPipeline(Renderer& renderer,const render::Pipeline& p,
    render::Primitive primitive,const Shader& vertex,const Shader* pixel,std::string& error) {
  auto descriptor=[MTLRenderPipelineDescriptor new];descriptor.rasterSampleCount=p.samples;
  descriptor.inputPrimitiveTopology=primitive==render::Primitive::Point ? MTLPrimitiveTopologyClassPoint :
      (primitive==render::Primitive::Line||primitive==render::Primitive::LineStrip) ? MTLPrimitiveTopologyClassLine : MTLPrimitiveTopologyClassTriangle;
  auto declaration=[MTLVertexDescriptor new];
  for(const auto& attribute:p.attributes) {
    auto a=declaration.attributes[attribute.location];a.format=VertexFormat(attribute.format);
    a.offset=attribute.offset;a.bufferIndex=attribute.stream+8;
    auto layout=declaration.layouts[attribute.stream+8];layout.stride=p.streams[attribute.stream].stride;
    layout.stepFunction=p.streams[attribute.stream].per_instance ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
    layout.stepRate=1;
  }
  descriptor.vertexDescriptor=declaration;
  for(size_t i=0;i<4;++i) {
    const auto format=p.colors[i];
    if(format>=render::Format::Depth32Float&&format!=render::Format::Invalid){error="Game color target is not a renderable color format";return {};}
    const auto& b=p.blends[i];auto a=descriptor.colorAttachments[i];a.pixelFormat=PlanAdapter::PixelFormat(format);
    a.blendingEnabled=b.enabled;a.sourceRGBBlendFactor=BlendFactor(b.source_rgb);
    a.destinationRGBBlendFactor=BlendFactor(b.destination_rgb);a.sourceAlphaBlendFactor=BlendFactor(b.source_alpha);
    a.destinationAlphaBlendFactor=BlendFactor(b.destination_alpha);
    a.rgbBlendOperation=MTLBlendOperation(b.rgb);a.alphaBlendOperation=MTLBlendOperation(b.alpha);a.writeMask=WriteMask(b.write_mask);
  }
  descriptor.depthAttachmentPixelFormat=PlanAdapter::PixelFormat(p.depth);descriptor.stencilAttachmentPixelFormat=PlanAdapter::PixelFormat(p.stencil);
  auto depth=DepthDescriptor(p);
  return pixel ? renderer.MakePipeline(vertex,*pixel,descriptor,depth,error) : renderer.MakeDepthPipeline(vertex,descriptor,depth,error);
}
std::shared_ptr<const Pipeline> PlanAdapter::PipelineFor(const render::Pipeline& source,render::Primitive primitive,std::string& error) {
  const auto& p=source;
  // Metal compiles topology classes; list versus strip remains in each Draw.
  if(primitive==render::Primitive::LineStrip)primitive=render::Primitive::Line;
  if(primitive==render::Primitive::TriangleStrip)primitive=render::Primitive::Triangle;
  // Exact source equality covers all shader, specialization, vertex, attachment,
  // blend and depth/stencil fields. Hits borrow the last realized object before
  // metadata lookup or canonical-key preparation; Open invalidates the owner.
  if(consecutive_pipeline_&&primitive==consecutive_primitive_&&source==consecutive_source_) {
    error.clear();return consecutive_pipeline_;
  }
  const auto remember=[&](std::shared_ptr<const Pipeline> result) {
    if(result){consecutive_source_=source;consecutive_primitive_=primitive;consecutive_pipeline_=result;}
    return result;
  };
  if(p.vertex.variant>1||p.fragment.variant>1){error="Game shader override has not been lowered into the Metal catalog";return {};}
  const uint32_t required=p.samples==32 ? UINT32_MAX : (1u<<p.samples)-1;
  if((p.sample_mask&required)!=required){error="This game draw requires pipeline sample-mask shader lowering";return {};}
  const auto* vs_meta=shaders_.Metadata({p.vertex.hash,p.vertex.variant==1,p.negative_one_to_one},Stage::Vertex);
  const auto* ps_meta=p.fragment.hash ? shaders_.Metadata({p.fragment.hash,p.fragment.variant==1},Stage::Fragment) : nullptr;
  if(!vs_meta||(p.fragment.hash&&!ps_meta)){error="Captured game shader is absent from the offline Metal catalog";return {};}
  const auto vs_specialization=vs_meta->Specialization(p.vertex.specialization);
  const auto ps_specialization=ps_meta?ps_meta->Specialization(p.fragment.specialization):p.fragment.specialization;
  render::Pipeline specialized;const auto* effective=&source;
  if(vs_specialization!=p.vertex.specialization||ps_specialization!=p.fragment.specialization) {
    specialized=source;specialized.vertex.specialization=vs_specialization;
    specialized.fragment.specialization=ps_specialization;effective=&specialized;
  }
  // Borrow the immutable declaration for hits. The previous value argument
  // and owning map lookup copied its attribute vector twice on every draw.
  if(auto it=pipelines_.find(PipelineLookup{*effective,primitive});it!=pipelines_.end()){error.clear();return remember(it->second);}
  render::Pipeline raster=*effective;
  raster.depth_test=false;raster.depth_write=false;raster.stencil_test=false;
  raster.depth_compare=render::Compare::Always;raster.front={};raster.back={};
  if(auto found=render_pipelines_.find(PipelineLookup{raster,primitive});found!=render_pipelines_.end()) {
    auto result=renderer_.MakeDepthVariant(*found->second,DepthDescriptor(*effective),error);
    if(result)pipelines_.emplace(std::pair{*effective,primitive},result);return remember(result);
  }
  auto vertex=shaders_.Resolve({p.vertex.hash,p.vertex.variant==1,p.negative_one_to_one},Stage::Vertex,vs_specialization,error);
  if(!vertex.function)return {};
  Shader pixel{};
  if(p.fragment.hash){pixel=shaders_.Resolve({p.fragment.hash,p.fragment.variant==1},Stage::Fragment,ps_specialization,error);if(!pixel.function)return {};}
  auto result=BuildFixedPipeline(renderer_,*effective,primitive,vertex,p.fragment.hash ? &pixel : nullptr,error);
  if(result){pipeline_cache_dirty_=true;render_pipelines_.emplace(std::pair{std::move(raster),primitive},result);
    pipelines_.emplace(std::pair{*effective,primitive},result);}return remember(result);
}
BufferView PlanAdapter::UploadedViewFor(const std::shared_ptr<const render::Bytes>& source,bool constants,std::string& error) {
  if(!upload_batch_active_||source->value.size()>64*1024)
    return constants?resources_.UniformBuffer(Version(source),source->value,error):
                     resources_.UploadBuffer(Version(source),source->value,error);
  // Allocator alignment and regular source strides made the old low-bit,
  // 32-slot index collide frequently. Mix the address and bound each lookup
  // to eight probes. A saturated table falls back to the full resource cache.
  uint64_t hash=reinterpret_cast<uintptr_t>(source.get())>>4;
  hash^=hash>>17;hash*=0x9e3779b97f4a7c15ull;hash^=hash>>32;
  const size_t base=constants?kGeometryViewSlots:0;
  const size_t mask=(constants?kConstantViewSlots:kGeometryViewSlots)-1;
  PreparedBufferView* destination=nullptr;
  size_t destination_slot=0;
  for(size_t probe=0;probe<8;++probe) {
    const auto slot=base+((size_t(hash)+probe)&mask);auto& entry=buffer_views_[slot];
    if(!entry.identity){destination=&entry;destination_slot=slot;break;}
    if(entry.identity!=source.get())continue;
    destination=&entry;destination_slot=slot;
    if(!entry.owner.owner_before(source)&&!source.owner_before(entry.owner)&&
        entry.generation==source->generation&&entry.conversion==source->conversion) {
      if(entry.view.length!=source->value.size()){error="Metal upload generation changed payload size";return {};}
      ++prepared_view_hits_;error.clear();return entry.view;
    }
    break;
  }
  ++prepared_view_misses_;
  auto view=constants?resources_.UniformBuffer(Version(source),source->value,error):
                      resources_.UploadBuffer(Version(source),source->value,error);
  if(view.buffer&&destination) {
    if(!destination->identity)buffer_view_slots_[buffer_view_count_++]=uint16_t(destination_slot);
    *destination={source.get(),source,source->generation,source->conversion,view};
  }
  return view;
}
BufferView PlanAdapter::BufferFor(const render::Buffer& b,std::string& error) {
  if(!b.source)return {};
  if(b.offset>b.source->value.size()||b.length>b.source->value.size()-b.offset){error="Invalid buffer source range";return {};}
  auto view=UploadedViewFor(b.source,false,error);
  if(!view.buffer)return {};
  view.offset+=NSUInteger(b.offset);view.length=NSUInteger(b.length);return view;
}
BufferView PlanAdapter::ConstantFor(const render::Buffer& b,std::string& error) {
  if(!b.source)return {};
  if(b.offset>b.source->value.size()||b.length>b.source->value.size()-b.offset){error="Invalid constant source range";return {};}
  if(b.source->value.size()>64*1024)return BufferFor(b,error);
  auto view=UploadedViewFor(b.source,true,error);
  if(!view.buffer)return {};
  view.offset+=NSUInteger(b.offset);view.length=NSUInteger(b.length);return view;
}
id<MTLTexture> PlanAdapter::ImageFor(const render::Image& image,std::string& error) {
  auto d=[MTLTextureDescriptor new];d.textureType=ImageType(image.kind);d.pixelFormat=PixelFormat(image.format);
  d.width=image.width;d.height=image.height;d.depth=image.depth;d.arrayLength=image.layers;d.mipmapLevelCount=image.levels;
  d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;
  const auto swizzle=[](render::Swizzle s){return MTLTextureSwizzle(s);};
  d.swizzle=MTLTextureSwizzleChannelsMake(swizzle(image.swizzle[0]),swizzle(image.swizzle[1]),swizzle(image.swizzle[2]),swizzle(image.swizzle[3]));
  std::vector<TextureUpload> uploads;uploads.reserve(image.mips.size());
  // The neutral capture records a plane pitch for every image kind. Metal
  // uses bytesPerImage only for volumes; 2D, array and cube slices are already
  // selected by their upload offset and slice number.
  for(const auto& m:image.mips)uploads.push_back({m.level,m.slice,m.width,m.height,m.depth,
      NSUInteger(m.row_bytes),image.kind==render::ImageKind::Texture3D ? NSUInteger(m.image_bytes) : 0,
      size_t(m.offset),size_t(m.size)});
  return resources_.Texture(Version(image.source),d,image.source->value,uploads,error);
}
id<MTLTexture> PlanAdapter::ImageFor(const std::shared_ptr<const render::Image>& image,std::string& error) {
  if(!image||!image->source){error="Missing immutable sampled image owner";return nil;}
  if(auto i=images_.find(image.get());i!=images_.end()&&
      !i->second.owner.owner_before(image)&&!image.owner_before(i->second.owner)) {
    const auto& e=i->second;const auto& d=e.description;
    if(e.source.owner_before(image->source)||image->source.owner_before(e.source)||
       e.generation!=image->source->generation||e.conversion!=image->source->conversion||e.bytes!=image->source->value.size()||
       d.format!=image->format||d.kind!=image->kind||d.width!=image->width||d.height!=image->height||
       d.depth!=image->depth||d.layers!=image->layers||d.levels!=image->levels||d.swizzle!=image->swizzle||d.mips!=image->mips) {
      error="Metal image owner changed upload/format identity";return nil;
    }
    error.clear();return e.texture;
  }
  auto texture=ImageFor(*image,error);if(!texture)return nil;
  ImageEntry entry;entry.owner=image;entry.source=image->source;entry.description=*image;entry.description.source.reset();
  entry.generation=image->source->generation;entry.conversion=image->source->conversion;entry.bytes=image->source->value.size();entry.texture=texture;
  images_.insert_or_assign(image.get(),std::move(entry));return texture;
}
id<MTLSamplerState> PlanAdapter::SamplerFor(const render::Sampler& s,std::string& error) {
  if(auto it=samplers_.find(s);it!=samplers_.end()){error.clear();return it->second;}
  const auto address=[](render::Address a) {
    switch(a){case render::Address::Repeat:return MTLSamplerAddressModeRepeat;
      case render::Address::MirrorRepeat:return MTLSamplerAddressModeMirrorRepeat;
      case render::Address::ClampEdge:return MTLSamplerAddressModeClampToEdge;
      case render::Address::MirrorClampEdge:return MTLSamplerAddressModeMirrorClampToEdge;
      case render::Address::ClampBorder:return MTLSamplerAddressModeClampToBorderColor;
      default:return MTLSamplerAddressModeClampToEdge;}
  };
  auto d=[MTLSamplerDescriptor new];d.minFilter=s.min_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
  d.magFilter=s.mag_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
  d.mipFilter=s.mip_linear ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
  d.sAddressMode=address(s.address[0]);d.tAddressMode=address(s.address[1]);d.rAddressMode=address(s.address[2]);
  d.maxAnisotropy=s.anisotropy;d.lodMinClamp=std::bit_cast<float>(s.min_lod_bits);d.lodMaxClamp=std::bit_cast<float>(s.max_lod_bits);
  d.borderColor=s.opaque_white_border ? MTLSamplerBorderColorOpaqueWhite : MTLSamplerBorderColorTransparentBlack;
  auto result=[renderer_.Device() newSamplerStateWithDescriptor:d];
  if(result)samplers_.emplace(s,result);else error="Metal rejected the game sampler descriptor";
  return result;
}
bool PlanAdapter::EnsureDummyImages(std::string& error) {
  for(size_t i=0;i<4;++i)if(!dummy_images_[i]) {
    auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
    d.textureType=ImageType(render::ImageKind(i));d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;
    auto image=renderer_.Texture(d,error);if(!image)return false;
    const uint8_t black[4]{};
    for(NSUInteger slice=0;slice<(i==3 ? 6u : 1u);++slice)
      [image replaceRegion:MTLRegionMake3D(0,0,0,1,1,1) mipmapLevel:0 slice:slice withBytes:black bytesPerRow:4 bytesPerImage:4];
    dummy_images_[i]=image;
  }
  return true;
}
bool PlanAdapter::Prepare(const render::Capture& capture,Draw& draw,std::string& error) {
  uint64_t maximum=0;
  if(!render::Validate(capture,error,nullptr,&index_ranges_,&maximum))return false;
  return PrepareValidated(capture,maximum,draw,error);
}
bool PlanAdapter::PrepareValidated(const render::Capture& capture,uint64_t maximum,Draw& draw,std::string& error) {
  const auto& source=capture.draw;Draw result=AcquireDrawStorage();
  theft4::StorageCleanup cleanup{[&]{RecycleDrawStorage(result);}};result.maximum_vertex=NSUInteger(maximum);
  result.pipeline=PipelineFor(source.pipeline,source.primitive,error);if(!result.pipeline)return false;
  result.primitive=MTLPrimitiveType(source.primitive);result.first_vertex=source.first_vertex;
  result.vertex_count=source.vertex_count;result.instance_count=source.instances;result.base_vertex=source.base_vertex;
  for(size_t i=0;i<3;++i) {
    const auto required=result.pipeline->constant_bytes[i];
    if(source.constant_bytes[i]<required)return Error(error,"Game constant payload does not cover the offline shader interface");
    if(!required)continue;
    result.constants[i]=ConstantFor(source.constants[i],error);if(!result.constants[i].buffer)return false;
  }
  for(size_t i=0;i<kGameVertexStreamCount;++i)if(result.pipeline->vertex_streams&(1u<<i)) {
    result.vertices[i]=BufferFor(source.vertices[i],error);if(!result.vertices[i].buffer)return false;
  }
  if(source.index_count) {
    result.indices=BufferFor(source.indices,error);if(!result.indices.buffer)return false;
    result.index_count=source.index_count;result.index_type=source.index_bytes==2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
    render::IndexRange range;
    if(!index_ranges_.Analyze(source.indices,source.index_count,source.index_bytes,range,error))return false;
    if(range.has_restart&&!(source.primitive_restart&&
        (source.primitive==render::Primitive::LineStrip||source.primitive==render::Primitive::TriangleStrip)))
      return Error(error,"Metal fixed restart markers differ from this game index stream; frontend expansion is required");
  }
  if((result.pipeline->vertex.textures||result.pipeline->fragment.textures)&&!EnsureDummyImages(error))return false;
  std::array<FetchResources,26> fetches{};
  const auto* vm=shaders_.Metadata({source.pipeline.vertex.hash,source.pipeline.vertex.variant==1,source.pipeline.negative_one_to_one},Stage::Vertex);
  const auto* pm=source.pipeline.fragment.hash ? shaders_.Metadata({source.pipeline.fragment.hash,source.pipeline.fragment.variant==1},Stage::Fragment) : nullptr;
  const auto used=(vm ? vm->used_texture_mask : 0)|(pm ? pm->used_texture_mask : 0);
  const auto placeholders=[&](const ShaderMetadata* metadata) {
    if(metadata)for(const auto& binding:metadata->bindings)if(binding.kind!=FetchKind::Sampler)
      fetches[binding.slot].images[size_t(binding.kind)]=dummy_images_[size_t(binding.kind)];
  };
  placeholders(vm);placeholders(pm);
  for(size_t i=0;i<fetches.size();++i) {
    if(!(used&(1u<<i)))continue;
    const auto& f=source.fetches[i];
    if(f.image) {
      const auto kind=size_t(f.image->kind);
      if(kind>=4)return Error(error,"Game cube-array sampling needs a matching Metal shader interface");
      fetches[i].images[kind]=ImageFor(f.image,error);if(!fetches[i].images[kind])return false;
    }
    if(f.sampler){fetches[i].sampler=SamplerFor(*f.sampler,error);if(!fetches[i].sampler)return false;}
  }
  if(!vm||!shaders_.BindMetadata(*vm,fetches,result,error))return false;
  if(source.pipeline.fragment.hash&&(!pm||!shaders_.BindMetadata(*pm,fetches,result,error)))return false;
  const auto& v=source.viewport;result.viewport={v[0],v[1],v[2],v[3],v[4],v[5]};
  const auto& s=source.scissor;result.scissor={s[0],s[1],s[2],s[3]};
  result.cull=MTLCullMode(source.cull);result.winding=source.clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise;
  result.stencil_reference=source.stencil_front_reference;result.stencil_back_reference=source.stencil_back_reference;
  result.blend_color=source.blend_color;result.depth_bias=source.depth_bias;result.slope_bias=source.slope_bias;
  result.depth_clamp=source.depth_clamp;result.lines=source.lines;
  draw=std::move(result);error.clear();return true;
}
std::shared_ptr<const Draw> PlanAdapter::Realize(const std::shared_ptr<const render::Capture>& capture,std::string& error) {
  if(!capture){error="Missing immutable game draw plan";return {};}
  if(auto it=prepared_.find(capture.get());it!=prepared_.end()) {
    auto owner=it->second.owner.lock();
    if(owner&&it->second.generation==capture->allocation_generation&&!owner.owner_before(capture)&&!capture.owner_before(owner)){error.clear();return it->second.draw;}
    prepared_.erase(it);
  }
  auto draw=std::make_shared<Draw>();if(!Prepare(*capture,*draw,error))return {};
  prepared_[capture.get()]={capture,draw,capture->allocation_generation};return draw;
}
size_t PlanAdapter::RetireResources(bool bounded) {
  const auto budget=bounded?size_t(512):std::numeric_limits<size_t>::max();
  SweepCacheBuckets(prepared_,prepared_bucket_,budget,[](const auto& entry){
    const auto owner=entry.second.owner.lock();return !owner||owner->allocation_generation!=entry.second.generation;
  });
  SweepCacheBuckets(images_,image_bucket_,bounded?size_t(256):budget,[](const auto& entry){return entry.second.owner.expired();});
  return resources_.SweepRetired(bounded);
}
bool PlanAdapter::BindProduced(const render::Capture& capture,
    const std::array<id<MTLTexture>,26>& produced,Draw& draw,std::string& error) const {
  uint32_t matched=0,requested=0;
  const MTLTextureType kinds[]{MTLTextureType2D,MTLTextureType2DArray,MTLTextureType3D,MTLTextureTypeCube};
  for(size_t slot=0;slot<produced.size();++slot)if(produced[slot]) {
    if(produced[slot].sampleCount!=1||capture.draw.fetches[slot].image)
      return Error(error,"Invalid GPU-produced game fetch replacement");
    requested|=1u<<slot;
  }
  if(!requested){error.clear();return true;}
  // Validate the complete replacement set before changing the draw, without
  // copying/reallocating its texture vector for every GPU-produced fetch.
  struct Replacement {size_t binding=0;id<MTLTexture> __unsafe_unretained texture=nil;};
  std::array<Replacement,62> replacements{};size_t replacement_count=0;
  for(auto stage:{Stage::Vertex,Stage::Fragment}) {
    const auto& shader=stage==Stage::Vertex ? capture.draw.pipeline.vertex : capture.draw.pipeline.fragment;
    if(!shader.hash)continue;
    const auto* metadata=shaders_.Metadata({shader.hash,shader.variant==1,stage==Stage::Vertex&&capture.draw.pipeline.negative_one_to_one},stage);
    if(!metadata)return Error(error,"Missing produced-input shader metadata");
    for(const auto& binding:metadata->bindings) {
      auto texture=produced[binding.slot];const auto kind=size_t(binding.kind);
      if(!texture||kind>=4||texture.textureType!=kinds[kind])continue;
      const auto it=std::find_if(draw.textures.begin(),draw.textures.end(),[&](const auto& b){
        return b.stage==stage&&b.index==binding.index;
      });
      if(it==draw.textures.end())return Error(error,"Missing prepared GPU-produced texture binding");
      if(replacement_count==replacements.size())return Error(error,"Too many GPU-produced Metal texture bindings");
      replacements[replacement_count++]={size_t(it-draw.textures.begin()),texture};matched|=1u<<binding.slot;
    }
  }
  if(matched!=requested)return Error(error,"GPU-produced texture has no matching shader fetch kind");
  for(size_t i=0;i<replacement_count;++i)draw.textures[replacements[i].binding].texture=replacements[i].texture;
  error.clear();return true;
}
}
