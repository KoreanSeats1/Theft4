#include "theft4_render_plan.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace theft4::render {
using nlohmann::json;
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Shader, hash, variant, specialization)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Attribute, location, stream, offset, format)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Stream, stride, per_instance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Blend, enabled, source_rgb, destination_rgb, source_alpha, destination_alpha, rgb, alpha, write_mask)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Stencil, compare, fail, pass, depth_fail, read_mask, write_mask)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Pipeline, vertex, fragment, attributes, streams, colors, blends, depth, stencil, samples, sample_mask, depth_test, depth_write, stencil_test, depth_compare, front, back, negative_one_to_one)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Sampler, min_linear, mag_linear, mip_linear, address, anisotropy, min_lod_bits, max_lod_bits, opaque_white_border)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Mip, level, slice, width, height, depth, row_bytes, image_bytes, offset, size)
namespace {
constexpr size_t kFileLimit = 128 * 1024 * 1024, kBlobLimit = 64 * 1024 * 1024;
bool Error(std::string& error, std::string_view text) { error=text;return false; }
template<class T> bool Enum(T value) { return uint32_t(value)<uint32_t(T::Count); }
bool View(const Buffer& b, uint64_t required) {
  return b.source && b.source->generation && b.source->value.size()<=kBlobLimit &&
      b.offset<=b.source->value.size() && b.length<=b.source->value.size()-b.offset && required<=b.length;
}
uint32_t Width(VertexFormat f) {
  switch(f) {
    case VertexFormat::Float:case VertexFormat::Int:case VertexFormat::UInt:
    case VertexFormat::Half2:case VertexFormat::Short2:case VertexFormat::UShort2:
    case VertexFormat::Short2Normalized:case VertexFormat::UShort2Normalized:
    case VertexFormat::UChar4:case VertexFormat::UChar4Normalized:
    case VertexFormat::UChar4NormalizedBGRA:case VertexFormat::Int1010102Normalized:return 4;
    case VertexFormat::Float2:case VertexFormat::Int2:case VertexFormat::UInt2:
    case VertexFormat::Half4:case VertexFormat::Short4:case VertexFormat::UShort4:
    case VertexFormat::Short4Normalized:case VertexFormat::UShort4Normalized:return 8;
    case VertexFormat::Float3:case VertexFormat::Int3:case VertexFormat::UInt3:return 12;
    case VertexFormat::Float4:case VertexFormat::Int4:case VertexFormat::UInt4:return 16;
    default:return 0;
  }
}
bool StencilValid(const Stencil& s) {
  return Enum(s.compare)&&Enum(s.fail)&&Enum(s.pass)&&Enum(s.depth_fail)&&s.read_mask<=255&&s.write_mask<=255;
}
struct Encoder {
  json blobs=json::array();std::unordered_map<const Bytes*,size_t> indices;
  size_t Source(const std::shared_ptr<const Bytes>& source) {
    if(auto it=indices.find(source.get());it!=indices.end())return it->second;
    const auto i=blobs.size();indices.emplace(source.get(),i);
    blobs.push_back({{"generation",source->generation},{"conversion",source->conversion},
        {"bytes",json::binary(source->value)}});return i;
  }
  json BufferValue(const Buffer& b) {
    return b.source ? json{{"source",Source(b.source)},{"offset",b.offset},{"length",b.length}} : json(nullptr);
  }
  json ImageValue(const Image& i) {
    return {{"source",Source(i.source)},{"format",i.format},{"kind",i.kind},
        {"width",i.width},{"height",i.height},{"depth",i.depth},{"layers",i.layers},
        {"levels",i.levels},{"swizzle",i.swizzle},{"mips",i.mips}};
  }
};
uint64_t U64(const json& v) {
  if(!v.is_number_unsigned())throw std::runtime_error("Capture integer must be unsigned");
  return v.get<uint64_t>();
}
// Reject overflow before nlohmann's integral conversion. Shader hashes are
// deliberately 64-bit; all other non-payload integral fields are 32-bit.
void SmallNumbers(const json& v, size_t depth=0, bool shader_hash=false) {
  if(depth>24)throw std::runtime_error("Capture metadata nesting exceeds the contract");
  if(v.is_object())for(auto it=v.begin();it!=v.end();++it)SmallNumbers(it.value(),depth+1,it.key()=="hash");
  else if(v.is_array()) {
    if(v.size()>2048)throw std::runtime_error("Capture metadata array exceeds the contract");
    for(const auto& item:v)SmallNumbers(item,depth+1);
  } else if(v.is_number_unsigned()) {
    if(!shader_hash && U64(v)>UINT32_MAX)throw std::runtime_error("Capture metadata integer overflow");
  } else if(v.is_number_integer()) {
    const auto n=v.get<int64_t>();
    if(n<INT32_MIN || n>int64_t(UINT32_MAX))throw std::runtime_error("Capture metadata integer overflow");
  } else if(v.is_number_float() && !std::isfinite(v.get<double>()))
    throw std::runtime_error("Nonfinite capture metadata");
}
struct Decoder {
  std::vector<std::shared_ptr<const Bytes>> blobs;
  explicit Decoder(const json& table) {
    if(!table.is_array() || table.size()>128)throw std::runtime_error("Invalid capture resource table");
    size_t total=0;
    for(const auto& item:table) {
      auto bytes=std::make_shared<Bytes>();bytes->generation=U64(item.at("generation"));
      const auto& c=item.at("conversion");
      if(!c.is_array()||c.size()!=4)throw std::runtime_error("Invalid conversion identity");
      for(size_t i=0;i<4;++i)bytes->conversion[i]=U64(c[i]);
      const auto& value=item.at("bytes");
      if(!value.is_binary()||value.get_binary().size()>kBlobLimit)
        throw std::runtime_error("Invalid capture resource bytes");
      total+=value.get_binary().size();
      if(total>kFileLimit)throw std::runtime_error("Capture resources exceed the size limit");
      bytes->value=value.get_binary();blobs.push_back(bytes);
    }
  }
  std::shared_ptr<const Bytes> Source(const json& v) {
    const auto index=U64(v);
    if(index>=blobs.size())throw std::runtime_error("Capture resource reference is out of bounds");
    return blobs[index];
  }
  Buffer BufferValue(const json& v) {
    return v.is_null() ? Buffer{} : Buffer{Source(v.at("source")),U64(v.at("offset")),U64(v.at("length"))};
  }
  std::shared_ptr<const Image> ImageValue(const json& v) {
    auto i=std::make_shared<Image>();i->source=Source(v.at("source"));
    v.at("format").get_to(i->format);v.at("kind").get_to(i->kind);
    v.at("width").get_to(i->width);v.at("height").get_to(i->height);
    v.at("depth").get_to(i->depth);v.at("layers").get_to(i->layers);
    v.at("levels").get_to(i->levels);v.at("swizzle").get_to(i->swizzle);v.at("mips").get_to(i->mips);
    return i;
  }
};
}
bool ValidateFixedPipeline(const Pipeline& p,std::string& error) {
  if(!p.samples||!std::has_single_bit(p.samples)||p.samples>32||!Enum(p.depth_compare)||
     !Enum(p.depth)||!Enum(p.stencil)||!StencilValid(p.front)||!StencilValid(p.back))
    return Error(error,"Invalid fixed pipeline state");
  for(size_t i=0;i<4;++i) {
    const auto& b=p.blends[i];
    if(!Enum(p.colors[i])||!Enum(b.source_rgb)||!Enum(b.destination_rgb)||
        !Enum(b.source_alpha)||!Enum(b.destination_alpha)||!Enum(b.rgb)||!Enum(b.alpha)||b.write_mask>15)
      return Error(error,"Invalid game attachment blend state");
    if(p.colors[i]>=Format::Depth32Float)return Error(error,"Invalid game color target format");
  }
  if((p.depth!=Format::Invalid&&p.depth!=Format::Depth32Float&&p.depth!=Format::Depth32FloatStencil8)||
      (p.stencil!=Format::Invalid&&p.stencil!=Format::Stencil8&&p.stencil!=Format::Depth32FloatStencil8)||
      (p.depth_test&&p.depth==Format::Invalid)||(p.depth_write&&p.depth==Format::Invalid)||
      (p.stencil_test&&p.stencil==Format::Invalid))return Error(error,"Invalid game depth/stencil attachment role");
  error.clear();return true;
}
bool ValidateSampler(const Sampler& s,std::string& error) {
  const float min=std::bit_cast<float>(s.min_lod_bits),max=std::bit_cast<float>(s.max_lod_bits);
  if(!std::isfinite(min)||!std::isfinite(max)||min<0||min>max||!s.anisotropy||s.anisotropy>16)
    return Error(error,"Invalid game sampler bounds");
  for(auto address:s.address)if(!Enum(address))return Error(error,"Invalid game sampler address mode");
  error.clear();return true;
}
bool ValidateImage(const Image& i,std::string& error) {
  if(!i.source||!i.source->generation||i.source->value.empty()||i.source->value.size()>kBlobLimit||
     !Enum(i.format)||i.format==Format::Invalid||!Enum(i.kind)||!i.width||!i.height||
     !i.depth||!i.layers||!i.levels||i.width>16384||i.height>16384||i.depth>2048||i.layers>2048||
     i.levels>15||i.mips.empty()||i.mips.size()>2048)return Error(error,"Invalid game sampled image");
  for(auto swizzle:i.swizzle)if(!Enum(swizzle))return Error(error,"Invalid game image swizzle");
  for(const auto& m:i.mips)if(m.level>=i.levels||!m.width||!m.height||!m.depth||!m.row_bytes||
     !m.image_bytes||m.offset>i.source->value.size()||!m.size||m.size>i.source->value.size()-m.offset)
    return Error(error,"Invalid game image mip payload");
  error.clear();return true;
}
bool AnalyzeIndices(const Buffer& buffer,uint32_t count,uint32_t index_bytes,IndexRange& result,std::string& error) {
  if(!count||(index_bytes!=2&&index_bytes!=4)||!View(buffer,uint64_t(count)*index_bytes)||buffer.offset%index_bytes)
    return Error(error,"Invalid game index view");
  IndexRange range;
  const auto* data=buffer.source->value.data()+buffer.offset;
  const uint32_t marker=index_bytes==2?UINT16_MAX:UINT32_MAX;
  for(uint32_t i=0;i<count;++i) {
    uint32_t index;
    if(index_bytes==2){uint16_t v;memcpy(&v,data+size_t(i)*2,2);index=v;}
    else memcpy(&index,data+size_t(i)*4,4);
    range.minimum=std::min(range.minimum,index);range.maximum=std::max(range.maximum,index);
    if(index==marker)range.has_restart=true;
    else {
      range.has_non_restart=true;
      range.minimum_without_restart=std::min(range.minimum_without_restart,index);
      range.maximum_without_restart=std::max(range.maximum_without_restart,index);
    }
  }
  result=range;error.clear();return true;
}
struct IndexRangeCache::Impl {
  struct Key {
    const Bytes* source;uint64_t generation,offset,size;
    std::array<uint64_t,4> conversion;
    uint32_t count,index_bytes;
    bool operator==(const Key&) const=default;
  };
  struct Hash {
    size_t operator()(const Key& k) const {
      uint64_t h=reinterpret_cast<uintptr_t>(k.source);
      const auto add=[&](uint64_t v){h^=v+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);};
      add(k.generation);add(k.offset);add(k.size);for(auto v:k.conversion)add(v);
      add(k.count);add(k.index_bytes);return size_t(h);
    }
  };
  struct Entry {std::weak_ptr<const Bytes> owner;IndexRange range;};
  std::unordered_map<Key,Entry,Hash> entries;
  uint64_t scanned=0,hits=0;
};
IndexRangeCache::IndexRangeCache():impl_(std::make_unique<Impl>()){}
IndexRangeCache::~IndexRangeCache()=default;
IndexRangeCache::IndexRangeCache(IndexRangeCache&&) noexcept=default;
IndexRangeCache& IndexRangeCache::operator=(IndexRangeCache&&) noexcept=default;
bool IndexRangeCache::Analyze(const Buffer& b,uint32_t count,uint32_t index_bytes,IndexRange& range,std::string& error) {
  if(!count||(index_bytes!=2&&index_bytes!=4)||!View(b,uint64_t(count)*index_bytes)||b.offset%index_bytes)
    return Error(error,"Invalid game index view");
  const Impl::Key key{b.source.get(),b.source->generation,b.offset,b.source->value.size(),b.source->conversion,count,index_bytes};
  if(auto i=impl_->entries.find(key);i!=impl_->entries.end()&&
      !i->second.owner.owner_before(b.source)&&!b.source.owner_before(i->second.owner)) {
    range=i->second.range;++impl_->hits;error.clear();return true;
  }
  if(!AnalyzeIndices(b,count,index_bytes,range,error))return false;
  impl_->scanned+=count;
  if(impl_->entries.size()>=8192)impl_->entries.clear();
  impl_->entries.insert_or_assign(key,Impl::Entry{b.source,range});return true;
}
uint64_t IndexRangeCache::ScannedIndices() const{return impl_->scanned;}
uint64_t IndexRangeCache::Hits() const{return impl_->hits;}
bool DrawResourceValidationCache::CheckImage(const std::shared_ptr<const Image>& image,std::string& error) {
  if(!image)return Error(error,"Missing immutable image for admission");
  auto& e=images_[(reinterpret_cast<uintptr_t>(image.get())>>4)%images_.size()];
  if(e.pointer==image.get()&&!e.owner.owner_before(image)&&!image.owner_before(e.owner)){++hits;return true;}
  ++checks;if(!ValidateImage(*image,error))return false;e={image.get(),image};return true;
}
bool DrawResourceValidationCache::CheckSampler(const std::shared_ptr<const Sampler>& sampler,std::string& error) {
  if(!sampler)return Error(error,"Missing immutable sampler for admission");
  auto& e=samplers_[(reinterpret_cast<uintptr_t>(sampler.get())>>4)%samplers_.size()];
  if(e.pointer==sampler.get()&&!e.owner.owner_before(sampler)&&!sampler.owner_before(e.owner)){++hits;return true;}
  ++checks;if(!ValidateSampler(*sampler,error))return false;e={sampler.get(),sampler};return true;
}
bool Validate(const Capture& capture, std::string& error,DrawValidationIssue* issue,IndexRangeCache* indices,uint64_t* maximum_vertex,DrawResourceValidationCache* resources) {
  if(issue)*issue=DrawValidationIssue::None;
  const auto& d=capture.draw;const auto& p=d.pipeline;
  if(!capture.width||!capture.height||capture.width>16384||capture.height>16384||!p.vertex.hash||
      p.vertex.variant>3||p.fragment.variant>3||p.attributes.size()>31||
      !p.samples||!std::has_single_bit(p.samples)||p.samples>32||!d.instances||
      !Enum(d.primitive)||!Enum(p.depth_compare)||!Enum(p.depth)||!Enum(p.stencil)||
      !StencilValid(p.front)||!StencilValid(p.back)||d.cull>2||
      d.stencil_front_reference>255||d.stencil_back_reference>255)
    return Error(error,"Invalid game draw/pipeline state");
  if(!ValidateFixedPipeline(p,error))return false;
  for(auto color:p.colors)if(!p.fragment.hash&&color!=Format::Invalid)return Error(error,"Depth-only draw has a color target");
  for(double v:d.viewport)if(!std::isfinite(v))return Error(error,"Nonfinite game viewport");
  for(float v:d.blend_color)if(!std::isfinite(v))return Error(error,"Nonfinite game blend color");
  if(!std::isfinite(d.depth_bias)||!std::isfinite(d.slope_bias)||d.viewport[2]<=0||d.viewport[3]<=0||
      d.viewport[4]<0||d.viewport[4]>1||d.viewport[5]<0||d.viewport[5]>1||
      d.scissor[0]>capture.width||d.scissor[1]>capture.height||!d.scissor[2]||!d.scissor[3]||
      d.scissor[2]>capture.width-d.scissor[0]||d.scissor[3]>capture.height-d.scissor[1])
    return Error(error,"Invalid game draw viewport/scissor: target="+std::to_string(capture.width)+"x"+std::to_string(capture.height)+
      " viewport="+std::to_string(d.viewport[0])+","+std::to_string(d.viewport[1])+","+std::to_string(d.viewport[2])+","+std::to_string(d.viewport[3])+
      " depth="+std::to_string(d.viewport[4])+","+std::to_string(d.viewport[5])+
      " scissor="+std::to_string(d.scissor[0])+","+std::to_string(d.scissor[1])+","+std::to_string(d.scissor[2])+","+std::to_string(d.scissor[3]));
  constexpr uint64_t sizes[]{4096,3584,1056};
  for(size_t i=0;i<3;++i)if(d.constant_bytes[i]>sizes[i]||d.constant_bytes[i]%16||
      (i==2&&d.constant_bytes[i]!=sizes[i])||!View(d.constants[i],d.constant_bytes[i])||d.constants[i].offset%16)
    return Error(error,"Invalid game constant bank");
  uint64_t maximum=0;
  if(d.index_count) {
    IndexRange range;
    if(!(indices?indices->Analyze(d.indices,d.index_count,d.index_bytes,range,error):
                 AnalyzeIndices(d.indices,d.index_count,d.index_bytes,range,error)))return false;
    if(d.primitive_restart&&!range.has_non_restart)return Error(error,"Game index view contains only restart markers");
    const auto minimum=d.primitive_restart?range.minimum_without_restart:range.minimum;
    const auto high=d.primitive_restart?range.maximum_without_restart:range.maximum;
    if(int64_t(minimum)+d.base_vertex<0)return Error(error,"Game index references a negative effective vertex");
    maximum=uint64_t(int64_t(high)+d.base_vertex);
  } else {
    if(!d.vertex_count)return Error(error,"Empty game draw");
    maximum=uint64_t(d.first_vertex)+d.vertex_count-1;
  }
  if(maximum_vertex)*maximum_vertex=maximum;
  uint32_t locations=0;
  for(const auto& a:p.attributes) {
    if(a.location>=31||a.stream>=kStreamCount||!Width(a.format)||
        (locations&(1u<<a.location)))return Error(error,"Invalid game vertex interface");
    locations|=1u<<a.location;const auto& s=p.streams[a.stream];
    if(!s.stride||s.stride>16384||uint64_t(a.offset)+Width(a.format)>s.stride)
      return Error(error,"Invalid game vertex layout");
    const uint64_t last=s.per_instance ? uint64_t(d.instances)-1 : maximum;
    const uint64_t needed=last*s.stride+a.offset+Width(a.format);
    if(!View(d.vertices[a.stream],needed)) {
      const auto& buffer=d.vertices[a.stream];
      // Only an otherwise valid immutable view can be classified as a title
      // range fault. Missing owners, malformed offsets and payloads remain
      // fatal validation errors in both live admission and offline replay.
      if(issue&&View(buffer,0)&&needed>buffer.length)*issue=DrawValidationIssue::VertexRange;
      return Error(error,"Game vertex view is shorter than the draw's actual index range: command="+
          std::to_string(capture.command)+" vs="+std::to_string(p.vertex.hash)+" ps="+std::to_string(p.fragment.hash)+
          " stream="+std::to_string(a.stream)+" location="+std::to_string(a.location)+
          " stride="+std::to_string(s.stride)+" attribute-offset="+std::to_string(a.offset)+
          " width="+std::to_string(Width(a.format))+" per-instance="+std::to_string(s.per_instance)+
          " maximum="+std::to_string(maximum)+" base="+std::to_string(d.base_vertex)+
          " indices="+std::to_string(d.index_count)+" index-bytes="+std::to_string(d.index_bytes)+
          " first="+std::to_string(d.first_vertex)+" vertices="+std::to_string(d.vertex_count)+
          " offset="+std::to_string(buffer.offset)+" length="+std::to_string(buffer.length)+
          " source-bytes="+std::to_string(buffer.source?buffer.source->value.size():0)+
          " needed="+std::to_string(needed));
    }
  }
  // A draw has a fixed maximum number of source views. Most use only four
  // distinct payloads; a hash-set allocated several heap nodes on every draw
  // at both frontend and backend admission. Preserve exact de-duplication
  // with bounded stack storage instead.
  std::array<const Bytes*,3+kStreamCount+1+kFetchCount> sources{};size_t source_count=0;
  size_t total=0;
  const auto add=[&](const std::shared_ptr<const Bytes>& b) {
    if(!b)return true;
    if(!b->generation||b->value.empty()||b->value.size()>kBlobLimit)return false;
    if(std::find(sources.begin(),sources.begin()+source_count,b.get())==sources.begin()+source_count) {
      sources[source_count++]=b.get();total+=b->value.size();
    }
    return total<=kFileLimit;
  };
  for(const auto& b:d.constants)if(!add(b.source))return Error(error,"Invalid game resource payload");
  for(const auto& b:d.vertices)if(!add(b.source))return Error(error,"Invalid game resource payload");
  if(!add(d.indices.source))return Error(error,"Invalid game resource payload");
  for(const auto& f:d.fetches) {
    if(f.sampler&&!(resources?resources->CheckSampler(f.sampler,error):ValidateSampler(*f.sampler,error)))return false;
    if(f.image) {
      if(!add(f.image->source))return Error(error,"Invalid game sampled image payload budget");
      if(!(resources?resources->CheckImage(f.image,error):ValidateImage(*f.image,error)))return false;
    }
  }
  error.clear();return true;
}
bool WriteCapture(const std::string& path, const Capture& capture, std::string& error) {
  if(!Validate(capture,error))return false;
  try {
    Encoder encoder;const auto& d=capture.draw;
    json constants=json::array(),vertices=json::array(),fetches=json::array();
    for(const auto& b:d.constants)constants.push_back(encoder.BufferValue(b));
    for(const auto& b:d.vertices)vertices.push_back(encoder.BufferValue(b));
    for(const auto& f:d.fetches)fetches.push_back({{"image",f.image ? encoder.ImageValue(*f.image) : json(nullptr)},
        {"sampler",f.sampler ? json(*f.sampler) : json(nullptr)}});
    json draw={{"pipeline",d.pipeline},{"constants",constants},{"constant_bytes",d.constant_bytes},{"vertices",vertices},{"fetches",fetches},
        {"indices",encoder.BufferValue(d.indices)},{"primitive",d.primitive},{"first_vertex",d.first_vertex},
        {"vertex_count",d.vertex_count},{"instances",d.instances},{"index_count",d.index_count},
        {"index_bytes",d.index_bytes},{"base_vertex",d.base_vertex},{"primitive_restart",d.primitive_restart},
        {"viewport",d.viewport},{"scissor",d.scissor},{"blend_color",d.blend_color},
        {"stencil_front_reference",d.stencil_front_reference},{"stencil_back_reference",d.stencil_back_reference},
        {"cull",d.cull},{"clockwise",d.clockwise},{"lines",d.lines},{"depth_clamp",d.depth_clamp},
        {"depth_bias",d.depth_bias},{"slope_bias",d.slope_bias}};
    json root={{"schema",1u},{"endian",0x04030201u},{"frame",capture.frame},{"command",capture.command},
        {"width",capture.width},{"height",capture.height},{"draw",draw},{"blobs",encoder.blobs}};
    auto bytes=json::to_cbor(root);
    if(bytes.size()>kFileLimit)return Error(error,"Serialized game capture exceeds size limit");
    const std::filesystem::path file(path),temporary(path+".partial");
    if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.close();
    if(!out)return Error(error,"Unable to write game draw capture");
    std::filesystem::rename(temporary,file);error.clear();return true;
  } catch(const std::exception& e){error=e.what();return false;}
}
bool ReadCapture(const std::string& path, Capture& capture, std::string& error) {
  try {
    std::ifstream in(path,std::ios::binary|std::ios::ate);const auto size=in ? in.tellg() : std::streampos(-1);
    if(size<=0||size>kFileLimit)return Error(error,"Missing or oversized game draw capture");
    std::vector<uint8_t> bytes(size_t(size),0);in.seekg(0);
    if(!in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))return Error(error,"Unable to read game draw capture");
    json root=json::from_cbor(bytes);if(U64(root.at("schema"))!=1||U64(root.at("endian"))!=0x04030201u||
        std::endian::native!=std::endian::little)return Error(error,"Unsupported game capture schema or endian");
    Decoder decoder(root.at("blobs"));Capture next;
    const auto& v=root.at("draw");SmallNumbers(v);
    for(const char* key:{"frame","command","width","height"})if(U64(root.at(key))>UINT32_MAX)
      return Error(error,"Capture identity/dimension integer overflow");
    root.at("frame").get_to(next.frame);root.at("command").get_to(next.command);
    root.at("width").get_to(next.width);root.at("height").get_to(next.height);
    auto& d=next.draw;v.at("pipeline").get_to(d.pipeline);
    if(v.contains("constant_bytes")) {
      const auto& bounds=v.at("constant_bytes");
      if(!bounds.is_array()||bounds.size()!=3)return Error(error,"Invalid constant bound count");
      bounds.get_to(d.constant_bytes);
    }
    const auto& c=v.at("constants");const auto& b=v.at("vertices");const auto& f=v.at("fetches");
    if(!c.is_array()||c.size()!=3||!b.is_array()||b.size()!=kStreamCount||!f.is_array()||f.size()!=kFetchCount)
      return Error(error,"Invalid game capture binding counts");
    for(size_t i=0;i<3;++i)d.constants[i]=decoder.BufferValue(c[i]);
    for(size_t i=0;i<kStreamCount;++i)d.vertices[i]=decoder.BufferValue(b[i]);
    for(size_t i=0;i<kFetchCount;++i) {
      if(!f[i].at("image").is_null())d.fetches[i].image=decoder.ImageValue(f[i].at("image"));
      if(!f[i].at("sampler").is_null())d.fetches[i].sampler=std::make_shared<Sampler>(f[i].at("sampler").get<Sampler>());
    }
    d.indices=decoder.BufferValue(v.at("indices"));
#define READ(field) v.at(#field).get_to(d.field)
    READ(primitive);READ(first_vertex);READ(vertex_count);READ(instances);READ(index_count);READ(index_bytes);
    READ(base_vertex);READ(primitive_restart);READ(viewport);READ(scissor);READ(blend_color);
    READ(stencil_front_reference);READ(stencil_back_reference);READ(cull);READ(clockwise);READ(lines);
    READ(depth_clamp);READ(depth_bias);READ(slope_bias);
#undef READ
    if(!Validate(next,error))return false;capture=std::move(next);error.clear();return true;
  } catch(const std::exception& e){error=e.what();return false;}
}
}
