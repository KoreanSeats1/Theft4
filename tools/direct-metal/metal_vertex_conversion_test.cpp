#define THEFT4_DIRECT_METAL_BACKEND 1
#define REXLOG_INFO(...) ((void)0)
#include "native_metal_vertex_conversion.h"
#include "native_frame_scheduling.h"
#include "native_incremental_owner_cache.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <vector>
using namespace rex::graphics::gta4_native;
struct VertexElement {uint32_t stream=0,offset=0,type=0;uint8_t usage=0,usage_index=0;};
enum class Numeric {kFloat,kSignedInteger,kUnsignedInteger};
struct Input {uint32_t location=0;Numeric numeric_type=Numeric::kFloat;};
struct Declaration {std::vector<VertexElement> elements;uint64_t content_hash=1;uint32_t handle=1;};
struct Shader {std::vector<Input> vertex_inputs;uint64_t hash=1;};
#include "metal-vertex-converter.inc"
namespace memory {
enum class ResourceKind {kVertexConversion};
enum class LifecycleAction {kCreate,kDestroy};
enum class LifecycleReason {kCacheMiss,kSuperseded};
}
static uint64_t creates=0;
template<class... Args> void RecordNativeMemoryLifecycle(memory::ResourceKind,
    memory::LifecycleAction action,Args&&...) {if(action==memory::LifecycleAction::kCreate)++creates;}
