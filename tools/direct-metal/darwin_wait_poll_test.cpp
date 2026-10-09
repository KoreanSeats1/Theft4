#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <ctime>

#define REX_PLATFORM_LINUX 0
#define assert_true(value) assert(value)
namespace live {
enum class WaitResult { kSuccess, kTimeout, kFailed };
struct Event {};
struct Semaphore {};
bool RuntimeWaitFixesEnabled() { return false; }
#include "darwin-wait-production.inc"
}
#ifdef WAIT_BASELINE
namespace baseline {
enum class WaitResult { kSuccess, kTimeout, kFailed };
struct Event {};
struct Semaphore {};
bool RuntimeWaitFixesEnabled() { return false; }
#include WAIT_BASELINE
}
#endif

using namespace std::chrono_literals;
using W=live::WaitResult;
using E=live::PosixCondition<live::Event>;
using S=live::PosixCondition<live::Semaphore>;
using B=live::PosixConditionBase;
static auto Wait(std::initializer_list<B*> objects,bool all,std::chrono::milliseconds timeout) {
  return B::WaitMultiple(std::vector<B*>(objects),all,timeout);
}
// The mutex is the actual production object's protected mutex. Simulate
// another thread holding it past the caller's finite timeout.
class Contended : public E {
 public:
  Contended():E(false,false){}
  void Hold(std::atomic<bool>& ready) {
    std::lock_guard lock(mutex_);ready=true;
    std::this_thread::sleep_for(60ms);
  }
};
static void Contracts() {
  E a(false,true),b(false,true);
  auto any=Wait({&b,&a},false,0ms);
  assert(any.first==W::kSuccess&&any.second==0);
  assert(b.Wait(0ms)==W::kTimeout&&a.Wait(0ms)==W::kSuccess);
  S s(2,2),t(0,2);
  assert(Wait({&s,&t},true,0ms).first==W::kTimeout);
  assert(s.Wait(0ms)==W::kSuccess); // Failed wait-all did not consume s.
  assert(t.Signal());
  assert(Wait({&s,&t},true,0ms).first==W::kSuccess);
  assert(s.Wait(0ms)==W::kTimeout&&t.Wait(0ms)==W::kTimeout);
  E manual(true,true),off(false,false);
  assert(Wait({&manual,&off},false,0ms).first==W::kSuccess);
  assert(manual.Wait(0ms)==W::kSuccess);manual.Reset();
  assert(Wait({&manual,&off},false,0ms).first==W::kTimeout);
  assert(Wait({&off},false,0ms).first==W::kTimeout);
  const auto start=std::chrono::steady_clock::now();
  assert(Wait({&manual,&off},false,5ms).first==W::kTimeout);
  assert(std::chrono::steady_clock::now()-start>=5ms);
  std::thread signal([&]{std::this_thread::sleep_for(3ms);off.Signal();});
  assert(Wait({&manual,&off},false,200ms).first==W::kSuccess);signal.join();
  std::thread both([&]{std::this_thread::sleep_for(3ms);manual.Signal();off.Signal();});
  assert(Wait({&manual,&off},true,200ms).first==W::kSuccess);both.join();
  assert(manual.Wait(0ms)==W::kSuccess&&off.Wait(0ms)==W::kTimeout);
  manual.Reset();
  std::thread infinite([&]{std::this_thread::sleep_for(3ms);off.Signal();});
  assert(Wait({&manual,&off},false,std::chrono::milliseconds::max()).first==W::kSuccess);infinite.join();
  Contended blocked;std::atomic<bool> ready=false;
  std::thread holder([&]{blocked.Hold(ready);});
  while(!ready)std::this_thread::yield();
  const auto before=std::chrono::steady_clock::now();
  assert(Wait({&blocked,&manual},false,2ms).first==W::kTimeout);
  const auto elapsed=std::chrono::steady_clock::now()-before;
  holder.join();assert(elapsed<40ms); // Must finish before the lock releases.
}
static double CpuMs() {
  timespec time{};assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&time)==0);
  return time.tv_sec*1000.0+time.tv_nsec/1e6;
}
template<class Base,class Result> class Probe : public Base {
 public:
  mutable size_t checks=0;
  bool Signal()override{return false;}
 private:
  bool signaled()const override{++checks;return false;}
  void post_execution()override{assert(false);}
};
template<class Base,class Result> static void Timed(const char* name) {
  Probe<Base,Result> a,b;
  const auto cpu=CpuMs();const auto start=std::chrono::steady_clock::now();
  for(int i=0;i<200;++i) {
    assert(Base::WaitMultiple(std::vector<Base*>{&a,&b},false,4ms).first==Result::kTimeout);
  }
  const auto wall=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  std::printf("%s: waits=200 timeout_ms=4 state_checks=%zu cpu_ms=%.3f wall_ms=%.3f\n",name,a.checks+b.checks,CpuMs()-cpu,wall);
}
int main() {
  Contracts();
#ifdef WAIT_BASELINE
  Timed<baseline::PosixConditionBase,baseline::WaitResult>("140 baseline");
#endif
  Timed<B,W>("141 candidate");
  std::puts("Production wait-any/all, signal consumption, manual reset, finite/infinite wake and contended deadline passed");
}
