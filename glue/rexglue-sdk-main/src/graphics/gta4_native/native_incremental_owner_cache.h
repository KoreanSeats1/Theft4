#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

namespace rex::graphics::gta4_native {
// Worker-owned immutable resource cache. Byte accounting is updated on each
// mutation, so frame preparation never recounts every retained payload. Sweep
// visits a bounded number of buckets; no iterator survives a possible rehash.
// Every actual cache hit still checks the resource's owner/generation itself.
template<class Key,class Entry,class ByteSize,class Hash=std::hash<Key>>
class NativeIncrementalOwnerCache {
 public:
  auto begin(){return entries_.begin();}
  auto end(){return entries_.end();}
  auto begin() const{return entries_.begin();}
  auto end() const{return entries_.end();}
  auto find(const Key& key){return entries_.find(key);}
  auto find(const Key& key) const{return entries_.find(key);}
  size_t size() const{return entries_.size();}
  size_t bytes() const{return bytes_;}
  void Store(const Key& key,Entry entry) {
    auto found=entries_.find(key);
    const size_t old=found!=entries_.end()?ByteSize{}(found->second):0;
    const size_t added=ByteSize{}(entry);
    entries_.insert_or_assign(key,std::move(entry));bytes_=bytes_-old+added;
  }
  size_t erase(const Key& key) {
    auto found=entries_.find(key);if(found==entries_.end())return 0;
    bytes_-=ByteSize{}(found->second);entries_.erase(found);return 1;
  }
  template<class Predicate> size_t EraseIf(Predicate&& predicate) {
    size_t removed=0;
    for(auto it=entries_.begin();it!=entries_.end();) {
      if(predicate(*it)){bytes_-=ByteSize{}(it->second);it=entries_.erase(it);++removed;}
      else ++it;
    }
    return removed;
  }
  template<class Predicate> size_t Sweep(size_t maximum_buckets,Predicate&& retired) {
    size_t removed=0;
    const size_t buckets=entries_.bucket_count();
    for(size_t n=0;n<std::min(maximum_buckets,buckets);++n) {
      bucket_%=buckets;
      for(auto it=entries_.begin(bucket_);it!=entries_.end(bucket_);) {
        const auto& entry=*it++;
        if(retired(entry)){const auto key=entry.first;removed+=erase(key);}
      }
      ++bucket_;
    }
    return removed;
  }
  void clear(){entries_.clear();bytes_=0;bucket_=0;}
 private:
  std::unordered_map<Key,Entry,Hash> entries_;
  size_t bytes_=0,bucket_=0;
};
} // namespace rex::graphics::gta4_native
