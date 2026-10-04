#pragma once
#include <algorithm>
#include <cstddef>
namespace theft4 {
// No iterator survives an insertion/rehash. Erasing one node preserves the
// next local iterator. All resource lookups still verify exact live owners.
template<class Map,class Predicate>
size_t SweepCacheBuckets(Map& cache,size_t& cursor,size_t budget,Predicate retired) {
  const auto buckets=cache.bucket_count();size_t removed=0;
  for(size_t n=0;n<std::min(budget,buckets);++n) {
    cursor%=buckets;
    for(auto it=cache.begin(cursor);it!=cache.end(cursor);) {
      const auto& entry=*it++;
      if(retired(entry)){const auto key=entry.first;removed+=cache.erase(key);}
    }
    ++cursor;
  }
  return removed;
}
}
