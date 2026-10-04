#pragma once
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace rex::graphics::gta4_native {
// Worker-local speculative map. First mutable access preserves the original
// node and installs a copy; rejection restores original nodes without allocation.
// Read-only traversal does not duplicate the registry. No concurrent readers.
template<class Key,class Value,class Compare=std::less<Key>>
class NativeTransactionalMap {
  using Map=std::map<Key,Value,Compare>;
  Map values_, originals_;
  std::set<Key,Compare> inserted_;
  bool active_=false;
  size_t copied_=0;
  void Touch(const Key& key) {
    if(!active_||originals_.contains(key)||inserted_.contains(key))return;
    auto it=values_.find(key);
    if(it==values_.end()){inserted_.insert(key);return;}
    // Allocate/copy before extracting authoritative state: an allocation or
    // value-copy exception leaves the original map unchanged.
    Map replacement;replacement.emplace(it->first,it->second);
    originals_.insert(values_.extract(it));
    values_.insert(replacement.extract(replacement.begin()));++copied_;
  }
 public:
  using const_iterator=typename Map::const_iterator;
  NativeTransactionalMap()=default;
  NativeTransactionalMap(NativeTransactionalMap&&)=default;
  NativeTransactionalMap& operator=(NativeTransactionalMap&&)=default;
  NativeTransactionalMap(const NativeTransactionalMap&)=delete;
  NativeTransactionalMap& operator=(const NativeTransactionalMap&)=delete;
  void Begin(){if(active_)throw std::logic_error("nested registry transaction");active_=true;copied_=0;}
  void Commit(){originals_.clear();inserted_.clear();active_=false;}
  void Rollback(){
    for(const auto& key:inserted_)values_.erase(key);
    while(!originals_.empty()){
      auto node=originals_.extract(originals_.begin());values_.erase(node.key());values_.insert(std::move(node));
    }
    inserted_.clear();active_=false;
  }
  size_t CopiedEntries()const{return copied_;}
  size_t size()const{return values_.size();}
  bool empty()const{return values_.empty();}
  bool contains(const Key& key)const{return values_.contains(key);}
  const_iterator begin()const{return values_.begin();}
  const_iterator end()const{return values_.end();}
  auto find(const Key& key){Touch(key);return values_.find(key);}
  auto find(const Key& key)const{return values_.find(key);}
  Value& at(const Key& key){if(!values_.contains(key))throw std::out_of_range("registry key");Touch(key);return values_.at(key);}
  const Value& at(const Key& key)const{return values_.at(key);}
  const Value& Read(const Key& key)const{return values_.at(key);}
  Value& operator[](const Key& key){Touch(key);return values_[key];}
  size_t erase(const Key& key){Touch(key);return values_.erase(key);}
  void erase(const_iterator it){const Key key=it->first;erase(key);}
  template<class Predicate> void EraseIf(Predicate&& predicate){
    for(auto it=values_.cbegin();it!=values_.cend();){auto current=it++;if(predicate(*current))erase(current->first);}
  }
  void clear(){if(active_)EraseIf([](const auto&){return true;});else values_.clear();}
};
}
