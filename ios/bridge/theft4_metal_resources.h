#pragma once
#include "theft4_native_metal.h"
#include <algorithm>

namespace theft4::metal {
struct ResourceVersion {
  // The source generation remains immutable. The game adapter supplies the
  // same owner and conversion identity while that generation is unchanged.
  std::shared_ptr<const void> owner;
  uint64_t generation = 0;
  std::array<uint64_t, 4> conversion{};
};
struct TextureUpload {
  NSUInteger level = 0, slice = 0;
  NSUInteger width = 0, height = 0, depth = 1;
  NSUInteger bytes_per_row = 0, bytes_per_image = 0;
  size_t offset = 0, size = 0;
  bool operator==(const TextureUpload&) const = default;
};
struct ResourceCacheStats {
  uint64_t buffer_creates = 0, texture_creates = 0;
  uint64_t buffer_hits = 0, texture_hits = 0, retired = 0, uploaded_bytes = 0;
  uint64_t resident_buffer_bytes=0,peak_buffer_bytes=0,buffer_evictions=0;
  uint64_t resident_constant_bytes=0,constant_evictions=0;
  uint64_t page_memory_allocations=0,page_memory_reuses=0,free_page_bytes=0;
  uint64_t prepared_view_hits=0,prepared_view_misses=0;
  uint64_t frame_upload_buffer_creates=0,frame_upload_buffer_reuses=0,frame_upload_resident_bytes=0,frame_uploaded_bytes=0;
};
// Render-worker-only transient constants. Unlike ResourceCache, these views
// are valid only while their batch lease is held. The live frame adapter keeps
// that lease through both CPU encoding and GPU completion. Escaping immutable
// draws continue to use ResourceCache and never enter this pool.
class FrameUploadPool {
 public:
  struct Impl;
  class Batch {
   public:
    ~Batch();
    Batch(const Batch&)=delete;
    Batch& operator=(const Batch&)=delete;
    // A full bounded pool returns an empty view; the caller can use the
    // immutable upload path without waiting, dropping work or growing forever.
    BufferView TryUpload(std::span<const uint8_t>);
   private:
    friend class FrameUploadPool;
    Batch(std::shared_ptr<Impl>,size_t);
    std::shared_ptr<Impl> pool_;
    size_t slot_=0,page_=0,offset_=0;
  };
  // Keep the older-device allocation split: 32 MiB transient constants out
  // of 128 MiB total, leaving 96 MiB for immutable uploads. High-memory
  // devices retain their larger, separately measured bounded working set.
  static size_t Budget(size_t total) { return std::min(total/4,size_t(96*1024*1024)); }
  FrameUploadPool(Renderer&,size_t budget);
  std::shared_ptr<Batch> Acquire();
  void AddStats(ResourceCacheStats&) const;
 private:
  std::shared_ptr<Impl> impl_;
};
class ResourceCache {
 public:
  explicit ResourceCache(Renderer& renderer,size_t buffer_budget=128*1024*1024,bool recycle_pages=true);
  ~ResourceCache();
  ResourceCache(const ResourceCache&) = delete;
  ResourceCache& operator=(const ResourceCache&) = delete;
  id<MTLBuffer> Buffer(const ResourceVersion& version, std::span<const uint8_t> bytes,
                       std::string& error);
  // CPU-converted, sampled-only immutable images. Render targets and aliases
  // stay under the ordered pass owner, outside this cache. Uploads happen once
  // per source/conversion generation; no GPU wait or shader conversion occurs.
  id<MTLTexture> Texture(const ResourceVersion& version, MTLTextureDescriptor* descriptor,
                         std::span<const uint8_t> bytes, std::span<const TextureUpload> uploads,
                         std::string& error);
  // Call at resource retirement boundaries, rather than scanning on each draw.
  // Append-only pages reduce driver allocations for small immutable constants.
  // BeginUploadBatch releases only the CPU's current page; encoded buffers and
  // live resource versions retain their pages through GPU completion.
  void BeginUploadBatch();
  BufferView UniformBuffer(const ResourceVersion&,std::span<const uint8_t>,std::string& error);
  // Geometry and indices use a separate append-only arena from constants. Every
  // view is immutable; cache eviction releases ownership, never reuses bytes
  // in a buffer that an encoded or pending frame can still retain.
  BufferView UploadBuffer(const ResourceVersion&,std::span<const uint8_t>,std::string& error);
  size_t SweepRetired(bool bounded=false);
  void Clear();
  size_t BufferCount() const;
  size_t TextureCount() const;
  ResourceCacheStats Stats() const;
 private:
  BufferView UploadInArena(const ResourceVersion&,std::span<const uint8_t>,std::string&,bool constants);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
