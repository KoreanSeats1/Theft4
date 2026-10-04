#pragma once
#include <cstddef>

namespace rex::graphics::gta4_native {
// For the Metal produced-texture journal only: active capture-map entries and
// queued/current command owner pages retain the actual resource strongly.
// A superseded generation with only its journal reference cannot be captured
// again. Submitted Metal plans retain independent Surface owners; retiring
// this CPU record never releases the last owner of in-flight GPU storage.
// Do not use this for resources discoverable through an external weak cache.
template<class Records,class Generations,class Protected,class Release>
size_t RetireExclusiveNativeGenerations(Records& records,Generations& pending,Protected&& protected_generation,Release&& release) {
  size_t retired=0;
  for(auto it=pending.begin();it!=pending.end();) {
    // A queued generation may not have a Metal record yet. Keep its pending
    // retirement so that recording that command cannot create an orphan.
    if(protected_generation(*it)){++it;continue;}
    auto record=records.find(*it);
    if(record==records.end()){it=pending.erase(it);continue;}
    if(!record->second.resource||record->second.resource.use_count()!=1){++it;continue;}
    release(record->second);records.erase(record);it=pending.erase(it);++retired;
  }
  return retired;
}
} // namespace rex::graphics::gta4_native
