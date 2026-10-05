#include "theft4_metal_resources.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <list>
#include <unordered_map>
#include <mutex>
#include <mach/mach.h>
#include "theft4_bounded_cache_sweep.h"

namespace theft4::metal {
namespace {
// Recycle VM backing only after Metal destroys its buffer, including escaped
// ARC references and pending GPU work. A new Metal buffer gets a new identity;
// no live buffer is overwritten. Each allocation is one page-aligned VM region.
struct UploadPagePool {
  static constexpr size_t capacity=256*1024,limit=16*1024*1024;
  std::mutex mutex;
  std::vector<vm_address_t> free;
  uint64_t allocations=0,reuses=0;
  UploadPagePool(){free.reserve(limit/capacity);}
  ~UploadPagePool(){for(auto address:free)vm_deallocate(mach_task_self(),address,capacity);}
  vm_address_t Acquire() {
    std::lock_guard lock(mutex);
    if(!free.empty()){auto address=free.back();free.pop_back();++reuses;return address;}
    vm_address_t address=0;
    if(vm_allocate(mach_task_self(),&address,capacity,VM_FLAGS_ANYWHERE)!=KERN_SUCCESS)return 0;
    ++allocations;return address;
  }
  void Release(vm_address_t address) {
    std::lock_guard lock(mutex);
    if(free.size()<limit/capacity)free.push_back(address);
    else vm_deallocate(mach_task_self(),address,capacity);
  }
  void Stats(ResourceCacheStats& result) {
    std::lock_guard lock(mutex);result.page_memory_allocations=allocations;
    result.page_memory_reuses=reuses;result.free_page_bytes=free.size()*capacity;
  }
};
struct UploadPageBacking {
  std::shared_ptr<UploadPagePool> pool;vm_address_t address;
  UploadPageBacking(std::shared_ptr<UploadPagePool> p,vm_address_t a):pool(std::move(p)),address(a){}
  ~UploadPageBacking(){pool->Release(address);}
};
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
  struct Allocation;
  using Allocations=std::list<Allocation>;
  using ConstantLru=std::list<Allocations::iterator>;
  struct Allocation {id<MTLBuffer> buffer=nil;std::vector<Key> keys;bool packed=false,constants=false;size_t live_entries=0;ConstantLru::iterator constant_lru;};
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
  ConstantLru constant_lru;
  Allocations::iterator sweep_allocation;
  size_t buffer_bucket=0,texture_bucket=0,uniform_bucket=0;
  size_t buffer_budget;
  size_t constant_budget;
  std::shared_ptr<UploadPagePool> page_pool;
  struct Page {id<MTLBuffer> buffer=nil;NSUInteger used=0;Allocations::iterator allocation;};
  std::array<Page,2> pages; // constants / geometry never pin one another's pages
  std::unordered_map<Key,BufferEntry,KeyHash> buffers;
  std::unordered_map<Key,TextureEntry,KeyHash> textures;
  explicit Impl(Renderer& value,size_t budget,bool recycle):renderer(value),sweep_allocation(allocations.end()),
      buffer_budget(std::max(size_t(256*1024),budget)),
      constant_budget(std::clamp(buffer_budget/4,size_t(256*1024),size_t(32*1024*1024))),
      page_pool(recycle?std::make_shared<UploadPagePool>():nullptr){}
  id<MTLBuffer> NewPage() {
    if(page_pool)if(auto address=page_pool->Acquire()) {
      auto backing=std::make_shared<UploadPageBacking>(page_pool,address);
      auto buffer=[renderer.Device() newBufferWithBytesNoCopy:reinterpret_cast<void*>(address)
          length:UploadPagePool::capacity options:MTLResourceStorageModeShared
          deallocator:^(void*,NSUInteger){(void)backing;}];
      if(buffer)return buffer;
    }
    // Optional recycling must never prevent an otherwise valid allocation.
    return [renderer.Device() newBufferWithLength:UploadPagePool::capacity options:MTLResourceStorageModeShared];
  }
  void Release(Allocations::iterator allocation,bool eviction) {
    if(sweep_allocation==allocation)++sweep_allocation;
    for(auto& page:pages)if(allocation->buffer==page.buffer){page.buffer=nil;page.used=0;}
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
    if(allocation->constants) {
      stats.resident_constant_bytes-=allocation->buffer.length;
      constant_lru.erase(allocation->constant_lru);
      if(eviction)++stats.constant_evictions;
    }
    if(eviction)++stats.buffer_evictions;
    allocations.erase(allocation);
  }
  void Reserve(size_t size,bool constants=false) {
    // A surviving CPU projection can keep one entry in a mostly obsolete
    // constant page alive. Keep these pages on a separate bounded LRU so
    // changing constants cannot consume the geometry working-set budget.
    // Encoded views retain evicted buffers; eviction never changes their bytes.
    if(constants)while(!constant_lru.empty()&&
        stats.resident_constant_bytes>constant_budget-std::min(constant_budget,size))
      Release(constant_lru.front(),true);
    // One oversized source may exceed the budget. It is admitted intact;
    // the next allocation evicts it. No clipping or in-flight overwrite.
    while(!allocations.empty()&&stats.resident_buffer_bytes>buffer_budget-std::min(buffer_budget,size))
      Release(allocations.begin(),true);
  }
  Allocations::iterator Add(id<MTLBuffer> buffer,bool packed,bool constants=false) {
    allocations.push_back({buffer,{},packed,constants});auto i=std::prev(allocations.end());
    if(constants) {
      constant_lru.push_back(i);i->constant_lru=std::prev(constant_lru.end());
      stats.resident_constant_bytes+=buffer.length;
    }
    stats.resident_buffer_bytes+=buffer.length;
    stats.peak_buffer_bytes=std::max(stats.peak_buffer_bytes,stats.resident_buffer_bytes);
    return i;
  }
  bool RetiredConstants(Allocations::iterator allocation,size_t& probes) const {
    if(!allocation->constants)return false;
    // Check complete allocation ownership, not just scattered hash buckets.
    // A live source stops retirement. Independent encoded GPU views retain
    // the Metal buffer; dropping cache ownership never overwrites its bytes.
    for(const auto& key:allocation->keys) {
      if(!probes)return false;
      --probes;
      const auto i=uniforms.find(key);
      if(i!=uniforms.end()&&i->second.allocation==allocation&&!i->second.owner.expired())return false;
    }
    return true;
  }
  void Touch(Allocations::iterator allocation){
    allocations.splice(allocations.end(),allocations,allocation);
    if(allocation->constants)constant_lru.splice(constant_lru.end(),constant_lru,allocation->constant_lru);
  }
};
ResourceCache::ResourceCache(Renderer& renderer,size_t budget,bool recycle):impl_(std::make_unique<Impl>(renderer,budget,recycle)){}
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
  if(found!=impl_->buffers.end()) {
    --found->second.allocation->live_entries;impl_->buffers.erase(found);++impl_->stats.retired;
  }
  impl_->Reserve(bytes.size());
  auto buffer=impl_->renderer.ImmutableBuffer(bytes,error);if (!buffer)return nil;
  auto allocation=impl_->Add(buffer,false);allocation->keys.push_back(key);
  impl_->buffers.insert_or_assign(key,Impl::BufferEntry{version.owner,buffer,bytes.size(),allocation});
  ++allocation->live_entries;
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
void ResourceCache::BeginUploadBatch(){for(auto& page:impl_->pages){page.buffer=nil;page.used=0;}}
BufferView ResourceCache::UniformBuffer(const ResourceVersion& version,std::span<const uint8_t> bytes,std::string& error) {
  if(!version.owner||!version.generation||bytes.empty()||bytes.size()>64*1024) {
    error="Invalid immutable Metal uniform generation";return {};
  }
  return UploadInArena(version,bytes,error,true);
}
BufferView ResourceCache::UploadBuffer(const ResourceVersion& version,std::span<const uint8_t> bytes,std::string& error) {
  return UploadInArena(version,bytes,error,false);
}
BufferView ResourceCache::UploadInArena(const ResourceVersion& version,std::span<const uint8_t> bytes,std::string& error,bool constants) {
  if(!version.owner||!version.generation||bytes.empty()) {error="Invalid immutable Metal upload generation";return {};}
  if(bytes.size()>64*1024) {
    auto buffer=Buffer(version,bytes,error);return {buffer,0,bytes.size()};
  }
  const auto key=Identity(version);
  auto i=impl_->uniforms.find(key);
  if(i!=impl_->uniforms.end()&&SameOwner(i->second.owner,version.owner)) {
    if(i->second.view.length!=bytes.size()){error="Metal uniform generation changed payload size";return {};}
    impl_->Touch(i->second.allocation);
    ++impl_->stats.buffer_hits;error.clear();return i->second.view;
  }
  if(i!=impl_->uniforms.end()) {
    --i->second.allocation->live_entries;impl_->uniforms.erase(i);++impl_->stats.retired;
  }
  constexpr NSUInteger capacity=256*1024,alignment=256;
  auto& page=impl_->pages[constants?0:1];
  const auto offset=(page.used+alignment-1)&~(alignment-1);
  if(!page.buffer||offset>capacity-bytes.size()) {
    impl_->Reserve(capacity,constants);
    page.buffer=impl_->NewPage();
    if(!page.buffer){error="Metal uniform page allocation failed";return {};}
    page.buffer.label=constants?@"Theft4 Immutable Constants":@"Theft4 Immutable Geometry";
    page.allocation=impl_->Add(page.buffer,true,constants);
    page.used=0;++impl_->stats.buffer_creates;
  }else page.used=offset;
  BufferView view{page.buffer,page.used,bytes.size()};
  std::memcpy(static_cast<uint8_t*>(page.buffer.contents)+view.offset,bytes.data(),bytes.size());
  page.used+=bytes.size();
  page.allocation->keys.push_back(key);impl_->Touch(page.allocation);
  impl_->uniforms.insert_or_assign(key,Impl::UniformEntry{version.owner,view,page.allocation});
  ++page.allocation->live_entries;
  impl_->stats.uploaded_bytes+=bytes.size();error.clear();return view;
}
size_t ResourceCache::SweepRetired(bool bounded) {
  const auto retired_before=impl_->stats.retired;size_t removed=0;
  const auto budget=bounded?size_t(1024):std::numeric_limits<size_t>::max();
  const auto expired=[](const auto& entry){
    if(!entry.second.owner.expired())return false;
    --entry.second.allocation->live_entries;return true;
  };
  removed+=SweepCacheBuckets(impl_->buffers,impl_->buffer_bucket,budget,expired);
  removed+=SweepCacheBuckets(impl_->uniforms,impl_->uniform_bucket,budget,expired);
  removed+=SweepCacheBuckets(impl_->textures,impl_->texture_bucket,bounded?size_t(256):budget,
    [](const auto& entry){return entry.second.owner.expired();});
  // Entry removal and owner replacement update exact page counts. Retiring
  // a page needs no repeated search through its historical keys.
  const auto allocations=std::min(impl_->allocations.size(),bounded?size_t(256):impl_->allocations.size());
  size_t constant_probes=bounded?size_t(1024):std::numeric_limits<size_t>::max();
  for(size_t n=0;n<allocations&&!impl_->allocations.empty();++n) {
    if(impl_->sweep_allocation==impl_->allocations.end())impl_->sweep_allocation=impl_->allocations.begin();
    const auto allocation=impl_->sweep_allocation++;
    if(!allocation->live_entries||impl_->RetiredConstants(allocation,constant_probes))impl_->Release(allocation,false);
  }
  impl_->stats.retired+=removed;return impl_->stats.retired-retired_before;
}
void ResourceCache::Clear(){impl_->stats.retired+=impl_->buffers.size()+impl_->textures.size()+impl_->uniforms.size();impl_->buffers.clear();impl_->textures.clear();impl_->uniforms.clear();impl_->constant_lru.clear();impl_->allocations.clear();impl_->sweep_allocation=impl_->allocations.end();impl_->buffer_bucket=impl_->texture_bucket=impl_->uniform_bucket=0;impl_->stats.resident_buffer_bytes=impl_->stats.resident_constant_bytes=0;BeginUploadBatch();}
size_t ResourceCache::BufferCount()const{return impl_->buffers.size()+impl_->uniforms.size();}
size_t ResourceCache::TextureCount()const{return impl_->textures.size();}
ResourceCacheStats ResourceCache::Stats()const{auto result=impl_->stats;if(impl_->page_pool)impl_->page_pool->Stats(result);return result;}
}
