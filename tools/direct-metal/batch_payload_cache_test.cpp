#include "native_batch_payload_cache.h"
#include "native_incremental_owner_cache.h"
#include <array>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace rex::graphics::gta4_native;
struct Payload {std::vector<uint8_t> value;};
using Key=std::array<uint64_t,8>;
struct Hash {size_t operator()(const Key& k)const{size_t h=0;for(auto v:k)h^=size_t(v)+0x9e3779b9+(h<<6)+(h>>2);return h;}};
struct CollisionHash {size_t operator()(const Key&)const{return 0;}};
using Batch=NativeBatchPayloadCache<Key,Payload,Hash>;
static auto Bytes(size_t size,uint8_t value) {
  auto p=std::make_shared<Payload>();p->value.assign(size,value);return p;
}
static void Ownership() {
  Batch cache;std::vector<std::shared_ptr<const Payload>> submitted;
  for(size_t frame=0;frame<64;++frame) {
    cache.Reset();assert(cache.size()==0&&cache.bytes()==0);
    for(size_t i=0;i<2048;++i) {
      Key key{3,i,frame%2};auto bytes=Bytes(16,uint8_t(frame));
      assert(!cache.Find(key));assert(cache.Store(key,bytes));assert(cache.Find(key)==bytes);
      if(i==0)submitted.push_back(bytes);
    }
    assert(cache.bytes()==2048*16&&cache.size()==2048);
    for(size_t i=0;i<submitted.size();++i)assert(submitted[i]->value[0]==uint8_t(i));
  }
  auto moved=std::move(cache);assert(moved.Find({3,0,1})->value[0]==63);
  std::weak_ptr<const Payload> released=moved.Find({3,1,1});moved.Reset();assert(released.expired());
  for(size_t i=0;i<submitted.size();++i)assert(submitted[i]->value[0]==uint8_t(i));
  NativeBatchPayloadCache<Key,Payload,CollisionHash> collisions;
  for(size_t i=0;i<512;++i)assert(collisions.Store({7,i},Bytes(16,uint8_t(i))));
  for(size_t i=0;i<512;++i)assert(collisions.Find({7,i})->value[0]==uint8_t(i));
  auto prior=collisions.Find({7,5});assert(collisions.Store({7,5},Bytes(32,99)));
  assert(prior->value[0]==5&&collisions.Find({7,5})->value[0]==99&&collisions.bytes()==512*16+16);
  assert(!collisions.Store({1},{}));collisions.Reset();assert(!collisions.Find({7,5}));
  std::cout<<"PASS: 131072 immutable identities, batch resets, owner release, submitted payload lifetime, address reuse, replacement, collisions and moves\n";
}
struct Entry {std::weak_ptr<const int> resource;std::shared_ptr<const Payload> owner;uint64_t used;};
struct Size {size_t operator()(const Entry& e)const{return e.owner->value.size();}};
using Legacy=NativeIncrementalOwnerCache<Key,Entry,Size,Hash>;
static void Benchmark(const char* output) {
  constexpr size_t frames=120,draws=2048,geometry=2048,budget=24*1024*1024;
  auto source=std::make_shared<const int>(1);std::array<double,5> legacy{},batch{};
  size_t old_evictions=0,new_geometry_retained=0;uint64_t checksum=0;
  for(size_t run=0;run<5;++run) {
    const auto measure=[&](bool split) {
      Legacy persistent;Batch packets;size_t evictions=0;
      for(size_t i=0;i<geometry;++i)persistent.Store({1,i},{source,Bytes(4096,7),0});
      auto start=std::chrono::steady_clock::now();
      for(size_t f=0;f<frames;++f) {
        packets.Reset();
        persistent.Sweep(512,[](const auto& e){return e.second.resource.expired();});
        while(persistent.bytes()>budget){persistent.EvictOldest();++evictions;}
        // Shared policy has a permanent source owner, but its camera/content
        // identity changes per frame. All full-bank byte sizes stay unchanged.
        for(size_t i=0;i<draws;++i) {
          Key key{4,f*draws+i};auto payload=Bytes(1056,uint8_t(f));
          if(split){assert(packets.Store(key,payload));checksum+=packets.Find(key)->value[0];}
          else {persistent.Store(key,{source,payload,f});checksum+=persistent.find(key)->second.owner->value[0];}
        }
      }
      const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/frames;
      size_t kept=0;for(size_t i=0;i<geometry;++i)kept+=persistent.find({1,i})!=persistent.end();
      if(split){new_geometry_retained=kept;assert(persistent.bytes()==geometry*4096);assert(packets.bytes()==draws*1056);}
      else old_evictions=evictions;
      return ms;
    };
    legacy[run]=measure(false);batch[run]=measure(true);
  }
  std::sort(legacy.begin(),legacy.end());std::sort(batch.begin(),batch.end());
  assert(checksum==uint64_t(5)*2*draws*(frames-1)*frames/2);
  assert(new_geometry_retained==geometry&&old_evictions>0);
  nlohmann::json j={{"scope","M1 Max CPU fixture; changing shared constants plus retained geometry; not M5 game FPS"},
    {"frames",frames},{"draws_per_frame",draws},{"legacy_ms",legacy[2]},{"batch_ms",batch[2]},
    {"legacy_evictions",old_evictions},{"geometry_retained",new_geometry_retained},{"batch_constant_bytes",draws*1056},
    {"persistent_geometry_bytes",geometry*4096},{"payload_bytes_unchanged",true}};
  std::ofstream(output)<<j.dump(2)<<'\n';std::cout<<j.dump(2)<<'\n';
}
int main(int argc,char** argv){Ownership();if(argc>1)Benchmark(argv[1]);}
