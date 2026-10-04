#include "native_metal_constant_projection.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
using namespace rex::graphics::gta4_native;
int main() {
  using Bytes=std::vector<uint8_t>;
  NativeMetalConstantProjection<Bytes> cache;
  NativeConstantUsage usage;usage.known=true;usage.banks[0][0]=4;
  AuthoritativeConstantState state(4096);
  Bytes canonical(4096);ConstantPayloadDelta delta;CaptureCompleteConstantSnapshot(canonical,delta);
  auto hash=[](std::span<const uint8_t>){return uint64_t(1);};
  auto version=state.Apply(delta,hash).version;
  auto payload=std::make_shared<const Bytes>(canonical);
  cache.Remember(11,12,0,version,usage,payload);
  assert(cache.Find(11,12,0,version,usage)==payload);
  assert(!cache.Find(11,13,0,version,usage));
  assert(!cache.Find(11,12,1,version,usage));
  assert(!cache.Find(11,12,2,version,usage));
  auto unknown=usage;unknown.known=false;assert(!cache.Find(11,12,0,version,unknown));
  auto different=usage;different.banks[0][0]=8;assert(!cache.Find(11,12,0,version,different));
  std::mt19937 random(10496);size_t changed_reuses=0,used_changes=0;
  for(size_t i=0;i<2000;++i) {
    // Frequent unused-register changes, used-register updates and complete
    // snapshot boundaries. Every accepted view must match exact used bytes.
    const size_t offset=(i%7==0?2:3+random()%253)*16;
    for(size_t j=0;j<16;++j)canonical[offset+j]=uint8_t(random());
    delta={};delta.ranges={{uint32_t(offset),0,16}};
    delta.payload.assign(canonical.begin()+offset,canonical.begin()+offset+16);
    if(i%43==0)CaptureCompleteConstantSnapshot(canonical,delta);
    version=state.Apply(delta,hash).version;assert(version);
    auto reused=cache.Find(11,12,0,version,usage);
    if(reused) {
      assert(offset!=32&&i%43!=0);++changed_reuses;
      assert(std::memcmp(reused->data()+32,canonical.data()+32,16)==0);
    } else {
      if(offset==32)++used_changes;
      payload=std::make_shared<const Bytes>(canonical);
      cache.Remember(11,12,0,version,usage,payload);
    }
  }
  assert(changed_reuses>1000&&used_changes>200&&cache.changed_version_hits==changed_reuses);
  // A different owner and an expired ancestor cannot reuse prior payloads.
  auto unrelated=std::make_shared<ConstantStateVersion>();unrelated->byte_size=4096;
  assert(!cache.Find(11,12,0,unrelated,usage));
  std::weak_ptr<const ConstantStateVersion> expired=version;
  version.reset();state=AuthoritativeConstantState(4096);assert(expired.expired());
  assert(!cache.Find(11,12,0,unrelated,usage));
  cache.Clear();assert(!cache.Find(11,12,0,unrelated,usage)&&!cache.hits);
  std::cout<<"Metal shader-specific constant views: 2000 differential updates, unknown/changed masks, used changes, full snapshots, owners and reset passed\n";
}
