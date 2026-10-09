// Extract the production pool. Slow VM doubles make mutex scope deterministic;
// actual GPU-owned region lifetimes are covered by the separate Metal fixtures.
#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
using vm_address_t=uintptr_t;
constexpr int VM_FLAGS_ANYWHERE=1,KERN_SUCCESS=0;
static std::mutex vm_mutex;static std::condition_variable vm_cv;
static bool block_allocate=false,block_deallocate=false,entered=false,proceed=false;
static std::atomic<uintptr_t> next_address{1};
static void Barrier(bool allocate) {
  std::unique_lock lock(vm_mutex);
  if(allocate?block_allocate:block_deallocate){entered=true;vm_cv.notify_all();vm_cv.wait(lock,[]{return proceed;});}
}
static int mach_task_self(){return 0;}
static int vm_allocate(int,vm_address_t* address,size_t,int){Barrier(true);*address=next_address++;return KERN_SUCCESS;}
static int vm_deallocate(int,vm_address_t,size_t){Barrier(false);return KERN_SUCCESS;}
static bool theft4_retail_mode(){return false;}
struct ResourceCacheStats{uint64_t page_memory_allocations=0,page_memory_reuses=0;size_t free_page_bytes=0;};
#include "upload-pool-body.inc"
static void Enter(bool allocate){std::lock_guard lock(vm_mutex);block_allocate=allocate;block_deallocate=!allocate;entered=proceed=false;}
static void Await(){std::unique_lock lock(vm_mutex);assert(vm_cv.wait_for(lock,std::chrono::seconds(5),[]{return entered;}));}
static void Continue(){std::lock_guard lock(vm_mutex);proceed=true;block_allocate=block_deallocate=false;vm_cv.notify_all();}
int main(){
  UploadPagePool pool;
  Enter(true);auto allocating=std::async(std::launch::async,[&]{return pool.Acquire();});Await();
  // Retirement of an unrelated region must finish while allocation is blocked.
  auto release=std::async(std::launch::async,[&]{pool.Release(999,pool.capacity);});
  const bool release_ready=release.wait_for(std::chrono::seconds(2))==std::future_status::ready;
  Continue();release.get();const auto allocation=allocating.get();assert(release_ready&&allocation);
  assert(pool.Acquire()==999);pool.Release(allocation,pool.capacity);
  // Overflow frees a region without holding up a hit in the reusable pool.
  Enter(false);auto freeing=std::async(std::launch::async,[&]{pool.Release(555,pool.limit+1);});Await();
  auto hit=std::async(std::launch::async,[&]{return pool.Acquire();});
  const bool hit_ready=hit.wait_for(std::chrono::seconds(2))==std::future_status::ready;
  Continue();assert(hit.get()==allocation);freeing.get();assert(hit_ready);
  // Parallel acquire/release accounting stays bounded and live identities unique.
  std::mutex live_mutex;std::vector<vm_address_t> live;
  std::vector<std::thread> threads;
  for(size_t t=0;t<8;++t)threads.emplace_back([&]{for(size_t i=0;i<1000;++i){
    const auto a=pool.Acquire();assert(a);
    {std::lock_guard lock(live_mutex);assert(std::find(live.begin(),live.end(),a)==live.end());live.push_back(a);}
    std::this_thread::yield();
    {std::lock_guard lock(live_mutex);std::erase(live,a);}pool.Release(a,pool.capacity);
  }});
  for(auto& t:threads)t.join();ResourceCacheStats stats;pool.Stats(stats);
  assert(live.empty()&&stats.free_page_bytes<=pool.limit&&stats.page_memory_reuses>0);
}
