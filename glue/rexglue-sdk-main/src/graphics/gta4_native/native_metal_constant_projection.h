#pragma once
#include "native_constant_projection.h"
#include "native_masked_constants.h"
#include <memory>
#include <utility>

namespace rex::graphics::gta4_native {
// Worker-owned shader-specific views. Reused payloads are never registered as
// the full contents of a different authoritative constant version. Unknown
// reflection, changed used registers, expired ancestors and long chains miss.
template<class Payload> class NativeMetalConstantProjection {
 public:
  std::shared_ptr<const Payload> Find(uint64_t vertex,uint64_t pixel,size_t bank,
      const std::shared_ptr<const ConstantStateVersion>& version,const NativeConstantUsage& usage) {
    if(!usage.known||bank>=2||!version)return {};
    auto& e=entries_[bank][Slot(vertex,pixel)];
    auto previous=e.version.lock();
    if(e.vertex!=vertex||e.pixel!=pixel||e.mask!=usage.banks[bank]||!e.payload||
       !CanReuseConstantProjection(previous.get(),version.get(),e.mask))return {};
    ++hits;if(previous!=version)++changed_version_hits;
    e.version=version;return e.payload;
  }
  void Remember(uint64_t vertex,uint64_t pixel,size_t bank,
      const std::shared_ptr<const ConstantStateVersion>& version,const NativeConstantUsage& usage,
      std::shared_ptr<const Payload> payload) {
    if(usage.known&&bank<2&&version&&payload)
      entries_[bank][Slot(vertex,pixel)]={vertex,pixel,usage.banks[bank],version,std::move(payload)};
  }
  // Replay into a new, zero-initialized ABI prefix covering every known read.
  // Unknown reflection retains the full-bank fallback. The previous host
  // bytes are usable only for this exact shader/mask, and both owners remain
  // alive until Write completes. Sparse payloads never become authoritative
  // guest materializations or enter a cache keyed by the version alone.
  template<class ViewPayload,class CopyGuest>
  uint64_t WriteMasked(uint64_t vertex,uint64_t pixel,size_t bank,
      const std::shared_ptr<const ConstantStateVersion>& version,const NativeConstantUsage& usage,
      std::span<uint8_t> output,ViewPayload&& view_payload,CopyGuest&& copy_guest) const {
    if(!usage.known||bank>=2||!version||output.size()>version->byte_size||
        output.size()<NativeMaskedConstantExtent(usage.banks[bank]))return 0;
    const auto& e=entries_[bank][Slot(vertex,pixel)];
    std::shared_ptr<const ConstantStateVersion> previous;
    std::shared_ptr<const Payload> payload;
    std::span<const uint8_t> host;
    if(e.vertex==vertex&&e.pixel==pixel&&e.mask==usage.banks[bank]&&e.payload) {
      previous=e.version.lock();
      if(previous){payload=e.payload;host=view_payload(*payload);}
    }
    NativeMaskedConstantPlan plan;
    if(!plan.Build(version.get(),usage.banks[bank],previous.get(),host))return 0;
    return plan.Write(output,std::forward<CopyGuest>(copy_guest));
  }
  void Clear(){entries_={};hits=changed_version_hits=0;}
  uint64_t hits=0,changed_version_hits=0;
 private:
  static size_t Slot(uint64_t vertex,uint64_t pixel){return (vertex^(pixel>>7))%256;}
  struct Entry {
    uint64_t vertex=0,pixel=0;NativeConstantMask mask{};
    std::weak_ptr<const ConstantStateVersion> version;
    std::shared_ptr<const Payload> payload;
  };
  std::array<std::array<Entry,256>,2> entries_{};
};
}
