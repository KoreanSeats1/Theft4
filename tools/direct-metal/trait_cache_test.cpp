#include "theft4_trait_cache.h"
#include <cassert>
#include <memory>
#include <iostream>
#include <vector>
struct Value {std::shared_ptr<const int> owner;int trait=0;};
int main() {
  theft4::TraitCache<Value> cache;
  std::vector<const void*> keys;
  for(uintptr_t p=0x10000;keys.size()<5;p+=0x400)
    if(theft4::TraitCache<Value>::SetFor(reinterpret_cast<void*>(p))==0)keys.push_back(reinterpret_cast<void*>(p));
  bool hit=false;std::vector<std::weak_ptr<const int>> weak;
  for(size_t i=0;i<4;++i) {
    auto owner=std::make_shared<const int>(int(i));weak.push_back(owner);
    auto& v=cache.Lookup(keys[i],hit);assert(!hit);v={owner,int(i)};
  }
  for(size_t iteration=0;iteration<10000;++iteration) {
    const auto i=iteration%4;const auto& v=cache.Lookup(keys[i],hit);
    assert(hit&&v.owner&&v.trait==int(i)&&*v.owner==int(i));
  }
  auto& replacement=cache.Lookup(keys[4],hit);assert(!hit);assert(!weak[0].expired());
  replacement={std::make_shared<const int>(4),4};assert(weak[0].expired());
  for(size_t i=1;i<5;++i){const auto& v=cache.Lookup(keys[i],hit);assert(hit&&v.trait==int(i));}
  auto& removed=cache.Lookup(keys[0],hit);assert(!hit);
  removed={std::make_shared<const int>(0),0};
  assert(weak[1].expired());
  cache={};for(const auto& owner:weak)assert(owner.expired());
  std::cout<<"PASS: aligned pointer collisions, four-probe reuse, exact identity, ownership retirement and bounded replacement\n";
}
