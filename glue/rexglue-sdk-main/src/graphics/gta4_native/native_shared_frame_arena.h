#pragma once
#include <array>
#include <memory>
#include <vector>
#include <cstddef>

namespace rex::graphics::gta4_native {
// Aliasing shared owners pin an entire page. A page can be reset only when the
// arena is its sole owner, including after GPU submission. Overflow uses normal
// shared allocation rather than blocking or mutating an in-flight page.
template<class T,size_t PageSize=64>
class NativeSharedFrameArena {
  struct Page {std::array<T,PageSize> values{};size_t used=0;};
  std::vector<std::shared_ptr<Page>> pages_;
  std::vector<size_t> available_;
  size_t cursor_=0,maximum_pages_;
 public:
  size_t page_allocations=0,pooled_objects=0,fallback_objects=0,recycled_objects=0;
  explicit NativeSharedFrameArena(size_t byte_budget=32*1024*1024):maximum_pages_(byte_budget/sizeof(Page)){}
  template<class Reset> void BeginBatch(Reset&& reset){
    available_.clear();cursor_=0;
    for(size_t i=0;i<pages_.size();++i)if(pages_[i].use_count()==1){
      auto& page=*pages_[i];for(size_t n=0;n<page.used;++n)reset(page.values[n]);
      recycled_objects+=page.used;page.used=0;available_.push_back(i);
    }
  }
  std::shared_ptr<T> Acquire(){
    while(cursor_<available_.size()&&pages_[available_[cursor_]]->used==PageSize)++cursor_;
    if(cursor_==available_.size()){
      if(pages_.size()>=maximum_pages_){++fallback_objects;return std::make_shared<T>();}
      auto page=std::make_shared<Page>();pages_.push_back(std::move(page));available_.push_back(pages_.size()-1);++page_allocations;
    }
    auto& page=pages_[available_[cursor_]];auto* value=&page->values[page->used++];++pooled_objects;
    return std::shared_ptr<T>(page,value);
  }
  size_t AllocatedBytes()const{return pages_.size()*sizeof(Page);}
};
}
