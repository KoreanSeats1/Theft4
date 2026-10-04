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
}
