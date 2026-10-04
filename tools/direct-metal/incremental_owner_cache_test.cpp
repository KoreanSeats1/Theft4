#include "native_incremental_owner_cache.h"
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <vector>
#include <nlohmann/json.hpp>
#include <fstream>
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
static void RecencyAndPinnedPayloads() {
  Cache cache;std::unordered_map<size_t,Entry> reference;std::vector<size_t> order;
  std::vector<std::shared_ptr<const int>> owners(512);std::mt19937 random(621);
  const auto remove=[&](size_t key){reference.erase(key);std::erase(order,key);};
  for(size_t i=0;i<15000;++i) {
    const auto key=random()%owners.size();
    switch(i%6) {
      case 0:case 1:
        owners[key]=std::make_shared<const int>(int(i));
        cache.Store(key,{owners[key],size_t(random()%4096+1)});
        reference[key]=cache.find(key)->second;std::erase(order,key);order.push_back(key);break;
      case 2: {
        auto it=cache.find(key);cache.Touch(it);
        if(it!=cache.end()){std::erase(order,key);order.push_back(key);}break;
      }
      case 3:cache.erase(key);remove(key);break;
      case 4:
        if(!order.empty()){const auto oldest=order.front();assert(cache.EvictOldest());remove(oldest);}
        else assert(!cache.EvictOldest());break;
      case 5:
        cache.EraseIf([](const auto& e){return e.first%11==0;});
        for(size_t k=0;k<owners.size();k+=11)remove(k);break;
    }
    CheckAccounting(cache);assert(cache.size()==order.size());
    if(order.empty())assert(!cache.Oldest());
    else {const auto& expected=reference.at(order.front());assert(cache.Oldest()->bytes==expected.bytes);
      assert(cache.Oldest()->resource.lock()==expected.resource.lock());}
  }
  Cache moved(std::move(cache));assert(!cache.size()&&!cache.Oldest());
  cache.Store(123,{{},16});cache=std::move(moved);assert(!moved.size()&&!moved.Oldest());
  while(!order.empty()){assert(cache.Oldest()->resource.lock()==reference.at(order.front()).resource.lock());
    assert(cache.EvictOldest());remove(order.front());}
  assert(!cache.size()&&!cache.bytes()&&!cache.Oldest());
  struct Pinned {std::shared_ptr<const std::vector<uint8_t>> payload;};
  struct PinnedSize {size_t operator()(const Pinned& entry)const{return entry.payload->size();}};
  NativeIncrementalOwnerCache<size_t,Pinned,PinnedSize> packets;
  auto bytes=std::make_shared<const std::vector<uint8_t>>(4096,0x73);
  packets.Store(1,{bytes});std::weak_ptr<const std::vector<uint8_t>> lifetime=bytes;
  auto accepted_plan=bytes;bytes.reset();assert(packets.EvictOldest());
  assert(!lifetime.expired()&&accepted_plan->front()==0x73);accepted_plan.reset();assert(lifetime.expired());
  std::cout<<"PASS: recency under replacement/rehash/erasure/moves, exact eviction order and accepted-plan payload pinning\n";
}
static void PressureBenchmark(const char* output) {
  constexpr size_t count=60000,evicted=2000,trials=13;
  std::vector<double> sorted_ms,lru_ms;auto owner=std::make_shared<const int>(1);
  for(size_t trial=0;trial<trials;++trial) {
    Cache cache;std::unordered_map<size_t,Entry> full;
    for(size_t i=0;i<count;++i){cache.Store(i,{owner,4096});full.emplace(i,Entry{owner,4096});}
    // Both methods evict exactly the same oldest entries; construction and
    // payload contents are outside the measured pressure operation.
    auto start=std::chrono::steady_clock::now();
    std::vector<std::pair<size_t,size_t>> candidates;candidates.reserve(full.size());
    for(const auto& [key,entry]:full)candidates.push_back({key,key});
    std::sort(candidates.begin(),candidates.end());
    for(size_t i=0;i<evicted;++i)full.erase(candidates[i].second);
    auto sorted=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    start=std::chrono::steady_clock::now();for(size_t i=0;i<evicted;++i)assert(cache.EvictOldest());
    auto lru=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    assert(full.size()==cache.size()&&cache.bytes()==(count-evicted)*4096);
    for(const auto& [key,entry]:full)assert(cache.find(key)!=cache.end());
    if(trial>=3){sorted_ms.push_back(sorted);lru_ms.push_back(lru);}
  }
  const auto median=[](auto values){std::sort(values.begin(),values.end());return (values[4]+values[5])/2;};
  nlohmann::json report{{"scope","Synthetic host CPU eviction of 2000 oldest packets from 60000 live entries; not M5 game frame time"},
    {"entries",count},{"evicted",evicted},{"sorted_median_ms",median(sorted_ms)},{"lru_median_ms",median(lru_ms)},
    {"sorted_samples_ms",sorted_ms},{"lru_samples_ms",lru_ms},{"identical_retained_keys",true},{"exact_byte_accounting",true}};
  std::ofstream file(output);file<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<'\n';
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
int main(int argc,char** argv){Differential();RecencyAndPinnedPayloads();if(argc>1)Benchmark();if(argc>2)PressureBenchmark(argv[2]);}
