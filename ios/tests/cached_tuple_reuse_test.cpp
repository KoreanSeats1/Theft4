#include "native_prepared_bindings.h"
#include <memory>
#include <random>
#include <stdexcept>
#include <iostream>
using namespace rex::graphics::gta4_native;
static void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
struct Pipeline{std::array<uint32_t,16> textures{};};
struct Command{
  std::shared_ptr<Pipeline> pipeline_state=std::make_shared<Pipeline>();
  std::array<std::shared_ptr<int>,16> textures{};
  std::array<std::array<uint32_t,6>,16> texture_fetches{};
  uint32_t used_texture_mask=1,failed_texture_mask=0;
  bool bindings_prepared=false;
};
using Tuple=std::array<uint64_t,80>;
static Tuple Realize(const Command& c){Tuple t{};for(size_t s=0;s<16;++s)if(c.used_texture_mask&(1U<<s)){t[s]=uintptr_t(c.textures[s].get());t[16+s]=c.pipeline_state->textures[s];for(auto w:c.texture_fetches[s])t[32+s]=t[32+s]*131+w;}return t;}
int main(){
  std::array<Command,32> materials;
  std::mt19937 rng(95);
  for(auto& c:materials){c.used_texture_mask=rng()&65535;for(size_t s=0;s<16;++s){c.pipeline_state->textures[s]=rng();if(rng()%3)c.textures[s]=std::make_shared<int>(rng());for(auto& w:c.texture_fetches[s])w=rng();}}
  NativePreparedDescriptorTupleMemo<Command,Tuple> memo;
  std::vector<Command> commands;std::vector<Tuple> tuples;commands.reserve(10000);tuples.reserve(10000);
  size_t hits=0;
  for(size_t i=0;i<10000;++i){auto c=materials[rng()%32];c.pipeline_state=std::make_shared<Pipeline>(*c.pipeline_state);if(i%13==0)c.texture_fetches[0][rng()%6]^=rng();if(i%17==0)c.pipeline_state->textures[0]=0;if(i%23==0)c.textures[0]=std::make_shared<int>(95);if(i%31==0)c.used_texture_mask^=1;
    auto expected=Realize(c);auto* hit=memo.Find(c);if(hit){Check(*hit->tuple==expected,"cached tuple differs from complete realization");++hits;}
    commands.push_back(std::move(c));tuples.push_back(expected);memo.Remember(commands.back(),tuples.back());
    Check(!commands.back().bindings_prepared,"tuple preparation marked GPU ready");
  }
  Check(hits>1000,"no material reuse");
  Command rejected=materials[0];rejected.failed_texture_mask=1;Tuple bad{};memo.Remember(rejected,bad);auto* candidate=memo.Find(materials[0]);Check(!candidate||candidate->command!=&rejected,"failed tuple admitted");
  NativePreparedDescriptorTupleMemo<Command,Tuple> next_batch;Check(!next_batch.Find(materials[0]),"tuple escaped preparation batch");
  Command guest_null;guest_null.pipeline_state->textures[0]=0;auto missing=guest_null;missing.pipeline_state=std::make_shared<Pipeline>(*guest_null.pipeline_state);missing.pipeline_state->textures[0]=100;
  Check(!NativeTextureInputsEqual(guest_null,missing),"guest null confused with missing resource");
  std::cout<<"PASS: 10000 material/generation/fetch/mask/null comparisons against complete realization, reuse_hits="<<hits<<", failed tuples, batch reset, delayed GPU-ready state\n";
}
