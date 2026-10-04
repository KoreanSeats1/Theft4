#include "native_incremental_owner_cache.h"
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <vector>
using namespace rex::graphics::gta4_native;
struct Entry {std::weak_ptr<const int> resource;size_t bytes=0;};
struct Size {size_t operator()(const Entry& e)const{return e.bytes;}};
struct CollidingHash {size_t operator()(size_t key)const{return key%4;}};
using Cache=NativeIncrementalOwnerCache<size_t,Entry,Size>;
template<class C> void CheckAccounting(const C& cache) {
  size_t sum=0;for(const auto& [key,e]:cache)sum+=e.bytes;assert(sum==cache.bytes());
}
static void Differential() {
  Cache cache;std::mt19937 random(98111);
  std::vector<std::shared_ptr<const int>> owners(500);
  for(size_t i=0;i<10000;++i) {
    const auto key=random()%owners.size();
    switch(i%5) {
      case 0:case 1:
        owners[key]=std::make_shared<const int>(int(i));cache.Store(key,{owners[key],size_t(random()%4096+1)});break;
      case 2:owners[key].reset();break;
      case 3:cache.erase(key);break;
      case 4:cache.Sweep(7,[](const auto& e){return e.second.resource.expired();});break;
    }
    CheckAccounting(cache);
    if(i%23==0){cache.Store(10000+i,{owners[0],64});CheckAccounting(cache);}
  }
  auto moved=std::move(cache);CheckAccounting(moved);
  moved.EraseIf([](const auto& e){return e.first%2==0;});CheckAccounting(moved);
  for(auto& owner:owners)owner.reset();
  for(size_t i=0;i<1000&&moved.size();++i)moved.Sweep(17,[](const auto& e){return e.second.resource.expired();});
  assert(moved.size()==0&&moved.bytes()==0);
  moved.Store(1,{{},57});moved.clear();assert(!moved.size()&&!moved.bytes());
  NativeIncrementalOwnerCache<size_t,Entry,Size,CollidingHash> collisions;
  for(size_t i=0;i<5000;++i)collisions.Store(i,{{},i+1});
  for(size_t i=0;i<1000&&collisions.size();++i)collisions.Sweep(17,[](const auto& e){return e.second.resource.expired();});
  assert(!collisions.size()&&!collisions.bytes());
  std::cout<<"PASS: 10000 differential mutations, exact accounting, rehashes, colliding buckets, retirement and moves\n";
}
static void Benchmark() {
  constexpr size_t entries=30000,frames=300;
  auto owner=std::make_shared<const int>(1);
  std::unordered_map<size_t,Entry> full;Cache incremental;
  for(size_t i=0;i<entries;++i){full[i]={owner,4096};incremental.Store(i,{owner,4096});}
  volatile size_t result=0;
  const auto measure=[&](auto&& run) {
    std::array<double,9> times{};
    for(auto& ms:times){auto start=std::chrono::steady_clock::now();run();ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/frames;}
    std::sort(times.begin(),times.end());return times[4];
  };
  const auto full_ms=measure([&] {
    for(size_t frame=0;frame<frames;++frame) {
      std::erase_if(full,[](const auto& e){return e.second.resource.expired();});
      size_t bytes=0;for(const auto& [key,e]:full)bytes+=e.bytes;result=bytes;
    }
  });
  const auto incremental_ms=measure([&] {
    for(size_t frame=0;frame<frames;++frame) {
      incremental.Sweep(512,[](const auto& e){return e.second.resource.expired();});result=incremental.bytes();
    }
  });
  assert(result==entries*4096);
  std::cout<<"BENCH 30000 live entries: full_scan_ms="<<full_ms<<" incremental_ms="<<incremental_ms<<" bytes="<<result<<'\n';
}
int main(int argc,char**){Differential();if(argc>1)Benchmark();}
