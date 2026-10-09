#define THEFT4_DIRECT_METAL_BACKEND 1
#define REXLOG_INFO(...) ((void)0)
#define REXLOG_WARN(...) ((void)0)
#define XXH_INLINE_ALL
#include <xxhash.h>
#include "native_metal_vertex_conversion.h"
#include "native_frame_scheduling.h"
#include "native_incremental_owner_cache.h"
#include "native_shared_geometry_payload.h"
#include "native_frame_resource_owners.h"
#include "theft4_render_plan.h"
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
namespace xenos {constexpr uint32_t kVertexIndexMask=0xFFFFFF;}
static uint64_t NextNativeTraceDiagnosticCount(std::atomic<uint64_t>& value){return ++value;}
namespace profile {
enum class CpuOp {kIndexConvert};
template<class F> decltype(auto) CpuCall(CpuOp,F&& fn){return fn();}
}
#include "metal-vertex-converter.inc"
namespace memory {
enum class ResourceKind {kVertexConversion,kIndexConversion};
enum class LifecycleAction {kCreate,kDestroy};
enum class LifecycleReason {kCacheMiss,kSuperseded};
}
static uint64_t creates=0;
template<class... Args> void RecordNativeMemoryLifecycle(memory::ResourceKind,
    memory::LifecycleAction action,Args&&...) {if(action==memory::LifecycleAction::kCreate)++creates;}
