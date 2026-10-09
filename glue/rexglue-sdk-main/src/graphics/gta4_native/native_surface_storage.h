#pragma once
#include <cstdint>
namespace rex::graphics::gta4_native {
enum class NativeSurfaceStorage { Invalid, GuestPlacement, PrivateTarget };
// Zero-pitch host helper targets own private backing storage. Classification
// does not invent a pitch or add these allocations to the guest alias journal.
template<class Descriptor>
constexpr NativeSurfaceStorage NativeSurfaceStorageOf(const Descriptor& descriptor) {
  const uint32_t sample_type=(descriptor.base>>16)&3u;
  if(!descriptor.handle||!descriptor.width||!descriptor.height||sample_type>2)
    return NativeSurfaceStorage::Invalid;
  if(descriptor.base&0x3fffu)return NativeSurfaceStorage::GuestPlacement;
  return descriptor.sample_type==sample_type?NativeSurfaceStorage::PrivateTarget:NativeSurfaceStorage::Invalid;
}
// Placement identity includes extents, but a resolve may read a smaller view
// of the same row layout. Compare layout separately when finding the latest
// writer, so a newer short/narrow write cannot reveal older, larger contents.
template<class Writer,class Requested>
constexpr bool NativeResolveSameRowLayout(const Writer& writer,const Requested& requested) {
  return !writer.depth&&!requested.depth&&writer.sample_pitch&&
      writer.placement_base_tiles==requested.placement_base_tiles&&
      writer.sample_pitch==requested.sample_pitch;
}
// The title floors both axes when reinterpreting 1x storage as a 4x downsample
// view. Odd native-aspect extents therefore crop a column as well as a row.
// The requested sample rectangle must be fully contained; its key stays exact.
template<class View>
constexpr bool NativeResolveContains(const View& writer,const View& requested) {
  return NativeResolveSameRowLayout(writer,requested)&&
      requested.sample_width&&requested.sample_height&&
      writer.sample_width>=requested.sample_width&&
      writer.sample_height>=requested.sample_height;
}
// Coordinates are expressed in the requested sample plane, then mapped into
// the writer's physical allocation. Never stretch a crop over the extra edge.
constexpr uint32_t NativeResolveCoordinate(uint32_t coordinate,uint32_t sample_scale,
    uint32_t writer_sample_extent,uint32_t physical_extent,bool ceil=false) {
  if(!writer_sample_extent)return 0;
  const uint64_t numerator=uint64_t(coordinate)*sample_scale*physical_extent;
  return uint32_t((numerator+(ceil?writer_sample_extent-1:0))/writer_sample_extent);
}
}
