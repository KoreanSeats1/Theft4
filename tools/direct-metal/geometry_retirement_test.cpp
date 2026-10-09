#include "native_geometry_retirement.h"
#include "native_frame_scheduling.h"
#include <cassert>
#include <memory>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <iostream>
using namespace rex::graphics::gta4_native;
struct Source { uint32_t last_used=0; std::vector<uint8_t> payload; };
int main() {
  NativeGeometryRetirement queue;
  std::unordered_map<uint32_t,std::shared_ptr<Source>> cache;
  for(uint32_t n=0;n<32768;++n) {
    queue.Track(n);cache[n]=std::make_shared<Source>(Source{10,{uint8_t(n)}});
  }
  // Repeated replacement has exactly one ticket per handle, no stale backlog.
  for(unsigned n=0;n<10000;++n)queue.Track(11);
  assert(queue.Size()==cache.size());
  auto pending=cache[11];
  cache[27]->last_used=605;
  size_t inspected=0,retired=0;
  const auto visit=[&](uint32_t handle) {
    ++inspected;const auto found=cache.find(handle);
    if(found==cache.end())return true;
    const auto& owner=found->second;
    if(!ShouldReclaimNativeBuffer(owner.use_count()==1,false,611,owner->last_used,
                                 kNativeBufferCacheRetentionFrames))return false;
    cache.erase(found);++retired;return true;
  };
  // A hot source and an independently retained generation cannot retire.
  queue.Sweep(32,visit);assert(inspected==32&&retired==30);
  assert(pending->payload==std::vector<uint8_t>({11})&&cache.contains(11)&&cache.contains(27));
  for(unsigned n=0;n<1024;++n) {auto before=inspected;queue.Sweep(32,visit);assert(inspected-before<=32);}
  assert(cache.size()==2&&queue.Size()==2);
  pending.reset();queue.Sweep(32,visit);assert(cache.size()==1&&cache.contains(27));
  // Reset/wrap must not make a recently used source spuriously ancient.
  assert(!ShouldReclaimNativeBuffer(true,false,0,605,kNativeBufferCacheRetentionFrames));
  assert(NativeResourceFrameForBatch(611,0,false)==611);
  assert(NativeResourceFrameForBatch(611,0,true)==0);
  queue.Erase(27);cache.erase(27);assert(queue.Size()==0);
  queue.Track(UINT32_MAX);queue.Track(0);
  std::vector<uint32_t> seen;queue.Sweep(32,[&](uint32_t n){seen.push_back(n);return true;});
  assert(seen.size()==2&&seen[0]!=seen[1]&&queue.Size()==0);
  queue.Clear();
  for(uint32_t n=0;n<32768;++n)queue.Track(n);
  size_t visits=0;auto start=std::chrono::steady_clock::now();
  for(unsigned n=0;n<10000;++n)queue.Sweep(32,[&](uint32_t){++visits;return false;});
  assert(visits==320000&&queue.Size()==32768);
  std::cout<<"32768 live handles, 32 visits/frame, mean sweep us="
      <<std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/10000<<'\n';
}
