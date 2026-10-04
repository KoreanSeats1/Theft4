#include "native_transactional_map.h"
#include "native_shared_frame_arena.h"
#include "theft4_frame_plan.h"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <string_view>

using namespace rex::graphics::gta4_native;
struct Record {
  uint64_t serial=0;
  std::shared_ptr<const int> owner;
  std::vector<uint64_t> data;
  bool operator==(const Record&)const=default;
};
struct ThrowingRecord {
  static inline bool fail=false;
  int value=0;
  ThrowingRecord()=default;
  ThrowingRecord(const ThrowingRecord& o):value(o.value){if(fail)throw std::runtime_error("copy rejected");}
};
static void TransactionChecks(){
  NativeTransactionalMap<uint64_t,Record> registry;
  std::map<uint64_t,Record> expected;
  std::mt19937 rng(12345);
  for(uint64_t i=0;i<1000;++i)registry[i]=expected[i]={i,std::make_shared<const int>(i),{i,i+1}};
  for(unsigned batch=0;batch<2000;++batch){
    auto working=expected;registry.Begin();
    for(unsigned n=0;n<30;++n){
      auto key=rng()%1200;
      switch(rng()%5){
        case 0:registry[key]=working[key]={rng(),std::make_shared<const int>(rng()),{key,42}};break;
        case 1:registry.erase(key);working.erase(key);break;
        case 2:if(auto it=registry.find(key);it!=registry.end()){++it->second.serial;++working.at(key).serial;}break;
        case 3:if(registry.contains(key)){registry.at(key).data.push_back(n);working.at(key).data.push_back(n);}break;
        default:assert(registry.contains(key)==working.contains(key));break;
      }
    }
    if(batch%11==0){registry.EraseIf([](const auto& e){return e.first%13==0;});std::erase_if(working,[](const auto& e){return e.first%13==0;});}
    if(batch%37==0){registry.clear();working.clear();}
    if(batch%2){registry.Commit();expected=std::move(working);}else registry.Rollback();
    assert(registry.size()==expected.size());
    auto it=expected.begin();for(const auto& e:registry){assert(e.first==it->first&&e.second==it->second);++it;}
  }
  NativeTransactionalMap<int,ThrowingRecord> throwing;throwing[1].value=77;throwing.Begin();
  ThrowingRecord::fail=true;try{throwing.at(1);assert(false);}catch(const std::runtime_error&){}
  ThrowingRecord::fail=false;throwing.Rollback();assert(throwing.at(1).value==77);
  registry.Begin();for(const auto& entry:registry)assert(registry.Read(entry.first)==entry.second);assert(registry.CopiedEntries()==0);registry.Rollback();
  registry.Begin();registry[9999]={123,std::make_shared<const int>(5),{6}};
  auto moved=std::move(registry);moved.Rollback();assert(!moved.contains(9999));
}
static void ArenaChecks(){
  namespace r=theft4::render;
  const auto reset=[](r::Capture& c){auto attributes=std::move(c.draw.pipeline.attributes);c={};attributes.clear();c.draw.pipeline.attributes=std::move(attributes);};
  NativeSharedFrameArena<r::Capture,4> arena(12*sizeof(r::Capture));
  arena.BeginBatch(reset);
  auto pinned=arena.Acquire();pinned->frame=100;pinned->draw.pipeline.attributes.resize(40);
  auto bytes=std::make_shared<r::Bytes>();bytes->value={1,2,3};pinned->draw.constants[0]={bytes,0,3};
  std::weak_ptr<const r::Bytes> weak=bytes;bytes.reset();
  auto gpu_plan=std::make_shared<r::FramePlan>();r::Pass pass;pass.commands.push_back(r::FrameDraw{pinned});gpu_plan->commands.push_back(std::move(pass));
  auto old=pinned.get();pinned.reset();
  for(unsigned batch=0;batch<100;++batch){
    arena.BeginBatch(reset);std::vector<std::shared_ptr<r::Capture>> captures;
    for(unsigned n=0;n<12;++n){auto c=arena.Acquire();assert(c.get()!=old);c->frame=batch;captures.push_back(std::move(c));}
    const auto& retained=std::get<r::FrameDraw>(std::get<r::Pass>(gpu_plan->commands[0]).commands[0]).capture;
    assert(retained->frame==100&&retained->draw.constants[0].source->value==std::vector<uint8_t>({1,2,3}));
  }
  assert(arena.fallback_objects>0&&!weak.expired());gpu_plan.reset();arena.BeginBatch(reset);assert(weak.expired());
  auto reused=arena.Acquire();assert(reused.get()==old&&reused->draw.pipeline.attributes.capacity()>=40&&reused->draw.pipeline.attributes.empty());
  std::shared_ptr<r::Capture> survivor;
  {NativeSharedFrameArena<r::Capture> temporary;temporary.BeginBatch(reset);survivor=temporary.Acquire();survivor->frame=999;}
  assert(survivor->frame==999);
  assert(arena.AllocatedBytes()<=12*sizeof(r::Capture));
}
static void Benchmark(){
  using Clock=std::chrono::steady_clock;
  std::map<uint64_t,Record> original;NativeTransactionalMap<uint64_t,Record> journal;
  for(uint64_t i=0;i<25000;++i)original[i]=journal[i]={i,std::make_shared<const int>(i),{i,i+1,i+2,i+3}};
  uint64_t checksum=0;auto start=Clock::now();
  for(unsigned b=0;b<100;++b){auto copy=original;for(uint64_t n=0;n<100;++n)++copy.at(n*17).serial;checksum+=copy.at(0).serial;}
  auto full=std::chrono::duration<double,std::milli>(Clock::now()-start).count()/100;
  start=Clock::now();for(unsigned b=0;b<100;++b){journal.Begin();for(uint64_t n=0;n<100;++n)++journal.at(n*17).serial;checksum+=journal.at(0).serial;journal.Rollback();}
  auto incremental=std::chrono::duration<double,std::milli>(Clock::now()-start).count()/100;
  std::cout<<"registry_entries=25000 touched=100 full_copy_ms="<<full<<" journal_ms="<<incremental<<" copied="<<journal.CopiedEntries()<<" checksum="<<checksum<<"\n";
  namespace r=theft4::render;
  NativeSharedFrameArena<r::Capture> arena;
  const auto reset=[](r::Capture& c){auto attributes=std::move(c.draw.pipeline.attributes);c={};attributes.clear();c.draw.pipeline.attributes=std::move(attributes);};
  auto draw_trial=[&](bool pooled){
    auto began=Clock::now();if(pooled)arena.BeginBatch(reset);
    std::vector<std::shared_ptr<r::Capture>> owners;owners.reserve(4000);
    for(unsigned n=0;n<4000;++n){auto c=pooled?arena.Acquire():std::make_shared<r::Capture>();c->frame=n;c->draw.pipeline.attributes.resize(24);checksum+=c->frame;owners.push_back(std::move(c));}
    owners.clear();return std::chrono::duration<double,std::milli>(Clock::now()-began).count();
  };
  draw_trial(true);draw_trial(true);
  std::vector<double> ordinary,pooled;
  for(unsigned trial=0;trial<41;++trial){ordinary.push_back(draw_trial(false));pooled.push_back(draw_trial(true));}
  std::sort(ordinary.begin(),ordinary.end());std::sort(pooled.begin(),pooled.end());
  std::cout<<"draws=4000 capture_bytes="<<sizeof(r::Capture)<<" allocation_ms="<<ordinary[20]<<" arena_ms="<<pooled[20]<<" pages="<<arena.page_allocations<<" fallbacks="<<arena.fallback_objects<<" checksum="<<checksum<<"\n";

}
int main(int argc,char** argv){TransactionChecks();ArenaChecks();if(argc>1&&std::string_view(argv[1])=="--benchmark")Benchmark();std::cout<<"frontend storage differential and lifetime checks passed\n";}
