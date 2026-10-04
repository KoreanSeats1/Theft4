#include "native_exclusive_generation_retirement.h"
#include <cassert>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <iostream>
using namespace rex::graphics::gta4_native;
struct Resource {std::shared_ptr<const Resource> packed_source;};
struct Record {std::shared_ptr<const Resource> resource;std::shared_ptr<const int> surface;};
int main() {
  std::map<uint64_t,Record> records;std::set<uint64_t> pending;
  std::vector<std::shared_ptr<const int>> submitted_surfaces;
  auto captured=std::make_shared<const Resource>();auto queued=captured;
  records[1]={captured,std::make_shared<const int>(91)};pending={1,2};
  submitted_surfaces.push_back(records[1].surface);
  auto release=[](const Record& record){assert(record.surface);};
  std::set<uint64_t> protected_generations{2};
  auto protect=[&](uint64_t generation){return protected_generations.contains(generation);};
  assert(!RetireExclusiveNativeGenerations(records,pending,protect,release));assert(pending==std::set<uint64_t>({1,2}));
  records[2]={std::make_shared<const Resource>(),std::make_shared<const int>(92)};
  assert(!RetireExclusiveNativeGenerations(records,pending,protect,release));
  protected_generations.clear();assert(RetireExclusiveNativeGenerations(records,pending,protect,release)==1);
  captured.reset();assert(!RetireExclusiveNativeGenerations(records,pending,protect,release));
  queued.reset();assert(RetireExclusiveNativeGenerations(records,pending,protect,release)==1);
  assert(records.empty()&&pending.empty()&&*submitted_surfaces[0]==91);
  auto parent=std::make_shared<const Resource>();auto child=std::make_shared<Resource>();child->packed_source=parent;
  records[3]={parent,std::make_shared<const int>(93)};records[4]={child,std::make_shared<const int>(94)};
  parent.reset();child.reset();pending={3,4};
  // Child still owns the source; retirement can make it exclusive on the next sweep.
  assert(RetireExclusiveNativeGenerations(records,pending,protect,release)==1&&records.contains(3));
  assert(RetireExclusiveNativeGenerations(records,pending,protect,release)==1&&records.empty());
  for(uint64_t generation=5;generation<5005;++generation) {
    records[generation]={std::make_shared<const Resource>(),std::make_shared<const int>(int(generation))};
    submitted_surfaces.push_back(records[generation].surface);pending.insert(generation);
  }
  assert(RetireExclusiveNativeGenerations(records,pending,protect,release)==5000&&records.empty()&&pending.empty());
  assert(submitted_surfaces.size()==5001&&*submitted_surfaces.back()==5004);
  std::cout<<"PASS: active capture, queued owners, packed aliases, 5000 superseded generations and independent in-flight surfaces\n";
}