struct Gta4NativeGraphicsSystem {
  struct NativeBufferResource {
    struct ConvertedVertexPayload {
      uint64_t metal_conversion_identity=0,declaration_hash=0,shader_hash=0;
      uint32_t stream=0,stream_offset=0,stride=0,created_frame=0,last_used_frame=0;
      std::vector<uint8_t> payload;
      std::shared_ptr<const theft4::render::Bytes> metal_owner;
      const std::vector<uint8_t>& Data() const;
    };
    uint64_t generation=1;uint32_t handle=1;
    std::vector<uint8_t> payload;
    mutable std::vector<ConvertedVertexPayload> converted_vertex_payloads;
    mutable std::vector<uint8_t> host_index16_payload,host_index32_payload;
    mutable std::array<std::shared_ptr<const theft4::render::Bytes>,2> metal_index_owners{};
    const std::vector<uint8_t>& IndexPayload(bool) const;
  };
  struct NativePipelineState {
    const Declaration* vertex_declaration_resource=nullptr;
    const Shader* vertex_shader_resource=nullptr;
    struct Stream {uint32_t stride=0,offset=0;};
    std::array<Stream,16> vertex_streams{};
  };
  static constexpr uint32_t kVertexStreamCount=16;
  uint32_t active_texture_frame_=1;
  uint64_t next_native_metal_allocation_=0,next_native_metal_sequence_=0;
  const NativeBufferResource::ConvertedVertexPayload* PrepareConvertedVertexPayload(
      const NativeBufferResource*,const NativePipelineState&,uint32_t,
      const NativeMetalVertexConversion* =nullptr,bool=false);
  const NativeBufferResource::ConvertedVertexPayload* FindConvertedVertexPayload(
      const NativeBufferResource*,const NativePipelineState&,uint32_t,const NativeMetalVertexConversion*);
  const std::vector<uint8_t>& PrepareConvertedIndexPayload(const NativeBufferResource*,bool);
  const std::shared_ptr<const theft4::render::Bytes>& PrepareNativeMetalVertexPayload(
      NativeResourceView<NativeBufferResource>,const NativePipelineState&,uint32_t,const NativeMetalVertexConversion*);
  const std::shared_ptr<const theft4::render::Bytes>& PrepareNativeMetalIndexPayload(const NativeBufferResource*,bool);
  struct BytesRecord {std::weak_ptr<const void> resource;std::shared_ptr<const theft4::render::Bytes> owner;uint64_t used=0;};
  struct Size {size_t operator()(const BytesRecord& r)const{return r.owner->value.size();}};
  struct Hash {size_t operator()(const std::array<uint64_t,8>& key)const {
    return size_t(XXH3_64bits(key.data(),sizeof(key)));
  }};
  struct FrameState {NativeIncrementalOwnerCache<std::array<uint64_t,8>,BytesRecord,Size,Hash> buffers;};
  std::unique_ptr<FrameState> native_metal_frame_=std::make_unique<FrameState>();
};
#include "metal-vertex-cache.inc"
#include "metal-geometry-publish.inc"
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
static void RecipeDifferential() {
  std::mt19937 rng(917381);
  constexpr std::array<uint32_t,13> types{0x2C2359,0x1A235A,0x2C235F,0x1A2360,
      0x1A2187,0x182886,0x2C83A4,0x2C23A5,0x2A23B9,0x1A23A6,0xDEADBEEF,0x2C2059,0x1A215A};
  uint64_t tiled=0,fallback=0;
  for(size_t trial=0;trial<40000;++trial) {
    const size_t size=trial<100?trial:128+rng()%20000;
    std::vector<uint8_t> source(size),reference(size),actual(size);
    for(auto& byte:source)byte=uint8_t(rng());
    Declaration declaration;Shader shader;const uint32_t stride=1+rng()%128;
    for(size_t n=0,end=rng()%20;n<end;++n) {
      declaration.elements.push_back({uint32_t(rng()%3),uint32_t(rng()%(stride+8)),
          types[rng()%types.size()],uint8_t(rng()%4),uint8_t(rng()%4)});
      const auto& e=declaration.elements.back();
      if(rng()%3)shader.vertex_inputs.push_back({ConvertVertexUsageToLocation(e.usage,e.usage_index),Numeric(rng()%3)});
    }
    for(uint32_t stream=0;stream<3;++stream) {
      const auto plan=Plan(declaration,shader,stream,stride);assert(plan);
      const uint32_t offset=size?rng()%size:0;
      const uint32_t canonical=plan->Offset(offset);
      const auto expected=ConvertGuestVertexPayload(reference.data(),source.data(),size,declaration,shader,stream,canonical,stride);
      const auto result=ConvertNativeMetalVertexPayload(actual.data(),source.data(),size,*plan,canonical);
      assert(reference==actual);
      assert(result.components_16==expected.components_16&&result.dec3n==expected.dec3n&&result.color_uint==expected.color_uint);
      (plan->whole_records&&!(plan->stride%4)&&!(canonical%4)?tiled:fallback)++;
    }
  }
  std::cout<<"compiled recipe differential: passed (120000 full-byte/count comparisons, tiled="<<tiled<<" fallback="<<fallback<<")\n";
}
static void RecipeBenchmark() {
  // Streaming burst, including float-only attributes and common packed fields.
  for(bool packed:{false,true}) {
    Declaration declaration{{{0,0,0x2A23B9,0,0},{0,12,0x2C23A5,3,0},
        {0,20,packed?0x1A2187u:0x2C83A4u,3,1},{0,24,packed?0x2C235Fu:0x2C23A5u,3,2}}};
    Shader shader{{{0,Numeric::kFloat},{4,Numeric::kFloat},{5,Numeric::kFloat},{6,Numeric::kFloat}}};
    auto plan=Plan(declaration,shader,0,32);assert(plan);
    constexpr size_t bytes=512*1024,sources=128;
    std::vector<uint8_t> source(bytes),output(bytes);for(size_t i=0;i<bytes;++i)source[i]=uint8_t(i*13);
    std::array<std::vector<double>,2> times;uint64_t checksum=0;
    for(size_t trial=0;trial<8;++trial)for(size_t order=0;order<2;++order) {
      const size_t mode=(trial+order)%2;
      const auto began=std::chrono::steady_clock::now();
      for(size_t mesh=0;mesh<sources;++mesh) {
        if(mode)ConvertNativeMetalVertexPayload(output.data(),source.data(),bytes,*plan,0);
        else ConvertGuestVertexPayload(output.data(),source.data(),bytes,declaration,shader,0,0,32);
        checksum+=output[mesh*32];
      }
      times[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count());
    }
    for(size_t mode=0;mode<2;++mode) {
      auto& t=times[mode];std::sort(t.begin(),t.end());
      std::cout<<"recipe benchmark: packed="<<packed<<" mode="<<(mode?"compiled":"legacy")
          <<" meshes="<<sources<<" total-bytes="<<bytes*sources<<" median-ms="<<t[t.size()/2]<<" checksum="<<checksum<<"\n";
    }
  }
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
static void SharedOwnership() {
  Gta4NativeGraphicsSystem system;
  auto source=std::make_shared<Resource>();source->payload.resize(4096);
  for(size_t i=0;i<source->payload.size();++i)source->payload[i]=uint8_t(i);
  Declaration declaration{{{0,0,0x2C2359,0,0}}};Shader shader{{{0,Numeric::kFloat}}};
  NativeMetalVertexConversion recipe{*Plan(declaration,shader,0,32),100000};
  State state{&declaration,&shader};state.vertex_streams[0]={32,64};
  const auto* before=system.PrepareConvertedVertexPayload(source.get(),state,0,&recipe);
  const auto* allocation=before->Data().data();const auto expected=before->Data();
  const auto* after=system.PrepareConvertedVertexPayload(source.get(),state,0,&recipe,true);
  assert(after==before&&after->payload.capacity()==0&&after->metal_owner);
  assert(after->Data().data()==allocation&&after->Data()==expected);
  auto escaped=after->metal_owner;
  const auto generations=system.next_native_metal_allocation_;
  for(size_t i=0;i<100;++i) {
    const auto& owner=system.PrepareNativeMetalVertexPayload(NativeResourceView<Resource>(source),state,0,&recipe);
    assert(owner==escaped&&system.next_native_metal_allocation_==generations);
  }
  std::array<Declaration,3> declarations;
  std::array<NativeMetalVertexConversion,3> recipes;
  std::array<std::shared_ptr<const theft4::render::Bytes>,3> saved{};
  source=std::make_shared<Resource>();source->payload.resize(4096);
  for(size_t i=0;i<source->payload.size();++i)source->payload[i]=uint8_t(i*3);
  source->generation=2;
  for(size_t i=0;i<3;++i) {
    declarations[i].elements={{0,uint32_t(i)*4,0x2C2359,0,0}};
    recipes[i]={*Plan(declarations[i],shader,0,32),200000+i};
  }
  const auto start_creates=creates;
  for(size_t draw=0;draw<300;++draw) {
    const auto i=draw%3;state.vertex_declaration_resource=&declarations[i];
    const auto& owner=system.PrepareNativeMetalVertexPayload(NativeResourceView<Resource>(source),state,0,&recipes[i]);
    assert(owner);if(saved[i])assert(owner==saved[i]);else saved[i]=owner;
    std::vector<uint8_t> reference(source->payload.size());
    ConvertGuestVertexPayload(reference.data(),source->payload.data(),reference.size(),declarations[i],shader,0,64,32);
    assert(std::equal(reference.begin()+64,reference.end(),owner->value.begin()+64));
  }
  assert(creates-start_creates==3&&system.native_metal_frame_->buffers.size()==3);
  std::weak_ptr<const Resource> weak_source=source;
  system.native_metal_frame_->buffers.clear();source.reset();assert(weak_source.expired());
  assert(escaped->value==expected); // Escaped draws survive source/cache destruction.
  for(const auto& owner:saved)assert(owner&&owner->value.size()==4096);
  Resource indices;indices.payload={0x12,0x34,0xFF,0xFF,0xAB,0xCD,0x00,0x01,0xCC};
  for(bool index32:{false,true}) {
    std::vector<uint8_t> reference(indices.payload.size());
    CopyGuestIndicesToHost(reference.data(),indices.payload.data(),reference.size(),index32);
    const auto& converted=system.PrepareConvertedIndexPayload(&indices,index32);
    const auto* data=converted.data();
    auto owner=system.PrepareNativeMetalIndexPayload(&indices,index32);
    assert(owner&&owner->value==reference&&owner->value.data()==data);
    assert(indices.IndexPayload(index32).data()==data);
    assert((index32?indices.host_index32_payload:indices.host_index16_payload).capacity()==0);
    assert(system.PrepareNativeMetalIndexPayload(&indices,index32)==owner);
    // Legacy upload can borrow the published allocation without re-converting.
    assert(system.PrepareConvertedIndexPayload(&indices,index32).data()==data);
  }
  std::vector<uint8_t> invalid{1,2,3};const auto* original=invalid.data();
  assert(!PublishNativeGeometryPayload<theft4::render::Bytes>(invalid,0,{}));
  assert(invalid.data()==original&&invalid.size()==3);
  std::cout<<"shared geometry ownership: passed (allocation identity, three-recipe cycling, eviction, escaped draws, index widths)\n";
}
static void GeometryPublicationBenchmark(bool reverse=false) {
  Declaration declaration{{{0,0,0x2C2359,0,0},{0,4,0x1A2187,3,0}}};
  Shader shader{{{0,Numeric::kFloat},{4,Numeric::kFloat}}};
  NativeMetalVertexConversion recipe{*Plan(declaration,shader,0,32),1000001};
  constexpr size_t sources=256,bytes=128*1024,draws=4500,frames=24;
  const auto run=[&](bool shared) {
    Gta4NativeGraphicsSystem system;State state{&declaration,&shader};state.vertex_streams[0]={32,64};
    std::array<std::shared_ptr<Resource>,sources> resources;
    for(size_t n=0;n<sources;++n) {
      resources[n]=std::make_shared<Resource>();resources[n]->generation=n+1;
      resources[n]->payload.resize(bytes);
      for(size_t i=0;i<bytes;++i)resources[n]->payload[i]=uint8_t(i+n);
    }
    uint64_t copied=0,checksum=0;std::vector<double> times;
    for(size_t frame=0;frame<frames;++frame) {
      auto start=std::chrono::steady_clock::now();
      for(size_t draw=0;draw<draws;++draw) {
        const auto& source=resources[(draw*13)%sources];
        std::shared_ptr<const theft4::render::Bytes> owner;
        if(shared)owner=system.PrepareNativeMetalVertexPayload(NativeResourceView<Resource>(source),state,0,&recipe);
        else {
          // Build 120's live frontend hit/miss path, using its production
          // XXH3 key hash, weak expiry check, LRU and full packet copy.
          const std::array<uint64_t,8> key{6,source->generation,recipe.identity,recipe.plan.Offset(64)};
          auto& cache=system.native_metal_frame_->buffers;auto found=cache.find(key);
          if(found!=cache.end()&&!found->second.resource.expired()) {
            owner=found->second.owner;found->second.used=++system.next_native_metal_sequence_;cache.Touch(found);
          }else {
            const auto* converted=system.PrepareConvertedVertexPayload(source.get(),state,0,&recipe);
            auto packet=std::make_shared<theft4::render::Bytes>();packet->generation=++system.next_native_metal_allocation_;
            packet->conversion={recipe.identity,recipe.plan.Offset(64)};packet->value=converted->Data();
            copied+=packet->value.size();owner=packet;cache.Store(key,{source,owner,++system.next_native_metal_sequence_});
          }
        }
        assert(owner);checksum+=owner->value[64];
      }
      times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }
    uint64_t duplicate=0;
    for(const auto& source:resources)for(const auto& conversion:source->converted_vertex_payloads) {
      const std::array<uint64_t,8> key{6,source->generation,recipe.identity,recipe.plan.Offset(64)};
      const auto found=system.native_metal_frame_->buffers.find(key);assert(found!=system.native_metal_frame_->buffers.end());
      if(conversion.Data().data()!=found->second.owner->value.data())duplicate+=conversion.Data().capacity();
    }
    auto warm=times;warm.erase(warm.begin());std::sort(warm.begin(),warm.end());
    std::cout<<(shared?"shared":"build120")<<" publication: sources="<<sources<<" bytes-per-source="<<bytes
        <<" cold-ms="<<times[0]<<" warm-median-ms="<<warm[warm.size()/2]<<" copied-bytes="<<copied
        <<" duplicate-retained-bytes="<<duplicate<<" checksum="<<checksum<<"\n";
    assert(shared?copied==0&&duplicate==0:copied==sources*bytes&&duplicate==sources*bytes);
    return checksum;
  };
  const auto first=run(reverse);const auto second=run(!reverse);assert(first==second);
}
int main(int argc,char** argv){Differential();RecipeDifferential();RecipeBenchmark();SharedOwnership();Benchmark();
  GeometryPublicationBenchmark(argc>1&&std::strcmp(argv[1],"--reverse-publication")==0);}
