#include "native_masked_constants.h"
#include "native_immutable_bindings.h"
#define XXH_INLINE_ALL
#include "xxhash.h"
#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace rex::graphics::gta4_native;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static uint64_t Hash(std::span<const uint8_t> s) { return XXH3_64bits(s.data(),s.size()); }
static void Copy(uint8_t* out, const uint8_t* in, size_t bytes) {
  for (size_t i=0;i<bytes;i+=4) { uint32_t x; std::memcpy(&x,in+i,4); x=__builtin_bswap32(x); std::memcpy(out+i,&x,4); }
}
static NativeConstantMask Mask(size_t first, size_t count) {
  NativeConstantMask m{}; for(size_t i=first;i<first+count;++i) m[i/64]|=uint64_t{1}<<(i%64); return m;
}
template<class F> static double Measure(F&& f) {
  std::array<double,11> times{};
  for(auto& t:times) { auto start=std::chrono::steady_clock::now(); f(); t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); }
  std::sort(times.begin(),times.end()); return times[5];
}
struct Allocation { uint8_t* mapping=nullptr; };

static void CoverageAndFences() {
  ConstantStateVersion v; v.byte_size=4096;
  NativeFrameConstantCoverage<Allocation> coverage;
  Check(coverage.Observe(&v,Mask(2,3),true),"first coverage");
  Check(coverage.Observe(&v,Mask(210,1),true),"second coverage");
  auto* e=coverage.Find(&v); Check(e&&e->known&&NativeConstantMaskContains(e->mask,Mask(210,1)),"all consumers union");
  Check(NativeMaskedConstantExtent(e->mask)==211*16,"bank ABI extent");
  Check(coverage.Observe(&v,{},false)&&!e->known,"unknown consumer did not disable");
  Check(coverage.Reset()&&!coverage.Find(&v),"stale coverage after reset");
  Check(coverage.Observe(&v,Mask(0,1),true),"new generation");
  coverage.Find(&v)->prepared=true;
  Check(!coverage.Observe(&v,Mask(1,1),true),"late widening of prepared version");
  FrameConstantArenaIndex arena; Check(arena.SetByteCapacity(1024),"capacity");
  auto a=arena.ReserveTransient(17,256), b=arena.FindOrReserve({FrameConstantKind::kVertex,1},19,256);
  Check(a&&b&&a->offset==0&&b->offset==256&&arena.reservation_count()==2,"mixed reservations");
  Check(arena.MarkSubmitted(2)&&!arena.ReserveTransient(1,1)&&!arena.ResetAfterCompletion(1),"inflight changed");
  Check(arena.ResetAfterCompletion(2)&&arena.bytes_used()==0&&arena.reservation_count()==0,"fence reset");
  Check(!arena.ReserveTransient(1025,1)&&!arena.ReserveTransient(1,3),"invalid reserve");
  Check(arena.ReserveTransient(1024,1).has_value()&&!arena.ReserveTransient(1,1),"exact capacity");
  Check(arena.ResetUnsubmitted(),"rollback");
  FrameGenerationMap<uint64_t,uint64_t> mixed, legacy(false);
  for(size_t generation=0;generation<20;++generation) {
    for(uint64_t i=0;i<4096;++i) { auto a=mixed.Insert(i<<8,i^17),b=legacy.Insert(i<<8,i^17); Check(a&&b&&a.inserted&&b.inserted,"insert"); }
    for(uint64_t i=0;i<4096;++i) Check(*mixed.Find(i<<8)==*legacy.Find(i<<8),"collision equality");
    Check(mixed.ResetGeneration()&&legacy.ResetGeneration()&&!mixed.Find(0),"stale cache");
  }
}

