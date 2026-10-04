#include "native_metal_constant_projection.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
using namespace rex::graphics::gta4_native;
using Bytes=std::vector<uint8_t>;
static void CopyWords(uint8_t* out,const uint8_t* in,size_t size) {
  assert(size%4==0);
  for(size_t i=0;i<size;i+=4) {
    uint32_t word;std::memcpy(&word,in+i,4);word=__builtin_bswap32(word);std::memcpy(out+i,&word,4);
  }
}
static auto View(const Bytes& bytes){return std::span<const uint8_t>(bytes);}
static void CheckCovered(const Bytes& actual,const Bytes& guest,const NativeConstantMask& mask) {
  for(size_t reg=0;reg<guest.size()/16;++reg) {
    std::array<uint8_t,16> expected{};
    if(mask[reg/64]&(uint64_t{1}<<(reg%64)))CopyWords(expected.data(),guest.data()+16*reg,16);
    assert(!std::memcmp(expected.data(),actual.data()+16*reg,16));
  }
}
static void MaskedMetalReplay() {
  std::mt19937 random(98104);size_t writes=0,fallbacks=0;
  for(size_t bank:{size_t(4096),size_t(3584)}) {
    NativeMetalConstantProjection<Bytes> cache;AuthoritativeConstantState state(bank);
    Bytes canonical(bank);for(auto& b:canonical)b=uint8_t(random());
    ConstantPayloadDelta delta;CaptureCompleteConstantSnapshot(canonical,delta);
    auto hash=[](std::span<const uint8_t>){return uint64_t(1);};
    auto version=state.Apply(delta,hash).version;
    NativeConstantUsage usage;usage.known=true;usage.banks[0][0]=1;
    std::shared_ptr<const Bytes> payload;
    for(size_t step=0;step<3000;++step) {
      if(step%17==0) {
        usage.banks[0]={};
        for(size_t i=0;i<8;++i){size_t reg=random()%(bank/16);usage.banks[0][reg/64]|=uint64_t{1}<<(reg%64);}
        // Last-register and masks spanning 64-bit boundaries.
        size_t reg=bank/16-1;usage.banks[0][reg/64]|=uint64_t{1}<<(reg%64);
      }
      const size_t offset=(random()%(bank/4))*4;
      const size_t count=std::min(bank-offset,size_t(4*(1+random()%20)));
      delta={};delta.ranges={{uint32_t(offset),0,uint32_t(count)}};
      for(size_t j=0;j<count;++j)canonical[offset+j]=uint8_t(random());
      delta.payload.assign(canonical.begin()+offset,canonical.begin()+offset+count);
      if(step%41==0)CaptureCompleteConstantSnapshot(canonical,delta);
      version=state.Apply(delta,hash).version;assert(version);
      auto before_parent=version->parent;auto before_materialized=version->materialized;
      Bytes covered(bank);
      if(cache.WriteMasked(11,12,0,version,usage,covered,View,CopyWords)) {
        ++writes;CheckCovered(covered,canonical,usage.banks[0]);
        assert(version->parent==before_parent&&version->materialized==before_materialized);
        payload=std::make_shared<const Bytes>(std::move(covered));
      } else {
        ++fallbacks;
        auto full=AuthoritativeConstantState::MaterializeView(version);assert(full&&*full==canonical);
        CopyWords(covered.data(),full->data(),bank);payload=std::make_shared<const Bytes>(std::move(covered));
      }
      cache.Remember(11,12,0,version,usage,payload);
      // A poisoned previous sparse payload must never satisfy unknown/new
      // masks or another shader. Reconstruct from the actual guest version.
      if(step%23==0) {
        auto changed=usage;changed.banks[0]={};changed.banks[0][0]=2;
        Bytes other(bank);
        if(cache.WriteMasked(11,13,0,version,changed,other,View,CopyWords))CheckCovered(other,canonical,changed.banks[0]);
        auto unknown=usage;unknown.known=false;Bytes untouched(bank,0xA7);
        assert(!cache.WriteMasked(11,12,0,version,unknown,untouched,View,CopyWords));
        assert(untouched==Bytes(bank,0xA7));
        assert(!cache.WriteMasked(11,12,2,version,usage,untouched,View,CopyWords));
        assert(!cache.WriteMasked(11,12,0,version,usage,std::span<uint8_t>(untouched).first(bank-1),View,CopyWords));
      }
      if(step%71==0)assert(*AuthoritativeConstantState::MaterializeView(version)==canonical);
    }
  }
  assert(writes>5000&&fallbacks>0);
  std::cout<<"Metal sparse replay: 6000 differential bank updates, changed masks/shaders, host/guest bases, untouched holes and bounded fallback passed\n";
}
static void Benchmark() {
  constexpr size_t draws=5000,bank=4096;
  auto run=[=](bool sparse) {
    AuthoritativeConstantState state(bank);Bytes canonical(bank,0x19);ConstantPayloadDelta delta;
    CaptureCompleteConstantSnapshot(canonical,delta);
    auto hash=[](std::span<const uint8_t>){return uint64_t(1);};
    state.Apply(delta,hash);
    std::vector<std::shared_ptr<const ConstantStateVersion>> versions;
    for(size_t i=0;i<draws;++i) {
      delta={};delta.ranges={{uint32_t((i%32)*16),0,16}};delta.payload.assign(16,uint8_t(i));
      versions.push_back(state.Apply(delta,hash).version);
    }
    NativeConstantUsage usage;usage.known=true;usage.banks[0][0]=0xffffffff;
    NativeMetalConstantProjection<Bytes> cache;
    std::vector<std::shared_ptr<const Bytes>> outputs;outputs.reserve(draws);
    uint64_t written=0,checksum=0;size_t fallbacks=0;
    const auto began=std::chrono::steady_clock::now();
    for(const auto& version:versions) {
      auto payload=std::make_shared<Bytes>(bank);
      const auto n=sparse?cache.WriteMasked(11,12,0,version,usage,*payload,View,CopyWords):0;
      if(n)written+=n;
      else {
        if(sparse)++fallbacks;
        auto full=AuthoritativeConstantState::MaterializeView(version);assert(full);
        // Match the old Metal temporary host vector plus immutable copy.
        Bytes host(bank);CopyWords(host.data(),full->data(),bank);*payload=host;written+=bank;
      }
      cache.Remember(11,12,0,version,usage,payload);outputs.push_back(payload);
      checksum+=(*payload)[0]+(*payload)[511];
    }
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
    return std::tuple{ms,written,checksum,fallbacks};
  };
  std::array<double,9> full{},sparse{};uint64_t full_bytes=0,sparse_bytes=0;size_t fallbacks=0;
  for(size_t i=0;i<full.size();++i) {
    auto a=run(i%2==0),b=run(i%2!=0);auto f=i%2? a:b;auto s=i%2?b:a;
    full[i]=std::get<0>(f);sparse[i]=std::get<0>(s);full_bytes=std::get<1>(f);sparse_bytes=std::get<1>(s);
    assert(std::get<2>(f)==std::get<2>(s));fallbacks=std::get<3>(s);
  }
  std::sort(full.begin(),full.end());std::sort(sparse.begin(),sparse.end());
  std::cout<<"BENCH Metal 5000 changed banks, 32/256 registers: full_ms="<<full[4]<<" sparse_ms="<<sparse[4]
      <<" full_bytes="<<full_bytes<<" sparse_bytes="<<sparse_bytes<<" fallbacks="<<fallbacks<<'\n';
}
int main(int argc,char**) {
  using Bytes=std::vector<uint8_t>;
  NativeMetalConstantProjection<Bytes> cache;
  NativeConstantUsage usage;usage.known=true;usage.banks[0][0]=4;
  AuthoritativeConstantState state(4096);
  Bytes canonical(4096);ConstantPayloadDelta delta;CaptureCompleteConstantSnapshot(canonical,delta);
  auto hash=[](std::span<const uint8_t>){return uint64_t(1);};
  auto version=state.Apply(delta,hash).version;
  auto payload=std::make_shared<const Bytes>(canonical);
  cache.Remember(11,12,0,version,usage,payload);
  assert(cache.Find(11,12,0,version,usage)==payload);
  assert(!cache.Find(11,13,0,version,usage));
  assert(!cache.Find(11,12,1,version,usage));
  assert(!cache.Find(11,12,2,version,usage));
  auto unknown=usage;unknown.known=false;assert(!cache.Find(11,12,0,version,unknown));
  auto different=usage;different.banks[0][0]=8;assert(!cache.Find(11,12,0,version,different));
  std::mt19937 random(10496);size_t changed_reuses=0,used_changes=0;
  for(size_t i=0;i<2000;++i) {
    // Frequent unused-register changes, used-register updates and complete
    // snapshot boundaries. Every accepted view must match exact used bytes.
    const size_t offset=(i%7==0?2:3+random()%253)*16;
    for(size_t j=0;j<16;++j)canonical[offset+j]=uint8_t(random());
    delta={};delta.ranges={{uint32_t(offset),0,16}};
    delta.payload.assign(canonical.begin()+offset,canonical.begin()+offset+16);
    if(i%43==0)CaptureCompleteConstantSnapshot(canonical,delta);
    version=state.Apply(delta,hash).version;assert(version);
    auto reused=cache.Find(11,12,0,version,usage);
    if(reused) {
      assert(offset!=32&&i%43!=0);++changed_reuses;
      assert(std::memcmp(reused->data()+32,canonical.data()+32,16)==0);
    } else {
      if(offset==32)++used_changes;
      payload=std::make_shared<const Bytes>(canonical);
      cache.Remember(11,12,0,version,usage,payload);
    }
  }
  assert(changed_reuses>1000&&used_changes>200&&cache.changed_version_hits==changed_reuses);
  // A different owner and an expired ancestor cannot reuse prior payloads.
  auto unrelated=std::make_shared<ConstantStateVersion>();unrelated->byte_size=4096;
  assert(!cache.Find(11,12,0,unrelated,usage));
  std::weak_ptr<const ConstantStateVersion> expired=version;
  version.reset();state=AuthoritativeConstantState(4096);assert(expired.expired());
  assert(!cache.Find(11,12,0,unrelated,usage));
  cache.Clear();assert(!cache.Find(11,12,0,unrelated,usage)&&!cache.hits);
  std::cout<<"Metal shader-specific constant views: 2000 differential updates, unknown/changed masks, used changes, full snapshots, owners and reset passed\n";
  MaskedMetalReplay();if(argc>1)Benchmark();
}
