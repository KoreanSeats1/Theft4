#define XXH_INLINE_ALL
#include <xxhash.h>
#include "native_batch_payload_cache.h"
#include "native_metal_shared_constants.h"
#include "native_shared_frame_arena.h"
#include "native_shader_booleans.h"
#include "native_sampler_lod_bias.h"
#include "native_color_output.h"
#include <rex/graphics/gta4_native/title_commands.h>
#include <rex/graphics/gta4_native/supersampling_policy.h>
#include <algorithm>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <span>
#include <vector>
using namespace rex::graphics::gta4_native;
constexpr size_t kShaderTextureCount=26;
#include "metal-shared-abi.inc"
struct Fixed {
  float alpha_reference=0;
  uint32_t alpha_to_mask=0,user_clip_plane_enable_mask=0;
  std::array<uint32_t,4> clip_plane_bits{};
};
struct Target {
  uint32_t width=2048,height=1536,logical_width=1280,logical_height=720;
  uint32_t samples=1,color_attachment_mask=1;
};
struct ShaderState {uint32_t vertex_booleans=0,pixel_booleans=0;};
struct Command {
  std::shared_ptr<ShaderState> shader_state=std::make_shared<ShaderState>();
  std::shared_ptr<const EnvironmentalDataV1> environmental_data;
  struct Fetch {int32_t lod_bias=0;};
  std::array<Fetch,26> texture_fetches{};
};
struct State {struct Color {uint32_t address=0;};std::array<Color,4> render_targets{};};
struct Caps {float maximum_sampler_lod_bias=16;};
struct Input {Fixed fixed;Target target;Command command;State state;Caps caps;};
static auto Key(const Input& input) {
  const auto& [fixed,target,command,state,caps]=input;
#include "metal-shared-key.inc"
}
static auto Write(const Input& input) {
  const auto& [fixed,target,command,state,caps]=input;
#include "metal-shared-writer.inc"
}
struct Payload {std::vector<uint8_t> value;};
using Memo=NativeMetalSharedConstantMemo<Payload,EnvironmentalDataV1>;
static auto PayloadFor(const Input& input) {
  const auto bytes=Write(input);auto payload=std::make_shared<Payload>();
  const auto* first=reinterpret_cast<const uint8_t*>(&bytes);
  payload->value.assign(first,first+sizeof(bytes));return payload;
}
static void PoolChecks() {
  constexpr size_t budget=2*1024*1024;
  NativeSharedFrameArena<Payload,16> pool(budget,true,4096);
  std::vector<std::shared_ptr<const Payload>> submitted;
  const auto reset=[](Payload& payload){payload.value.clear();};
  for(size_t frame=0;frame<8;++frame) {
    pool.BeginBatch(reset);
    for(size_t n=0;n<600;++n) {
      auto payload=pool.Acquire();payload->value.reserve(4096);
      payload->value.assign(n%2?3584:4096,uint8_t(frame));
      submitted.push_back(payload);
    }
    assert(pool.AllocatedBytes()<=budget);
    for(size_t i=0;i<submitted.size();++i)
      for(auto value:submitted[i]->value)assert(value==uint8_t(i/600));
  }
  assert(pool.fallback_objects>0);
  auto first=submitted.front().get();submitted.clear();pool.BeginBatch(reset);
  auto reused=pool.Acquire();assert(reused.get()==first&&reused->value.capacity()==4096);
  reused->value.resize(512);assert(std::all_of(reused->value.begin(),reused->value.end(),[](auto b){return b==0;}));
  // Zero/overflow budgets fall back safely; an escaped payload outlives pool.
  NativeSharedFrameArena<Payload,16> overflow(size_t(-1),true,size_t(-1));
  auto fallback=overflow.Acquire();assert(overflow.AllocatedBytes()==0&&overflow.fallback_objects==1);
  std::shared_ptr<const Payload> escaped;
  {NativeSharedFrameArena<Payload,16> temporary(budget,true,4096);
   auto payload=temporary.Acquire();payload->value.assign(4096,0xa9);escaped=payload;}
  assert(escaped->value==std::vector<uint8_t>(4096,0xa9));
}
static void PoolBenchmark() {
  auto measure=[](bool pooled) {
    NativeSharedFrameArena<Payload,16> pool(96*1024*1024,false,4096);
    std::vector<std::shared_ptr<const Payload>> retained;retained.reserve(4500);
    std::vector<double> times;uint64_t checksum=0;
    for(size_t frame=0;frame<32;++frame) {
      const auto begin=std::chrono::steady_clock::now();retained.clear();
      if(pooled)pool.BeginBatch([](Payload& payload){payload.value.clear();});
      for(size_t n=0;n<4500;++n) {
        auto payload=pooled?pool.Acquire():std::make_shared<Payload>();
        if(pooled&&payload->value.capacity()<4096)payload->value.reserve(4096);
        payload->value.resize(n%2?3584:4096);payload->value[0]=uint8_t(n);
        checksum+=payload->value[0];retained.push_back(payload);
      }
      if(frame>=4)times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
    }
    std::sort(times.begin(),times.end());return std::pair{times[times.size()/2],checksum};
  };
  std::vector<double> old_ms,new_ms;
  for(size_t trial=0;trial<6;++trial) {
    const auto a=measure(trial%2==0),b=measure(trial%2!=0);assert(a.second==b.second);
    old_ms.push_back(trial%2?a.first:b.first);new_ms.push_back(trial%2?b.first:a.first);
  }
  std::sort(old_ms.begin(),old_ms.end());std::sort(new_ms.begin(),new_ms.end());
  std::cout<<"BENCH immutable constant allocation/zeroing/reset only, 4500 banks: baseline_ms="<<old_ms[3]
      <<" pooled_ms="<<new_ms[3]<<" byte_parity=true (not gameplay FPS)\n";
}
static void Benchmark() {
  using CacheKey=std::array<uint64_t,8>;
  struct Hash {size_t operator()(const CacheKey& k)const{return XXH3_64bits(k.data(),sizeof(k));}};
  auto measure=[](bool memoized) {
    NativeBatchPayloadCache<CacheKey,Payload,Hash> cache;Memo memo;Input input;
    input.command.environmental_data=std::make_shared<EnvironmentalDataV1>();
    size_t writes=0;uint64_t checksum=0;const auto begin=std::chrono::steady_clock::now();
    for(size_t i=0;i<4500;++i) {
      input.fixed.alpha_reference=float((i/16)%8)/8;
      auto payload=memoized?memo.Find(Key(input),input.command.environmental_data):nullptr;
      if(!payload) {
        ++writes;const auto shared=Write(input);
        const auto bytes=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&shared),sizeof(shared));
        const CacheKey key{4,XXH3_64bits(bytes.data(),bytes.size())};
        payload=cache.Find(key);
        if(!payload||payload->value.size()!=bytes.size()||std::memcmp(payload->value.data(),bytes.data(),bytes.size())) {
          auto created=std::make_shared<Payload>();created->value.assign(bytes.begin(),bytes.end());
          payload=created;assert(cache.Store(key,payload));
        }
        if(memoized)memo.Remember(Key(input),input.command.environmental_data,payload);
      }
      checksum+=payload->value[0x246];
    }
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    return std::tuple{ms,checksum,writes};
  };
  std::vector<double> old_ms,new_ms;size_t writes=0;
  for(size_t i=0;i<20;++i) {
    const auto a=measure(i%2==0),b=measure(i%2!=0);
    assert(std::get<1>(a)==std::get<1>(b));
    const auto& old=i%2?a:b;const auto& optimized=i%2?b:a;
    if(i>=4){old_ms.push_back(std::get<0>(old));new_ms.push_back(std::get<0>(optimized));}
    writes=std::get<2>(optimized);
  }
  std::sort(old_ms.begin(),old_ms.end());std::sort(new_ms.begin(),new_ms.end());
  std::cout<<"BENCH shared-bank frontend only, 4500 draws in 16-draw material groups: baseline_ms="<<old_ms[old_ms.size()/2]
      <<" optimized_ms="<<new_ms[new_ms.size()/2]<<" writers=4500->"<<writes<<" byte_parity=true (not gameplay FPS)\n";
}
int main() {
  Memo memo;Input input;std::mt19937 random(119);
  auto original=PayloadFor(input);memo.Remember(Key(input),{},original);
  assert(memo.Find(Key(input),{})==original);
  // Independently perturb each ABI input. The key builder and byte writer
  // come from the actual frontend, so a missing key dependency fails here.
  for(size_t test=0;test<20000;++test) {
    const auto previous_key=Key(input);const auto previous_bytes=PayloadFor(input);
    memo.Remember(previous_key,input.command.environmental_data,previous_bytes);
    switch(test%16) {
      case 0:input.target.width=1+random()%4096;break;
      case 1:input.target.height=1+random()%4096;break;
      case 2:input.target.logical_width=1+random()%4096;break;
      case 3:input.target.logical_height=1+random()%4096;break;
      case 4:input.target.samples=1u<<(random()%3);break;
      case 5:input.target.color_attachment_mask=random()%16;break;
      case 6:input.fixed.alpha_reference=std::bit_cast<float>(uint32_t(random()));break;
      case 7:input.fixed.alpha_to_mask=random();break;
      case 8:input.fixed.user_clip_plane_enable_mask=random();break;
      case 9:input.fixed.clip_plane_bits[random()%4]=random();break;
      case 10:input.state.render_targets[random()%4].address=random();break;
      case 11:input.command.texture_fetches[random()%26].lod_bias=int(random()%1024)-512;break;
      case 12:input.command.shader_state->vertex_booleans=random();break;
      case 13:input.command.shader_state->pixel_booleans=random();break;
      case 14:input.caps.maximum_sampler_lod_bias=float(random()%17);break;
      case 15: {
        auto environment=std::make_shared<EnvironmentalDataV1>();
        environment->valid_fields=random();environment->fog_density=float(random());
        environment->view_inverse_matrix[random()%16]=float(random());
        environment->camera_position[random()%4]=float(random());
        environment->projection_matrix[random()%16]=float(random());
        input.command.environmental_data=test%2?environment:nullptr;break;
      }
    }
    const auto current=PayloadFor(input);auto hit=memo.Find(Key(input),input.command.environmental_data);
    if(hit)assert(hit->value==current->value);
    if(previous_bytes->value!=current->value)assert(!hit);
  }
  auto environment=std::make_shared<EnvironmentalDataV1>();
  auto a=std::make_shared<int>(1),b=std::make_shared<int>(2);
  std::shared_ptr<const EnvironmentalDataV1> first(a,environment.get()),second(b,environment.get());
  memo.Remember(Key(input),first,original);assert(memo.Find(Key(input),first)==original);
  assert(!memo.Find(Key(input),second));first.reset();a.reset();assert(!memo.Find(Key(input),second));
  memo.Remember(Key(input),second,original);auto moved=std::move(memo);
  assert(moved.Find(Key(input),second)==original);moved.Clear();assert(!moved.Find(Key(input),second));
  // Retaining an accepted payload must not retain its environment or change
  // the old bytes when the next frame replaces the memo.
  Memo lifetime;std::weak_ptr<const EnvironmentalDataV1> weak=environment;
  lifetime.Remember(Key(input),environment,original);environment.reset();assert(weak.expired());
  assert(original->value.size()==1056);
  PoolChecks();PoolBenchmark();Benchmark();
  std::cout<<"PASS: 20000 differential shared-bank changes; all key inputs, bit-exact float changes, environment identities, alias owners, reset/move and payload lifetime\n";
}