static void MixedFrameTruth() {
  constexpr size_t bank=4096, count=1600;
  AuthoritativeConstantState state(bank);std::vector<uint8_t> source(bank,0x42);
  ConstantPayloadDelta initial;CaptureCompleteConstantSnapshot(source,initial);state.Apply(initial,Hash);
  struct Draw { std::shared_ptr<const ConstantStateVersion> version; NativeConstantMask mask; bool known; size_t truth; };
  std::vector<Draw> draws;std::vector<std::vector<uint8_t>> truth;
  for(size_t i=0;i<count;++i){ConstantPayloadDelta d;d.ranges={{uint32_t((i%64)*16),0,16}};d.payload.assign(16,uint8_t(i));std::copy(d.payload.begin(),d.payload.end(),source.begin()+d.ranges[0].destination_offset);auto v=state.Apply(d,Hash).version;truth.push_back(source);
    for(size_t family=0;family<3;++family){auto m=Mask((i%3)*32+family*4,8);if(family==2&&i%7==0)m=Mask(250,6);bool known=!(family==1&&i%11==0);if(!known)m=Mask(0,256);draws.push_back({v,m,known,i});}}
  NativeFrameConstantCoverage<Allocation> coverage;
  for(auto& d:draws)Check(coverage.Observe(d.version.get(),d.mask,d.known),"mixed observe");
  FrameConstantArenaIndex index;std::vector<uint8_t> output(count*bank,0xD3);Check(index.SetByteCapacity(output.size()),"mixed capacity");NativeImmutableBindings<Allocation> full;
  size_t covered=0, fallbacks=0, repeated=0;
  auto upload=[&](auto kind,uint64_t id,const auto& data,Allocation& out){auto r=index.FindOrReserve({kind,id},data.size(),256);if(!r)return false;out.mapping=output.data()+r->offset;if(!r->reused)Copy(out.mapping,data.data(),data.size());return true;};
  auto delta=[&](auto kind,uint64_t id,const Allocation& parent,const auto& data,size_t size,Allocation& out){auto r=index.FindOrReserve({kind,id},size,256);if(!r)return false;out.mapping=output.data()+r->offset;if(!r->reused){std::memcpy(out.mapping,parent.mapping,size);for(auto& r:data.ranges)Copy(out.mapping+r.destination_offset,data.payload.data()+r.payload_offset,r.byte_count);}return true;};
  for(auto& draw:draws){auto* entry=coverage.Find(draw.version.get());Allocation out;bool ready=entry->prepared;if(ready){out=entry->allocation;++repeated;}
    if(!ready&&entry->known&&!entry->fallback){size_t bytes=0;for(auto bits:entry->mask)bytes+=std::popcount(bits)*16;auto* parent=coverage.Find(draw.version->parent.get());bool parent_covered=parent&&parent->prepared&&NativeConstantMaskContains(parent->mask,entry->mask);NativeMaskedConstantPlan plan;
      if(bytes&&bytes<bank*3/4&&plan.Build(draw.version.get(),entry->mask,parent_covered?draw.version->parent.get():nullptr,parent_covered?std::span<const uint8_t>(parent->allocation.mapping,NativeMaskedConstantExtent(parent->mask)):std::span<const uint8_t>{})){auto r=index.ReserveTransient(plan.extent(),256);Check(bool(r),"mixed covered reserve");out.mapping=output.data()+r->offset;Check(plan.Write({out.mapping,plan.extent()},Copy)>0,"mixed replay");entry->allocation=out;entry->prepared=ready=true;++covered;}}
    if(!ready){entry->fallback=true;const std::vector<uint8_t>* data=nullptr;auto result=full.BindWithDelta(FrameConstantKind::kVertex,draw.version,out,data,AuthoritativeConstantState::MaterializeView,upload,delta);Check(result!=NativeImmutableBindings<Allocation>::Result::kFailure,"mixed full fallback");++fallbacks;}
    // Oracle owns complete guest bytes independent of replay/materialization.
    ForNativeConstantRegisters(draw.mask,[&](size_t reg){std::array<uint8_t,16> expected{};Copy(expected.data(),truth[draw.truth].data()+reg*16,16);Check(std::memcmp(expected.data(),out.mapping+reg*16,16)==0,"mixed shader read differs from canonical truth");});
  }
  Check(covered>100&&fallbacks>100&&repeated>100&&index.reservation_count()<=count,"mixed path coverage/one allocation bound");
  std::cout<<"MIXED draws="<<draws.size()<<" covered_versions="<<covered<<" full_bindings="<<fallbacks<<" covered_reuses="<<repeated<<" bytes="<<index.bytes_used()<<" full_budget="<<output.size()<<'\n';
}

static void LookupBenchmark() {
  std::mt19937_64 rng(95);
  for(size_t alignment:{size_t(16),size_t(64),size_t(256),size_t(1)}) {
    std::vector<uint64_t> keys(5000); for(size_t i=0;i<keys.size();++i)keys[i]=alignment==1?rng():0x100000000ULL+i*alignment;
    FrameGenerationMap<uint64_t,uint64_t> legacy(false), mixed;
    uint64_t checksum=0;
    auto run=[&](auto& map) { Check(map.ResetGeneration(),"map reset"); for(auto k:keys)Check(bool(map.Insert(k,k)),"lookup insert"); for(size_t repeat=0;repeat<4;++repeat)for(auto k:keys){auto* hit=map.Find(k); Check(hit!=nullptr,"lookup missing"); checksum^=*hit;} };
    double old=Measure([&]{run(legacy);}), now=Measure([&]{run(mixed);});
    std::cout<<"LOOKUP keys=5000 alignment="<<alignment<<" legacy_ms="<<old<<" mixed_ms="<<now<<" checksum="<<checksum<<'\n';
  }
}

