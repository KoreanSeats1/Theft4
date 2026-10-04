#pragma once
#include <algorithm>
#include <cstddef>
#include <new>
#include <utility>
#include <vector>

namespace theft4 {
// Worker-owned CPU storage only. Clear releases every resource reference before
// caching; the submitted Metal command buffer retains its own GPU dependencies.
// Exhaustion falls back to ordinary vectors, never waits or reuses live packets.
template<class A,class B> class VectorStoragePool {
 public:
  struct Entry {std::vector<A> first;std::vector<B> second;};
  explicit VectorStoragePool(size_t bytes=8*1024*1024,size_t entries=8192)
      :maximum_bytes_(bytes),maximum_entries_(entries){}
  Entry Acquire() {
    if(entries_.empty()){++misses_;return {};}
    auto out=std::move(entries_.back());entries_.pop_back();
    bytes_-=Bytes(out);++hits_;return out;
  }
  void Recycle(std::vector<A>& first,std::vector<B>& second) noexcept {
    first.clear();second.clear();
    // Check before multiplying, including unusually large public test packets.
    if(first.capacity()>maximum_bytes_/sizeof(A)||second.capacity()>maximum_bytes_/sizeof(B))return;
    const size_t size=first.capacity()*sizeof(A)+second.capacity()*sizeof(B);
    if(!size||size>maximum_bytes_-bytes_||entries_.size()>=maximum_entries_)return;
    try {
      // Grow the slot array before transferring ownership so allocation failure
      // leaves the caller's vectors valid and rendering unaffected.
      if(entries_.size()==entries_.capacity())entries_.reserve(
          std::min(maximum_entries_,std::max(size_t(32),entries_.capacity()*2)));
      entries_.push_back({std::move(first),std::move(second)});bytes_+=size;
    }catch(const std::bad_alloc&){}
  }
  size_t Bytes()const{return bytes_;}
  size_t Entries()const{return entries_.size();}
  size_t Hits()const{return hits_;}
  size_t Misses()const{return misses_;}
 private:
  static size_t Bytes(const Entry& e){return e.first.capacity()*sizeof(A)+e.second.capacity()*sizeof(B);}
  std::vector<Entry> entries_;
  size_t maximum_bytes_,maximum_entries_,bytes_=0,hits_=0,misses_=0;
};
// Stack-only cleanup, including early failed admission. No std::function heap.
template<class F> struct StorageCleanup {
  F function;
  ~StorageCleanup(){function();}
};
}
