#include "theft4_metal_resources.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <list>
#include <unordered_map>

namespace theft4::metal {
namespace {
struct Key {
  const void* owner = nullptr;
  uint64_t generation = 0;
  std::array<uint64_t,4> conversion{};
  bool operator==(const Key&) const = default;
};
struct KeyHash {
  size_t operator()(const Key& key) const noexcept {
    uint64_t hash = uint64_t(reinterpret_cast<uintptr_t>(key.owner)) ^ key.generation;
    for (uint64_t word : key.conversion)
      hash ^= word + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    hash ^= hash >> 33; hash *= 0xff51afd7ed558ccdull; hash ^= hash >> 33;
    return size_t(hash);
  }
};
Key Identity(const ResourceVersion& version) {
  return {version.owner.get(),version.generation,version.conversion};
}
bool SameOwner(const std::weak_ptr<const void>& cached, const std::shared_ptr<const void>& owner) {
  // An equal live shared owner already proves the weak owner is not expired.
  return !cached.owner_before(owner) && !owner.owner_before(cached);
}
struct Shape {
  MTLTextureType type;
  MTLPixelFormat format;
  NSUInteger width,height,depth,levels,layers,samples;
  MTLStorageMode storage;
  MTLTextureUsage usage;
  MTLHazardTrackingMode hazard;
  std::array<MTLTextureSwizzle,4> swizzle;
  bool operator==(const Shape&) const = default;
};
Shape TextureShape(MTLTextureDescriptor* d) {
  return {d.textureType,d.pixelFormat,d.width,d.height,d.depth,d.mipmapLevelCount,d.arrayLength,
          d.sampleCount,d.storageMode,d.usage,d.hazardTrackingMode,
          {d.swizzle.red,d.swizzle.green,d.swizzle.blue,d.swizzle.alpha}};
}
struct Block { NSUInteger width=1,height=1,bytes=0; };
Block Layout(MTLPixelFormat format) {
  switch(format) {
    case MTLPixelFormatR8Unorm: case MTLPixelFormatR8Snorm:
    case MTLPixelFormatR8Uint: case MTLPixelFormatR8Sint: return {1,1,1};
    case MTLPixelFormatRG8Unorm: case MTLPixelFormatRG8Snorm:
    case MTLPixelFormatRG8Uint: case MTLPixelFormatRG8Sint:
    case MTLPixelFormatR16Unorm: case MTLPixelFormatR16Snorm:
    case MTLPixelFormatR16Uint: case MTLPixelFormatR16Sint: case MTLPixelFormatR16Float: return {1,1,2};
    case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB:
    case MTLPixelFormatRGBA8Snorm: case MTLPixelFormatRGBA8Uint: case MTLPixelFormatRGBA8Sint:
    case MTLPixelFormatBGRA8Unorm: case MTLPixelFormatBGRA8Unorm_sRGB:
    case MTLPixelFormatRG16Unorm: case MTLPixelFormatRG16Snorm:
    case MTLPixelFormatRG16Uint: case MTLPixelFormatRG16Sint: case MTLPixelFormatRG16Float:
    case MTLPixelFormatR32Uint: case MTLPixelFormatR32Sint: case MTLPixelFormatR32Float:
    case MTLPixelFormatRGB10A2Unorm: case MTLPixelFormatRGB10A2Uint:
    case MTLPixelFormatRG11B10Float: case MTLPixelFormatRGB9E5Float: return {1,1,4};
    case MTLPixelFormatRGBA16Unorm: case MTLPixelFormatRGBA16Snorm:
    case MTLPixelFormatRGBA16Uint: case MTLPixelFormatRGBA16Sint: case MTLPixelFormatRGBA16Float:
    case MTLPixelFormatRG32Uint: case MTLPixelFormatRG32Sint: case MTLPixelFormatRG32Float: return {1,1,8};
    case MTLPixelFormatRGBA32Uint: case MTLPixelFormatRGBA32Sint: case MTLPixelFormatRGBA32Float: return {1,1,16};
    case MTLPixelFormatBC1_RGBA: case MTLPixelFormatBC1_RGBA_sRGB:
    case MTLPixelFormatBC4_RUnorm: case MTLPixelFormatBC4_RSnorm: return {4,4,8};
    case MTLPixelFormatBC2_RGBA: case MTLPixelFormatBC2_RGBA_sRGB:
    case MTLPixelFormatBC3_RGBA: case MTLPixelFormatBC3_RGBA_sRGB:
    case MTLPixelFormatBC5_RGUnorm: case MTLPixelFormatBC5_RGSnorm:
    case MTLPixelFormatBC6H_RGBFloat: case MTLPixelFormatBC6H_RGBUfloat:
    case MTLPixelFormatBC7_RGBAUnorm: case MTLPixelFormatBC7_RGBAUnorm_sRGB:
    case MTLPixelFormatASTC_4x4_LDR: case MTLPixelFormatASTC_4x4_sRGB: return {4,4,16};
    default: return {};
  }
}
bool Multiply(size_t a,size_t b,size_t& result) {
  if (b && a>std::numeric_limits<size_t>::max()/b) return false;
  result=a*b;return true;
}
bool Validate(const Shape& s,std::span<const uint8_t> bytes,std::span<const TextureUpload> uploads,
              std::string& error) {
  auto reject=[&](const char* reason){error=reason;return false;};
  const auto block=Layout(s.format);
  const bool cube=s.type==MTLTextureTypeCube || s.type==MTLTextureTypeCubeArray;
  const bool array=s.type==MTLTextureType2DArray || s.type==MTLTextureTypeCubeArray;
  const bool volume=s.type==MTLTextureType3D;
  if (!block.bytes || (!cube && !array && !volume && s.type!=MTLTextureType2D) ||
      !s.width || !s.height || !s.depth || !s.layers || !s.levels || s.samples!=1 ||
      s.width>16384 || s.height>16384 || s.depth>2048 || s.layers>2048 ||
      s.storage!=MTLStorageModeShared || s.usage!=MTLTextureUsageShaderRead ||
      (volume && (s.width>2048 || s.height>2048)) || (cube && s.layers*6>2048) ||
      (!volume && s.depth!=1) || (!array && s.layers!=1) ||
      (cube && s.width!=s.height) || (volume && block.width!=1))
    return reject("Unsupported immutable Metal texture shape");
  const NSUInteger largest=std::max({s.width,s.height,s.depth});
  if (s.levels>std::bit_width(largest)) return reject("Too many immutable Metal texture mip levels");
  const NSUInteger slices=cube ? s.layers*6 : array ? s.layers : 1;
  if (uploads.size()!=slices*s.levels) return reject("Immutable Metal texture requires every mip and slice");
  std::vector<bool> seen(uploads.size());
  for (const auto& u:uploads) {
    if (u.level>=s.levels || u.slice>=slices ||
        u.width!=std::max(NSUInteger(1),s.width>>u.level) ||
        u.height!=std::max(NSUInteger(1),s.height>>u.level) ||
        u.depth!=(volume ? std::max(NSUInteger(1),s.depth>>u.level) : 1) ||
        seen[u.slice*s.levels+u.level]) return reject("Invalid or repeated immutable Metal texture subresource");
    seen[u.slice*s.levels+u.level]=true;
    const size_t columns=(u.width+block.width-1)/block.width, rows=(u.height+block.height-1)/block.height;
    size_t row_bytes=0,image_bytes=0,preceding_images=0,preceding_rows=0;
    if (!Multiply(columns,block.bytes,row_bytes) || u.bytes_per_row<row_bytes ||
        u.bytes_per_row%block.bytes || !Multiply(u.bytes_per_row,rows,image_bytes) ||
        (volume && (u.bytes_per_image<image_bytes || u.bytes_per_image%block.bytes)) ||
        (!volume && u.bytes_per_image!=0) ||
        !Multiply(u.depth-1,u.bytes_per_image,preceding_images) ||
        !Multiply(rows-1,u.bytes_per_row,preceding_rows) ||
        preceding_images>std::numeric_limits<size_t>::max()-preceding_rows ||
        row_bytes>std::numeric_limits<size_t>::max()-preceding_images-preceding_rows ||
        u.offset>bytes.size() || u.size>bytes.size()-u.offset ||
        preceding_images+preceding_rows+row_bytes>u.size)
      return reject("Invalid immutable Metal texture payload range/pitch");
  }
  return true;
}
}
struct ResourceCache::Impl {
  struct Allocation {id<MTLBuffer> buffer=nil;std::vector<Key> keys;bool packed=false;};
  using Allocations=std::list<Allocation>;
  struct BufferEntry { std::weak_ptr<const void> owner; id<MTLBuffer> buffer=nil; size_t size=0;Allocations::iterator allocation; };
  struct TextureEntry {
    std::weak_ptr<const void> owner;
    id<MTLTexture> texture=nil;
    Shape shape{};
    size_t size=0;
    std::vector<TextureUpload> uploads;
  };
  Renderer& renderer;
  ResourceCacheStats stats;
  struct UniformEntry {std::weak_ptr<const void> owner;BufferView view;Allocations::iterator allocation;};
  std::unordered_map<Key,UniformEntry,KeyHash> uniforms;
  Allocations allocations;
  size_t buffer_budget;
  id<MTLBuffer> uniform_page=nil;
  NSUInteger uniform_used=0;
  Allocations::iterator current_page;
  std::unordered_map<Key,BufferEntry,KeyHash> buffers;
  std::unordered_map<Key,TextureEntry,KeyHash> textures;
  explicit Impl(Renderer& value,size_t budget):renderer(value),buffer_budget(std::max(size_t(256*1024),budget)){}
  void Release(Allocations::iterator allocation,bool eviction) {
    if(allocation->buffer==uniform_page){uniform_page=nil;uniform_used=0;}
    for(const auto& key:allocation->keys) {
      if(allocation->packed) {
        auto i=uniforms.find(key);
        if(i!=uniforms.end()&&i->second.allocation==allocation){uniforms.erase(i);++stats.retired;}
      }else {
        auto i=buffers.find(key);
        if(i!=buffers.end()&&i->second.allocation==allocation){buffers.erase(i);++stats.retired;}
      }
    }
    stats.resident_buffer_bytes-=allocation->buffer.length;
    if(eviction)++stats.buffer_evictions;
    allocations.erase(allocation);
  }
  void Reserve(size_t size) {
    // One oversized source may exceed the budget. It is admitted intact;
    // the next allocation evicts it. No clipping or in-flight overwrite.
    while(!allocations.empty()&&stats.resident_buffer_bytes>buffer_budget-std::min(buffer_budget,size))
      Release(allocations.begin(),true);
  }
  Allocations::iterator Add(id<MTLBuffer> buffer,bool packed) {
    allocations.push_back({buffer,{},packed});auto i=std::prev(allocations.end());
    stats.resident_buffer_bytes+=buffer.length;
    stats.peak_buffer_bytes=std::max(stats.peak_buffer_bytes,stats.resident_buffer_bytes);
    return i;
  }
  void Touch(Allocations::iterator allocation){allocations.splice(allocations.end(),allocations,allocation);}
};
ResourceCache::ResourceCache(Renderer& renderer,size_t budget):impl_(std::make_unique<Impl>(renderer,budget)){}
ResourceCache::~ResourceCache()=default;
id<MTLBuffer> ResourceCache::Buffer(const ResourceVersion& version,std::span<const uint8_t> bytes,
                                   std::string& error) {
  if (!version.owner || !version.generation || bytes.empty()) {
    error="Invalid immutable Metal buffer generation";return nil;
  }
  const Key key=Identity(version);
  auto found=impl_->buffers.find(key);
  if (found!=impl_->buffers.end() && SameOwner(found->second.owner,version.owner)) {
    if (found->second.size!=bytes.size()) {error="Metal buffer generation changed its payload size";return nil;}
    impl_->Touch(found->second.allocation);
    ++impl_->stats.buffer_hits;error.clear();return found->second.buffer;
  }
  impl_->Reserve(bytes.size());
  auto buffer=impl_->renderer.ImmutableBuffer(bytes,error);if (!buffer)return nil;
  auto allocation=impl_->Add(buffer,false);allocation->keys.push_back(key);
  impl_->buffers.insert_or_assign(key,Impl::BufferEntry{version.owner,buffer,bytes.size(),allocation});
  ++impl_->stats.buffer_creates;impl_->stats.uploaded_bytes+=bytes.size();error.clear();return buffer;
}
id<MTLTexture> ResourceCache::Texture(const ResourceVersion& version,MTLTextureDescriptor* descriptor,
    std::span<const uint8_t> bytes,std::span<const TextureUpload> uploads,std::string& error) {
  if (!version.owner || !version.generation || !descriptor || bytes.empty()) {
    error="Invalid immutable Metal texture generation";return nil;
  }
  const Key key=Identity(version);const Shape shape=TextureShape(descriptor);
  auto found=impl_->textures.find(key);
  if (found!=impl_->textures.end() && SameOwner(found->second.owner,version.owner)) {
    const auto& cached=found->second;
    if (cached.shape!=shape || cached.size!=bytes.size() || cached.uploads.size()!=uploads.size() ||
        !std::equal(uploads.begin(),uploads.end(),cached.uploads.begin())) {
      error="Metal texture generation changed its upload/format identity";return nil;
    }
    ++impl_->stats.texture_hits;error.clear();return cached.texture;
  }
  if (!Validate(shape,bytes,uploads,error))return nil;
  auto texture=impl_->renderer.Texture(descriptor,error);if (!texture)return nil;
  for (const auto& u:uploads) {
    [texture replaceRegion:MTLRegionMake3D(0,0,0,u.width,u.height,u.depth)
        mipmapLevel:u.level slice:u.slice withBytes:bytes.data()+u.offset
        bytesPerRow:u.bytes_per_row bytesPerImage:u.bytes_per_image];
  }
  impl_->textures.insert_or_assign(key,Impl::TextureEntry{version.owner,texture,shape,bytes.size(),
      std::vector<TextureUpload>(uploads.begin(),uploads.end())});
  ++impl_->stats.texture_creates;impl_->stats.uploaded_bytes+=bytes.size();error.clear();return texture;
}
void ResourceCache::BeginUploadBatch(){impl_->uniform_page=nil;impl_->uniform_used=0;}
BufferView ResourceCache::UniformBuffer(const ResourceVersion& version,std::span<const uint8_t> bytes,std::string& error) {
  if(!version.owner||!version.generation||bytes.empty()||bytes.size()>64*1024) {
    error="Invalid immutable Metal uniform generation";return {};
  }
  return UploadBuffer(version,bytes,error);
}
BufferView ResourceCache::UploadBuffer(const ResourceVersion& version,std::span<const uint8_t> bytes,std::string& error) {
  if(!version.owner||!version.generation||bytes.empty()) {error="Invalid immutable Metal upload generation";return {};}
  if(bytes.size()>64*1024) {
    auto buffer=Buffer(version,bytes,error);return {buffer,0,bytes.size()};
  }
  const auto key=Identity(version);
  if(auto i=impl_->uniforms.find(key);i!=impl_->uniforms.end()&&SameOwner(i->second.owner,version.owner)) {
    if(i->second.view.length!=bytes.size()){error="Metal uniform generation changed payload size";return {};}
    impl_->Touch(i->second.allocation);
    ++impl_->stats.buffer_hits;error.clear();return i->second.view;
  }
  constexpr NSUInteger capacity=256*1024,alignment=256;
  const auto offset=(impl_->uniform_used+alignment-1)&~(alignment-1);
  if(!impl_->uniform_page||offset>capacity-bytes.size()) {
    impl_->Reserve(capacity);
    impl_->uniform_page=[impl_->renderer.Device() newBufferWithLength:capacity options:MTLResourceStorageModeShared];
    if(!impl_->uniform_page){error="Metal uniform page allocation failed";return {};}
    impl_->uniform_page.label=@"Theft4 Immutable Upload Page";
    impl_->current_page=impl_->Add(impl_->uniform_page,true);
    impl_->uniform_used=0;++impl_->stats.buffer_creates;
  }else impl_->uniform_used=offset;
  BufferView view{impl_->uniform_page,impl_->uniform_used,bytes.size()};
  std::memcpy(static_cast<uint8_t*>(impl_->uniform_page.contents)+view.offset,bytes.data(),bytes.size());
  impl_->uniform_used+=bytes.size();
  impl_->current_page->keys.push_back(key);impl_->Touch(impl_->current_page);
  impl_->uniforms.insert_or_assign(key,Impl::UniformEntry{version.owner,view,impl_->current_page});
  impl_->stats.uploaded_bytes+=bytes.size();error.clear();return view;
}
size_t ResourceCache::SweepRetired() {
  const auto retired_before=impl_->stats.retired;size_t removed=0;
  for (auto i=impl_->buffers.begin();i!=impl_->buffers.end();) {
    if(i->second.owner.expired()){i=impl_->buffers.erase(i);++removed;}else ++i;
  }
  for (auto i=impl_->textures.begin();i!=impl_->textures.end();) {
    if(i->second.owner.expired()){i=impl_->textures.erase(i);++removed;}else ++i;
  }
  for(auto i=impl_->uniforms.begin();i!=impl_->uniforms.end();) {
    if(i->second.owner.expired()){i=impl_->uniforms.erase(i);++removed;}else ++i;
  }
  // A page remains resident only while at least one cached generation uses
  // it. The page keys can outlive an expired owner, so check the current entry
  // and allocation identity before deciding that the page is still live.
  for(auto i=impl_->allocations.begin();i!=impl_->allocations.end();) {
    const auto allocation=i++;
    const bool live=std::any_of(allocation->keys.begin(),allocation->keys.end(),[&](const Key& key){
      if(allocation->packed){auto e=impl_->uniforms.find(key);return e!=impl_->uniforms.end()&&e->second.allocation==allocation;}
      auto e=impl_->buffers.find(key);return e!=impl_->buffers.end()&&e->second.allocation==allocation;
    });
    if(!live)impl_->Release(allocation,false);
  }
  impl_->stats.retired+=removed;return impl_->stats.retired-retired_before;
}
void ResourceCache::Clear(){impl_->stats.retired+=impl_->buffers.size()+impl_->textures.size()+impl_->uniforms.size();impl_->buffers.clear();impl_->textures.clear();impl_->uniforms.clear();impl_->allocations.clear();impl_->stats.resident_buffer_bytes=0;BeginUploadBatch();}
size_t ResourceCache::BufferCount()const{return impl_->buffers.size()+impl_->uniforms.size();}
size_t ResourceCache::TextureCount()const{return impl_->textures.size();}
ResourceCacheStats ResourceCache::Stats()const{return impl_->stats;}
}
