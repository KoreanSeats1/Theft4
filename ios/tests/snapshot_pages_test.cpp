#include "native_snapshot_pages.h"
#include <array>
#include <chrono>
#include <iostream>
#include <memory_resource>
#include <stdexcept>
#include <thread>
#include <vector>
#include <algorithm>
using namespace rex::graphics::gta4_native;
static void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
struct alignas(128) Tracked {
  static inline std::atomic<size_t> created{0},destroyed{0};
  explicit Tracked(size_t value):value(value){if(value==SIZE_MAX)throw std::runtime_error("intentional construction failure");++created;}
  ~Tracked(){++destroyed;}
  size_t value;
};
static void Lifetime() {
  std::shared_ptr<Tracked> tail;
  {
    NativeSnapshotPages<Tracked,7> pages;
    std::vector<std::shared_ptr<Tracked>> values;
    for(size_t i=0;i<2000;++i){auto v=pages.Create(i);Check(uintptr_t(v.get())%128==0,"alignment");values.push_back(std::move(v));}
    Check(pages.pages_created()==286&&pages.objects_created()==2000,"page counts");
    try{pages.Create(SIZE_MAX);throw std::runtime_error("constructor did not fail");}catch(const std::runtime_error& e){Check(std::string(e.what())=="intentional construction failure","exception state");}
    auto after=pages.Create(2000);Check(after->value==2000&&pages.objects_created()==2001,"exception recovery");
    tail=values.back();pages.ReleaseActive();
    std::thread cleanup([owned=std::move(values)]()mutable{for(auto& value:owned){Check(value!=nullptr,"published ownership");value.reset();}});cleanup.join();
    Check(tail->value==1999&&pages.live_bytes()>0,"tail ownership");
    after.reset();
  }
  Check(tail->value==1999&&Tracked::destroyed<Tracked::created,"pool destroyed tail");
  tail.reset();Check(Tracked::created==Tracked::destroyed,"exact destruction");
}
template<size_t Bytes> struct Snapshot { std::array<uint8_t,Bytes> state{}; std::shared_ptr<const uint64_t> owner; };
template<class F>static double Median(F&& f){std::array<double,11>times{};for(auto& time:times){auto start=std::chrono::steady_clock::now();f();time=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}std::sort(times.begin(),times.end());return times[5];}
template<size_t Bytes>static void Benchmark(){
  using T=Snapshot<Bytes>;constexpr size_t count=5000;
  T source;source.owner=std::make_shared<const uint64_t>(95);source.state[0]=95;
  std::pmr::synchronized_pool_resource pool;
  NativeSnapshotPages<T,64> pages;
  std::vector<std::shared_ptr<T>> owners;owners.reserve(count);
  uint64_t checksum=0;
  double baseline=Median([&]{for(size_t i=0;i<count;++i)owners.push_back(std::allocate_shared<T>(std::pmr::polymorphic_allocator<T>(&pool),source));for(auto& p:owners)checksum+=p->state[0]+*p->owner;owners.clear();});
  double paged=Median([&]{for(size_t i=0;i<count;++i)owners.push_back(pages.Create(source));pages.ReleaseActive();for(auto& p:owners)checksum+=p->state[0]+*p->owner;owners.clear();Check(pages.live_bytes()==0,"benchmark page leaked");});
  std::cout<<"SNAPSHOT bytes="<<sizeof(T)<<" objects=5000 synchronized_ms="<<baseline<<" paged_ms="<<paged<<" page_allocations_per_run=79 checksum="<<checksum<<'\n';
}
int main(int argc,char**){Lifetime();std::cout<<"PASS: 2001 aligned snapshots, partial pages, constructor exception, cross-thread retirement, tail outliving builder, exact destruction\n";if(argc>1){Benchmark<256>();Benchmark<1024>();Benchmark<2048>();}}