static void BindingBenchmark() {
  constexpr size_t draws=5000,bank=4096;
  AuthoritativeConstantState state(bank); std::vector<uint8_t> canonical(bank,0x2A);
  ConstantPayloadDelta initial; CaptureCompleteConstantSnapshot(canonical,initial); state.Apply(initial,Hash);
  std::vector<std::shared_ptr<const ConstantStateVersion>> versions;
  for(size_t i=0;i<draws;++i) { ConstantPayloadDelta d; d.ranges={{uint32_t((i%32)*16),0,16}}; d.payload.assign(16,uint8_t(i)); versions.push_back(state.Apply(d,Hash).version); }
  // Fresh instances per comparison keep fallback materialization independent.
  std::vector<uint8_t> output(draws*bank+4096,0xD3);
  NativeImmutableBindings<Allocation> bindings(false), corrected;
  FrameConstantArenaIndex arena(false), newarena;
  Check(arena.SetByteCapacity(output.size())&&newarena.SetByteCapacity(output.size()),"benchmark capacity");
  NativeFrameConstantCoverage<Allocation> coverage;
  uint64_t bytes=0,checksum=0;
  auto baseline=[&](auto& cache,auto& index) {
    bytes=0; Check(cache.Reset()&&index.ResetUnsubmitted(),"full reset");
    FrameGenerationMap<const ConstantStateVersion*,uint8_t> sizing(false);
    for(auto& v:versions) Check(bool(sizing.Insert(v.get(),1)),"full sizing");
    auto upload=[&](auto kind,uint64_t identity,const auto& data,Allocation& out) { auto r=index.FindOrReserve({kind,identity},data.size(),256); if(!r)return false; out.mapping=output.data()+r->offset; if(!r->reused){Copy(out.mapping,data.data(),data.size());bytes+=data.size();} return true; };
    auto delta=[&](auto kind,uint64_t identity,const Allocation& parent,const auto& d,size_t size,Allocation& out) { auto r=index.FindOrReserve({kind,identity},size,256); if(!r)return false; out.mapping=output.data()+r->offset; if(!r->reused){std::memcpy(out.mapping,parent.mapping,size); bytes+=size; for(auto& range:d.ranges){Copy(out.mapping+range.destination_offset,d.payload.data()+range.payload_offset,range.byte_count);bytes+=range.byte_count;}}return true; };
    for(auto& v:versions) { Allocation out; const std::vector<uint8_t>* data=nullptr; auto result=cache.BindWithDelta(FrameConstantKind::kVertex,v,out,data,AuthoritativeConstantState::MaterializeView,upload,delta); Check(result!=NativeImmutableBindings<Allocation>::Result::kFailure,"full binding"); checksum+=out.mapping[0]; }
  };
  for(bool scattered:{false,true}) {
    auto mask=Mask(0,32); if(scattered){mask=Mask(0,16); auto high=Mask(192,16);for(size_t i=0;i<4;++i)mask[i]|=high[i];}
    auto covered=[&] {
      bytes=0; Check(coverage.Reset()&&newarena.ResetUnsubmitted(),"covered reset");
      for(auto& v:versions)Check(coverage.Observe(v.get(),mask,true),"covered sizing");
      for(auto& v:versions) {
        auto* e=coverage.Find(v.get()); auto* parent=coverage.Find(v->parent.get());
        NativeMaskedConstantPlan plan;
        Check(plan.Build(v.get(),e->mask,parent&&parent->prepared?v->parent.get():nullptr,
          parent&&parent->prepared?std::span<const uint8_t>(parent->allocation.mapping,NativeMaskedConstantExtent(parent->mask)):std::span<const uint8_t>{}),"covered binding plan");
        auto r=newarena.ReserveTransient(plan.extent(),256); Check(bool(r),"covered capacity");
        e->allocation.mapping=output.data()+r->offset; bytes+=plan.Write({e->allocation.mapping,plan.extent()},Copy); e->prepared=true; checksum+=e->allocation.mapping[0];
      }
    };
    // Covered runs first, before the baseline's complete fallback memoizes bytes.
    double sparse=Measure(covered); uint64_t sparse_bytes=bytes; size_t reserved=newarena.bytes_used();
    double full=Measure([&]{baseline(bindings,arena);}); uint64_t full_bytes=bytes;
    double hashfix=Measure([&]{baseline(corrected,newarena);});
    std::cout<<"BINDING draws=5000 registers=32 scattered="<<scattered<<" legacy_ms="<<full<<" hash_fixed_ms="<<hashfix<<" covered_ms="<<sparse<<" legacy_bytes="<<full_bytes<<" covered_bytes="<<sparse_bytes<<" reserved="<<reserved<<" checksum="<<checksum<<'\n';
  }
}
int main(int argc,char**) {
  CoverageAndFences(); MixedFrameTruth();
  std::cout<<"PASS: union coverage, unknown consumers, late widening, fence/rollback/capacity, generation collisions\n";
  if(argc>1){LookupBenchmark();BindingBenchmark();}
}
