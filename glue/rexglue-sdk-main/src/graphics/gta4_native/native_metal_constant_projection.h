#pragma once
#include "native_constant_projection.h"
#include <memory>

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
