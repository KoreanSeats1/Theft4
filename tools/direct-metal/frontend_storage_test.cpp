#include "native_transactional_map.h"
#include "native_shared_frame_arena.h"
#include "theft4_frame_plan.h"
#include "theft4_vector_storage_pool.h"
#include "theft4_bounded_cache_sweep.h"
#include <unordered_map>
#include <cassert>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <string_view>
#include <cstring>

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
static void BindingStorageChecks(){
  using Ref=std::shared_ptr<const int>;
  theft4::VectorStoragePool<Ref,Ref> pool(4096,2);
  std::vector<Ref> a,b;a.reserve(8);b.reserve(4);
  auto owner=std::make_shared<const int>(42);std::weak_ptr<const int> weak=owner;
  a.push_back(owner);b.push_back(owner);owner.reset();
  auto* address=a.data();pool.Recycle(a,b);assert(weak.expired());
  assert(pool.Entries()==1&&pool.Bytes()==12*sizeof(Ref));
  auto entry=pool.Acquire();assert(entry.first.empty()&&entry.second.empty());
  assert(entry.first.data()==address&&entry.first.capacity()==8&&entry.second.capacity()==4);
  // A separately live packet cannot be reused, even after many batches.
  entry.first.push_back(std::make_shared<const int>(99));auto pinned=entry.first.front();
  for(int i=0;i<1000;++i){auto scratch=pool.Acquire();scratch.first.reserve(8);
    scratch.first.push_back(std::make_shared<const int>(i));pool.Recycle(scratch.first,scratch.second);}
  assert(*entry.first.front()==99&&entry.first.front()==pinned);
  pool.Recycle(entry.first,entry.second);assert(pool.Entries()==2);
  auto overflow=pool.Acquire();overflow.first.reserve(4097/sizeof(Ref)+1);
  pool.Recycle(overflow.first,overflow.second);assert(overflow.first.empty()&&pool.Bytes()<=4096);
  theft4::VectorStoragePool<Ref,Ref> disabled(0,0);
  disabled.Recycle(overflow.first,overflow.second);assert(disabled.Entries()==0);
}
static void CompactPacketChecks(){
  namespace r=theft4::render;
  r::ProducedViews empty;empty.Set(4,std::nullopt);
  const auto& inspect=empty;
  assert(empty.AllocatedBytes()==0&&!inspect.HasViews()&&!inspect[4]);
  assert(std::all_of(inspect.begin(),inspect.end(),[](const auto& v){return !v;}));
  assert(empty.AllocatedBytes()==0); // Inspecting the static-image path stays allocation-free.
  empty.Set(4,r::SurfaceView{{91,1},0,0,r::Aspect::Color});
  auto copied=empty;copied[4]->surface.generation=2;
  assert(inspect[4]->surface.generation==1&&copied[4]->surface.generation==2);
  auto assigned=r::ProducedViews{};assigned=copied;assigned[4]->level=1;
  assert(copied[4]->level==0);assigned=inspect;assigned=assigned;
  assert(assigned[4]->surface.generation==1);
  auto moved=std::move(empty);assert(moved.HasViews()&&!empty.HasViews());
  moved.Set(4,std::nullopt);assert(!moved.HasViews());
  assigned=r::ProducedViews{};assert(assigned.AllocatedBytes()==0);
  r::HostDraw host;host.scissor={0,0,16,8};host.pipeline.attributes.resize(2);
  r::PassCommand original=host,clone=original;
  r::GetHostDraw(clone)->scissor[2]=4;r::GetHostDraw(clone)->pipeline.attributes.clear();
  assert(r::GetHostDraw(original)->scissor[2]==16&&r::GetHostDraw(original)->pipeline.attributes.size()==2);
  clone=original;r::GetHostDraw(clone)->stencil_front_reference=3;
  assert(r::GetHostDraw(original)->stencil_front_reference==0);
  auto retained=std::move(original);assert(r::GetHostDraw(retained)&&!r::GetHostDraw(original));
  r::FramePlan invalid;invalid.sequence=1;r::Pass pass;pass.attachmentless_extent={16,8};
  pass.commands.push_back(std::move(original));invalid.commands.push_back(std::move(pass));
  std::string error;r::SurfaceContents result,before{{{99,1},0,0,r::Aspect::Color}};result=before;
  assert(!r::ValidateFrame(invalid,before,result,error)&&result==before&&error.find("missing host")!=std::string::npos);
}
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
static void AttachmentChecks(){
  namespace r=theft4::render;
  std::mt19937 rng(55291);
  for(unsigned trial=0;trial<300;++trial){
    r::FramePlan plan;plan.sequence=trial+1;
    for(unsigned n=0;n<8;++n){auto s=std::make_shared<r::Surface>();s->key={n+1,1};s->width=16;s->height=16;s->levels=3;s->format=r::Format::RGBA8Unorm;plan.surfaces.push_back(s);}
    const auto clear=[&](unsigned id,unsigned mip){r::Pass pass;r::Attachment a;a.view={plan.surfaces[id]->key,mip,0,r::Aspect::Color};a.load=r::Load::Clear;a.store=r::Store::Store;pass.colors[0]=a;plan.commands.push_back(std::move(pass));};
    for(unsigned id=0;id<8;++id)for(unsigned mip=0;mip<3;++mip)clear(id,mip);
    for(unsigned n=0;n<100;++n){unsigned source=rng()%8,target=rng()%8,mip=rng()%3;
      if(source==target||rng()%2){clear(target,mip);continue;}
      r::ImageCopy copy;copy.source={plan.surfaces[source]->key,mip,0,r::Aspect::Color};copy.destination={plan.surfaces[target]->key,mip,0,r::Aspect::Color};
      unsigned extent=16u>>mip;if(rng()%2)extent/=2;copy.extent={extent,extent};plan.commands.push_back(copy);
    }
    plan.output=r::SurfaceView{plan.surfaces[0]->key,0,0,r::Aspect::Color};
    r::SurfaceContents expected,actual;std::string error;assert(r::ValidateFrame(plan,{},expected,error));
    auto optimized=plan;const auto masks=r::DeadAttachmentStores(plan);
    for(size_t n=0;n<masks.size();++n)if(masks[n]){auto& pass=std::get<r::Pass>(optimized.commands[n]);assert(masks[n]==1&&pass.colors[0]);pass.colors[0]->store=r::Store::Discard;}
    assert(r::ValidateFrame(optimized,{},actual,error));assert(actual==expected);
  }
}
static void BoundedSweepChecks() {
  std::unordered_map<unsigned,std::weak_ptr<int>> cache;std::vector<std::shared_ptr<int>> owners;
  for(unsigned i=0;i<20000;++i){owners.push_back(std::make_shared<int>(i));cache.emplace(i,owners.back());}
  for(unsigned i=0;i<owners.size();i+=3)owners[i].reset();size_t cursor=0;
  const auto expired=[](const auto& e){return e.second.expired();};
  for(unsigned i=0;i<400;++i) {
    if(i%7==0){auto key=50000+i;owners.push_back(std::make_shared<int>(key));cache.emplace(key,owners.back());}
    theft4::SweepCacheBuckets(cache,cursor,128,expired);
  }
  for(const auto& [key,owner]:cache)assert(!owner.expired());
  assert(cache.size()==20000-6667+58);
  owners.clear();for(unsigned i=0;i<400;++i)theft4::SweepCacheBuckets(cache,cursor,128,expired);
  assert(cache.empty());
}
static void HostOverwriteChecks() {
  namespace r=theft4::render;r::FramePlan frame;
  auto source=std::make_shared<r::Surface>();source->key={91,1};source->format=r::Format::RGBA8Unorm;source->width=16;source->height=8;
  auto target=std::make_shared<r::Surface>(*source);target->key={92,1};frame.surfaces={source,target};
  r::Pass pass;r::Attachment attachment;attachment.view={target->key,0,0,r::Aspect::Color};attachment.load=r::Load::Load;attachment.store=r::Store::Store;
  pass.colors[0]=attachment;r::HostDraw host;host.program=r::HostProgram::Resolve;host.pipeline.colors[0]=target->format;host.scissor={0,0,16,8};
  host.fetches[0].produced=r::SurfaceView{source->key,0,0,r::Aspect::Color};host.fetches[0].sampler=std::make_shared<r::Sampler>();
  auto constants=std::make_shared<r::Bytes>();constants->generation=1;
  std::array<uint32_t,16> fields{};fields[8]=1;fields[11]=2;fields[12]=fields[14]=16;fields[13]=fields[15]=8;
  constants->value.resize(64);std::memcpy(constants->value.data(),fields.data(),64);host.constants={constants,0,64};pass.commands.push_back(host);frame.commands.push_back(pass);
  assert(r::RedundantAttachmentLoads(frame)==std::vector<uint8_t>{1});assert(r::IdentityResolveCopy(frame,pass));
  auto discarded=frame;std::get<r::Pass>(discarded.commands[0]).colors[0]->load=r::Load::Discard;
  assert(r::RedundantAttachmentLoads(discarded)==std::vector<uint8_t>{0});
  assert(r::IdentityResolveCopy(discarded,std::get<r::Pass>(discarded.commands[0])));
  // Every conversion-sensitive field stays on the shader path.
  for(size_t i:{0u,1u,2u,3u,8u,9u,10u,11u,12u,13u,14u,15u}) {
    auto changed=fields;changed[i]^=1;std::memcpy(constants->value.data(),changed.data(),64);assert(!r::IdentityResolveCopy(frame,pass));
  }
  std::memcpy(constants->value.data(),fields.data(),64);
  for(unsigned mutation=0;mutation<8;++mutation) {
    auto changed=pass;auto& h=(*r::GetHostDraw(changed.commands[0]));
    switch(mutation){case 0:h.scissor[2]--;break;case 1:h.pipeline.blends[0].enabled=true;break;
      case 2:h.pipeline.blends[0].write_mask=7;break;case 3:h.program=r::HostProgram::SmaaEdgeHigh;break;
      case 4:h.pipeline.depth_test=true;break;case 5:changed.depth=attachment;break;
      case 6:h.pipeline.sample_mask=0;break;case 7:h.pipeline.samples=4;break;}
    auto candidate=frame;candidate.commands[0]=changed;assert(r::RedundantAttachmentLoads(candidate)==std::vector<uint8_t>{0});assert(!r::IdentityResolveCopy(candidate,changed));
  }
  // A full host overwrite consumes no previous destination pixels. Its old
  // store may be discarded, but the source sampled by the host must survive.
  r::Pass previous;previous.colors[0]=attachment;previous.colors[0]->load=r::Load::Clear;
  frame.commands.insert(frame.commands.begin(),previous);
  assert(r::DeadAttachmentStores(frame)==std::vector<uint8_t>({1,0}));
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
  // Identical attachment/draw order, alternating trials. The legacy producer
  // allocates a temporary vector for each draw before joining its pass.
  auto capture=std::make_shared<r::Capture>();
  auto assembly=[&](bool direct){
    r::FramePlan frame;frame.commands.reserve(200);auto began=Clock::now();
    for(unsigned n=0;n<4000;++n){r::Pass p;r::Attachment a;
      a.view={{n/20+1,1},0,0,r::Aspect::Color};a.load=n%20?r::Load::Load:r::Load::Clear;a.store=r::Store::Store;p.colors[0]=a;
      r::FrameDraw draw;draw.capture=capture;
      if(n%10==0)draw.produced.Set(0,r::SurfaceView{{999,1},0,0,r::Aspect::Color});
      if(direct)r::AppendPass(frame,std::move(p)).commands.emplace_back(std::move(draw));
      else {p.commands.emplace_back(std::move(draw));r::AppendPass(frame,std::move(p));}
    }
    auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-began).count();
    assert(frame.commands.size()==200);
    for(const auto& c:frame.commands){const auto& pass=std::get<r::Pass>(c);assert(pass.commands.size()==20);
      assert(pass.colors[0]->load==r::Load::Clear);checksum+=pass.commands.size();}
    return elapsed;
  };
  std::vector<double> temporary,direct;assembly(false);assembly(true);
  for(unsigned trial=0;trial<101;++trial){if(trial%2){direct.push_back(assembly(true));temporary.push_back(assembly(false));}
    else{temporary.push_back(assembly(false));direct.push_back(assembly(true));}}
  std::sort(temporary.begin(),temporary.end());std::sort(direct.begin(),direct.end());
  std::cout<<"frame_draw_bytes="<<sizeof(r::FrameDraw)<<" host_draw_bytes="<<sizeof(r::HostDraw)
    <<" pass_command_bytes="<<sizeof(r::PassCommand)<<" pass_bytes="<<sizeof(r::Pass)
    <<" draws=4000 passes=200 produced_inputs=400 legacy_append_ms="<<temporary[50]
    <<" direct_append_ms="<<direct[50]<<" checksum="<<checksum<<"\n";

}
int main(int argc,char** argv){CompactPacketChecks();BindingStorageChecks();TransactionChecks();ArenaChecks();AttachmentChecks();HostOverwriteChecks();BoundedSweepChecks();if(argc>1&&std::string_view(argv[1])=="--benchmark")Benchmark();std::cout<<"frontend storage differential and lifetime checks passed\n";}
