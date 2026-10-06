#include "theft4_render_plan.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
using namespace theft4::render;
static auto Source(size_t ranges,uint32_t width=2) {
  auto source=std::make_shared<Bytes>();source->generation=1;source->value.resize(ranges*512*width);
  for(size_t i=0;i<ranges*512;++i) {
    const uint32_t value=i%509==0?(width==2?UINT16_MAX:UINT32_MAX):uint32_t(i%2000);
    if(width==2){const uint16_t v=value;std::memcpy(source->value.data()+i*width,&v,width);}
    else std::memcpy(source->value.data()+i*width,&value,width);
  }
  return source;
}
static void Contracts() {
  std::string error;IndexRange result;
  for(uint32_t width:{2u,4u}) {
    auto source=Source(10000,width);IndexRangeCache cache;
    const Buffer hot{source,0,512*width};
    assert(cache.Analyze(hot,512,width,result,error));const auto expected=result;
    for(size_t i=1;i<10000;++i) {
      const Buffer view{source,i*512*width,512*width};IndexRange oracle;
      assert(AnalyzeIndices(view,512,width,oracle,error)&&cache.Analyze(view,512,width,result,error));
      assert(result.minimum==oracle.minimum&&result.maximum==oracle.maximum&&
        result.minimum_without_restart==oracle.minimum_without_restart&&
        result.maximum_without_restart==oracle.maximum_without_restart&&
        result.has_restart==oracle.has_restart&&result.has_non_restart==oracle.has_non_restart);
      if(i%32==0) {
        const auto scanned=cache.ScannedIndices();assert(cache.Analyze(hot,512,width,result,error));
        assert(cache.ScannedIndices()==scanned&&result.maximum==expected.maximum);
        assert(cache.Analyze(hot,512,width,result,error)&&cache.ScannedIndices()==scanned);
      }
    }
    const auto scanned=cache.ScannedIndices();assert(cache.Analyze({source,512*width,512*width},512,width,result,error));
    assert(cache.ScannedIndices()==scanned+512); // Old cold entry was evicted; cache stays bounded.
    assert(!cache.Analyze({source,0,width},512,width,result,error));
    assert(!cache.Analyze({source,1,512*width},512,width,result,error));
    assert(!cache.Analyze(hot,512,3,result,error));assert(!cache.Analyze(hot,0,width,result,error));
    assert(cache.Analyze(hot,1,width,result,error)&&result.minimum==result.maximum); // Count is part of the key.
    auto alias=std::shared_ptr<const Bytes>(source.get(),[](const Bytes*){});
    const auto before=cache.ScannedIndices();assert(cache.Analyze({alias,0,512*width},512,width,result,error));
    assert(cache.ScannedIndices()==before+512); // Same pointer with different control block cannot hit.
    assert(cache.Analyze(hot,512,width,result,error)&&cache.ScannedIndices()==before+1024);
    source->generation++;const uint32_t value=99;
    if(width==2){const uint16_t v=value;std::memcpy(source->value.data(),&v,width);}
    else std::memcpy(source->value.data(),&value,width);
    assert(cache.Analyze(hot,1,width,result,error)&&result.minimum==99);
    source->conversion[0]++;const auto after=cache.ScannedIndices();
    assert(cache.Analyze(hot,1,width,result,error)&&cache.ScannedIndices()==after+1);
    source->value.resize(source->value.size()-width);const auto resized=cache.ScannedIndices();
    assert(cache.Analyze(hot,1,width,result,error)&&cache.ScannedIndices()==resized+1);
    alias.reset();
    // Buffer views below own the source until the scope exits; the cache only stores weak owners.
    IndexRangeCache moved=std::move(cache);assert(moved.Analyze(hot,1,width,result,error));
  }
  IndexRangeCache cache;std::weak_ptr<const Bytes> weak;
  {auto source=Source(1);weak=source;assert(cache.Analyze({source,0,1024},512,2,result,error));}
  assert(weak.expired());
  std::cout<<"index range cache contracts passed: saturation, hot retention, cold eviction, range parity, version/owner identity, bounds, move, weak lifetime\n";
}
static void Benchmark() {
  std::string error;IndexRange range;auto source=Source(12000);IndexRangeCache cache;
  constexpr size_t draws=4500,frames=24;
  std::vector<double> times;uint64_t checksum=0;
  for(size_t frame=0;frame<frames;++frame) {
    const auto start=std::chrono::steady_clock::now();
    for(size_t draw=0;draw<draws;++draw) {
      const Buffer b{source,((draw*31)%4096)*1024,1024};
      for(size_t admission=0;admission<2;++admission) {
        assert(cache.Analyze(b,512,2,range,error));checksum+=range.maximum_without_restart;
      }
    }
    times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  }
  const auto cold=times.front();times.erase(times.begin());std::sort(times.begin(),times.end());
  std::cout<<"warm index lookup: draws="<<draws<<" repeated-admissions=2 median-ms="<<times[times.size()/2]
      <<" cold-ms="<<cold<<" checksum="<<checksum<<" scanned="<<cache.ScannedIndices()<<"\n";
  IndexRangeCache churn;uint64_t hot_rescans=0;
  const auto start=std::chrono::steady_clock::now();
  for(size_t i=0;i<12000;++i) {
    assert(churn.Analyze({source,i*1024,1024},512,2,range,error));
    if(i%32==0) {
      const auto before=churn.ScannedIndices();assert(churn.Analyze({source,0,1024},512,2,range,error));
      hot_rescans+=churn.ScannedIndices()-before;
    }
  }
  std::cout<<"saturation index lookup: new-views=12000 hot-rescanned-indices="<<hot_rescans
      <<" ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<"\n";
}
int main(int argc,char** argv) {
  if(argc>1&&std::strcmp(argv[1],"--benchmark")==0)Benchmark();else Contracts();
}
