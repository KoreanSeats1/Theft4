#pragma once
#include "frame_constant_arena.h"
#include <memory>
#include <vector>

namespace rex::graphics::gta4_native {
// The command batch keeps immutable source identities alive. Only its lookup
// index is generation-stamped; owners live in a dense, reusable vector and are
// released at the next batch boundary. Accepted plans and projection families
// retain their own payload owners, independently of this index.
template<class Key,class Payload,class Hash=std::hash<Key>>
class NativeBatchPayloadCache {
 public:
  std::shared_ptr<const Payload> Find(const Key& key) const {
    const auto* index=index_.Find(key);
    return index?owners_[*index]:std::shared_ptr<const Payload>{};
  }
  bool Store(const Key& key,std::shared_ptr<const Payload> payload) {
    if(!payload)return false;
    if(auto* index=index_.Find(key)) {
      bytes_-=owners_[*index]->value.size();bytes_+=payload->value.size();
      owners_[*index]=std::move(payload);return true;
    }
    const auto slot=owners_.size();owners_.push_back(std::move(payload));
    const auto inserted=index_.Insert(key,slot);
    if(!inserted){owners_.pop_back();return false;}
    bytes_+=owners_.back()->value.size();return true;
  }
  void Reset() {
    owners_.clear();bytes_=0;
    if(!index_.ResetGeneration())index_={};
  }
  size_t size() const{return owners_.size();}
  size_t bytes() const{return bytes_;}
 private:
  FrameGenerationMap<Key,size_t,Hash> index_;
  std::vector<std::shared_ptr<const Payload>> owners_;
  size_t bytes_=0;
};
}