static uint64_t NextNativeTraceDiagnosticCount(std::atomic<uint64_t>& value){return ++value;}
struct Gta4NativeGraphicsSystem {
  struct NativeBufferResource {
    struct ConvertedVertexPayload {
      uint64_t metal_conversion_identity=0,declaration_hash=0,shader_hash=0;
      uint32_t stream=0,stream_offset=0,stride=0,created_frame=0,last_used_frame=0;
      std::vector<uint8_t> payload;
    };
    uint64_t generation=1;uint32_t handle=1;
    std::vector<uint8_t> payload;
    mutable std::vector<ConvertedVertexPayload> converted_vertex_payloads;
  };
  struct NativePipelineState {
    const Declaration* vertex_declaration_resource=nullptr;
    const Shader* vertex_shader_resource=nullptr;
    struct Stream {uint32_t stride=0,offset=0;};
    std::array<Stream,16> vertex_streams{};
  };
  static constexpr uint32_t kVertexStreamCount=16;
  uint32_t active_texture_frame_=1;
  const NativeBufferResource::ConvertedVertexPayload* PrepareConvertedVertexPayload(
      const NativeBufferResource*,const NativePipelineState&,uint32_t,
      const NativeMetalVertexConversion* =nullptr);
};
#include "metal-vertex-cache.inc"
using Resource=Gta4NativeGraphicsSystem::NativeBufferResource;
using State=Gta4NativeGraphicsSystem::NativePipelineState;
static auto Plan(const Declaration& d,const Shader& s,uint32_t stream,uint32_t stride) {
  return BuildNativeMetalVertexConversionPlan(d,s,stream,stride,
      GetVertexElement16BitComponentCount,ConvertVertexUsageToLocation);
}
static void Differential() {
  std::mt19937 rng(12348);Gta4NativeGraphicsSystem system;
  constexpr std::array<uint32_t,9> types{0x2C2359,0x1A235A,0x2C235F,0x1A2360,
      0x1A2187,0x182886,0x2C83A4,0xDEADBEEF,0x2C2059};
  std::map<NativeMetalVertexConversionPlan,NativeMetalVertexConversion> recipes;
  uint64_t sequence=0;
  for(size_t trial=0;trial<5000;++trial) {
    Resource resource;resource.payload.resize(129+rng()%1500);
    for(auto& byte:resource.payload)byte=uint8_t(rng());
    Declaration declaration;Shader shader;const uint32_t stride=1+rng()%64;
    for(size_t i=0,n=rng()%12;i<n;++i) {
      declaration.elements.push_back({uint32_t(rng()%3),uint32_t(rng()%(stride+8)),
          types[rng()%types.size()],uint8_t(rng()%4),uint8_t(rng()%4)});
      const auto& e=declaration.elements.back();
      if(rng()%3)shader.vertex_inputs.push_back({ConvertVertexUsageToLocation(e.usage,e.usage_index),Numeric(rng()%3)});
    }
    for(uint32_t stream=0;stream<3;++stream) {
      auto plan=Plan(declaration,shader,stream,stride);assert(plan);
      auto [entry,inserted]=recipes.try_emplace(*plan,NativeMetalVertexConversion{*plan,++sequence});
      const uint32_t offset=rng()%resource.payload.size();
      State state{&declaration,&shader};state.vertex_streams[stream]={stride,offset};
      std::vector<uint8_t> expected(resource.payload.size());
      ConvertGuestVertexPayload(expected.data(),resource.payload.data(),expected.size(),
          declaration,shader,stream,offset,stride);
      const auto* actual=system.PrepareConvertedVertexPayload(&resource,state,stream,&entry->second);
      assert(actual&&actual->payload.size()==expected.size());
      assert(std::equal(expected.begin()+offset,expected.end(),actual->payload.begin()+offset));
    }
  }
  // Preserve unsupported, out-of-record and unaligned offset cases.
  Declaration crossing{{{0,2,0x1A235A,0,0}}};Shader shader{{{0,Numeric::kFloat}}};
  auto crossing_plan=Plan(crossing,shader,0,4);assert(crossing_plan&&!crossing_plan->whole_records);
  assert(crossing_plan->Offset(38)==38);
  assert(!Plan(crossing,shader,0,0));
  Declaration overflow;overflow.elements.resize(33,VertexElement{0,0,0x2C2359,0,0});
  assert(!Plan(overflow,shader,0,16));
  // Byte writes retain declaration order even when their fields overlap.
  Declaration overlapping{{{0,0,0x2C2359,0,0},{0,0,0x182886,3,0}}};
  Shader both{{{0,Numeric::kFloat},{4,Numeric::kUnsignedInteger}}};
  assert(Plan(overlapping,both,0,16)->whole_records);
  // Float/signed color inputs use only the endian copy; uint requires swizzle.
  Declaration color{{{0,0,0x182886,0,0}}};
  Shader uint_color{{{0,Numeric::kUnsignedInteger}}};
  assert(Plan(color,shader,0,16)!=Plan(color,uint_color,0,16));
  Resource resource;resource.payload.assign(2048,0xA4);State state{&color,&shader};
  state.vertex_streams[0]={16,16};NativeMetalVertexConversion recipe{*Plan(color,shader,0,16),100000};
  const auto* first=system.PrepareConvertedVertexPayload(&resource,state,0,&recipe);
  assert(first);std::vector<uint8_t> first_bytes=first->payload;
  shader.hash=999;state.vertex_streams[0]={32,128};
  assert(Plan(color,shader,0,32)==recipe.plan);
  const auto before=creates;const auto* reused=system.PrepareConvertedVertexPayload(&resource,state,0,&recipe);
  assert(reused==first&&creates==before&&reused->payload==first_bytes);
  // Source owners and legacy variants remain independent; two-entry eviction
  // cannot return a different recipe or turn a stale borrowed entry into a hit.
  for(uint64_t identity:{100001,100002,100000}) {
    auto other=recipe;other.identity=identity;
    const auto* result=system.PrepareConvertedVertexPayload(&resource,state,0,&other);
    assert(result&&result->metal_conversion_identity==identity&&result->payload==first_bytes);
  }
  const auto* legacy=system.PrepareConvertedVertexPayload(&resource,state,0);
  assert(legacy&&legacy->metal_conversion_identity==0);
  State invalid=state;invalid.vertex_streams[0].offset=resource.payload.size();
  assert(!system.PrepareConvertedVertexPayload(&resource,invalid,0,&recipe));
  std::cout<<"live converter/cache differential: passed (5000 randomized layouts, 15000 suffix comparisons)\n";
}
static void Benchmark() {
  Declaration declaration{{{0,0,0x2C2359,0,0},{0,4,0x1A2187,3,0}}};
  std::array<Shader,4> shaders;
  for(size_t i=0;i<shaders.size();++i)shaders[i]={{{0,Numeric::kFloat},{4,Numeric::kFloat}},i+1};
  NativeMetalVertexConversion recipe{*Plan(declaration,shaders[0],0,32),1000001};
  constexpr size_t draws=1600,size=128*1024;
  const auto run=[&](bool shared) {
    Resource resource;resource.payload.resize(size);for(size_t i=0;i<size;++i)resource.payload[i]=uint8_t(i);
    Gta4NativeGraphicsSystem system;State state;state.vertex_declaration_resource=&declaration;
    std::map<std::array<uint64_t,4>,size_t> uploads;
    std::vector<uint8_t> expected(size);uint64_t check=0;const auto start_creates=creates;
    auto start=std::chrono::steady_clock::now();
    for(size_t i=0;i<draws;++i) {
      state.vertex_shader_resource=&shaders[i%shaders.size()];state.vertex_streams[0]={32,uint32_t(i%8)*32};
      const auto* converted=system.PrepareConvertedVertexPayload(&resource,state,0,shared?&recipe:nullptr);
      assert(converted);check+=converted->payload[state.vertex_streams[0].offset];
      const auto key=shared?std::array<uint64_t,4>{6,resource.generation,recipe.identity,recipe.plan.Offset(state.vertex_streams[0].offset)}:
          std::array<uint64_t,4>{1,resource.generation,state.vertex_shader_resource->hash,state.vertex_streams[0].offset};
      uploads.try_emplace(key,size);
    }
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    const auto conversions=creates-start_creates;
    std::cout<<(shared?"semantic":"legacy")<<": draws="<<draws<<" conversions="<<conversions
        <<" upload-owners="<<uploads.size()<<" uploaded-bytes="<<uploads.size()*size<<" ms="<<ms<<" check="<<check<<"\n";
    assert(shared?conversions==1:conversions==draws);
    return check;
  };
  assert(run(false)==run(true));
  // Model BOTH production cache layers, including frontend hits that never
  // call the CPU converter. The budget is scaled down; this is a cache-pressure
  // fixture, not a claim about the device's current working set or frame time.
  struct Snapshot {std::vector<uint8_t> payload;};
  struct Size {size_t operator()(const Snapshot& value)const{return value.payload.size();}};
  struct Hash {size_t operator()(const std::array<uint64_t,8>& key)const {
    size_t hash=0;for(auto value:key)hash=(hash*1099511628211ull)^value;return hash;
  }};
  const auto pressure=[&](bool shared) {
    std::array<Resource,16> sources;for(size_t n=0;n<sources.size();++n) {
      sources[n].generation=n+1;sources[n].payload.resize(size);
      for(size_t i=0;i<size;++i)sources[n].payload[i]=uint8_t(i+n);
    }
    NativeIncrementalOwnerCache<std::array<uint64_t,8>,Snapshot,Size,Hash> cache;
    Gta4NativeGraphicsSystem system;State state;state.vertex_declaration_resource=&declaration;
    constexpr size_t budget=8*1024*1024,frames=50;
    uint64_t uploaded=0,check=0,misses=0;const auto start_creates=creates;
    auto start=std::chrono::steady_clock::now();
    for(size_t frame=0;frame<frames;++frame) {
      while(cache.bytes()>budget)assert(cache.EvictOldest());
      for(auto& source:sources)for(size_t draw=0;draw<8;++draw) {
        const auto& shader=shaders[draw%shaders.size()];
        const uint32_t offset=uint32_t(draw)*32;
        state.vertex_shader_resource=&shader;state.vertex_streams[0]={32,offset};
        const auto key=shared?std::array<uint64_t,8>{6,source.generation,recipe.identity,recipe.plan.Offset(offset)}:
            std::array<uint64_t,8>{1,source.generation,declaration.content_hash,shader.hash,0,offset,32};
        auto found=cache.find(key);
        if(found==cache.end()) {
          const auto* converted=system.PrepareConvertedVertexPayload(&source,state,0,shared?&recipe:nullptr);
          assert(converted);cache.Store(key,Snapshot{converted->payload});
          found=cache.find(key);uploaded+=converted->payload.size();++misses;
        }else cache.Touch(found);
        check+=found->second.payload[offset];
      }
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<(shared?"semantic":"legacy")<<" two-layer pressure: frames="<<frames
        <<" budget="<<budget<<" conversions="<<creates-start_creates<<" misses="<<misses
        <<" copied-bytes="<<uploaded<<" resident-bytes="<<cache.bytes()<<" ms="<<ms<<" check="<<check<<"\n";
    assert(shared?misses==sources.size():misses>sources.size()*8);
    return check;
  };
  assert(pressure(false)==pressure(true));
}
int main(){Differential();Benchmark();}
