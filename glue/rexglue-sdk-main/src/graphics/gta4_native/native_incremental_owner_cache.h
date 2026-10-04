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
  // Map node addresses survive rehash. Direct links avoid allocations on
  // hits and whole-cache scans/sorts at the budget boundary. Admitted plans
  // pin their payloads independently of cache eviction.
  struct Node:Entry {
    explicit Node(Entry entry):Entry(std::move(entry)){}
    const Key* key=nullptr;
    Node* previous=nullptr;
    Node* next=nullptr;
  };
  using Map=std::unordered_map<Key,Node,Hash>;
 public:
  NativeIncrementalOwnerCache()=default;
  NativeIncrementalOwnerCache(const NativeIncrementalOwnerCache&)=delete;
  NativeIncrementalOwnerCache& operator=(const NativeIncrementalOwnerCache&)=delete;
  NativeIncrementalOwnerCache(NativeIncrementalOwnerCache&& other) noexcept
      :entries_(std::move(other.entries_)),bytes_(std::exchange(other.bytes_,0)),
       bucket_(std::exchange(other.bucket_,0)),oldest_(std::exchange(other.oldest_,nullptr)),
       newest_(std::exchange(other.newest_,nullptr)){}
  NativeIncrementalOwnerCache& operator=(NativeIncrementalOwnerCache&& other) noexcept {
    if(this!=&other){clear();entries_=std::move(other.entries_);bytes_=std::exchange(other.bytes_,0);
      bucket_=std::exchange(other.bucket_,0);oldest_=std::exchange(other.oldest_,nullptr);
      newest_=std::exchange(other.newest_,nullptr);}
    return *this;
  }
  auto begin(){return entries_.begin();}
  auto end(){return entries_.end();}
  auto begin() const{return entries_.begin();}
  auto end() const{return entries_.end();}
  auto find(const Key& key){return entries_.find(key);}
  auto find(const Key& key) const{return entries_.find(key);}
  size_t size() const{return entries_.size();}
  size_t bytes() const{return bytes_;}
  void Touch(typename Map::iterator entry) {
    if(entry==entries_.end()||&entry->second==newest_)return;
    Unlink(entry->second);Append(entry->second);
  }
  const Entry* Oldest() const{return oldest_;}
  bool EvictOldest(){return oldest_&&erase(*oldest_->key);}
  void Store(const Key& key,Entry entry) {
    auto found=entries_.find(key);
    const size_t old=found!=entries_.end()?ByteSize{}(found->second):0;
    const size_t added=ByteSize{}(entry);
    if(found==entries_.end()) {
      auto inserted=entries_.try_emplace(key,std::move(entry)).first;
      inserted->second.key=&inserted->first;Append(inserted->second);
    }else {static_cast<Entry&>(found->second)=std::move(entry);Touch(found);}
    bytes_=bytes_-old+added;
  }
  size_t erase(const Key& key) {
    auto found=entries_.find(key);if(found==entries_.end())return 0;
    bytes_-=ByteSize{}(found->second);Unlink(found->second);entries_.erase(found);return 1;
  }
  template<class Predicate> size_t EraseIf(Predicate&& predicate) {
    size_t removed=0;
    for(auto it=entries_.begin();it!=entries_.end();) {
      if(predicate(*it)){bytes_-=ByteSize{}(it->second);Unlink(it->second);it=entries_.erase(it);++removed;}
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
  void clear(){entries_.clear();bytes_=0;bucket_=0;oldest_=newest_=nullptr;}
 private:
  void Unlink(Node& entry) {
    if(entry.previous)entry.previous->next=entry.next;else oldest_=entry.next;
    if(entry.next)entry.next->previous=entry.previous;else newest_=entry.previous;
    entry.previous=entry.next=nullptr;
  }
  void Append(Node& entry) {
    entry.previous=newest_;entry.next=nullptr;
    if(newest_)newest_->next=&entry;else oldest_=&entry;
    newest_=&entry;
  }
  Map entries_;
  size_t bytes_=0,bucket_=0;
  Node* oldest_=nullptr;
  Node* newest_=nullptr;
};
} // namespace rex::graphics::gta4_native
