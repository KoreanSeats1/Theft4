#include "shader_cache.h"
#include "smolv.h"
#include "spirv_cross.hpp"
#include <zstd.h>
#include <bit>
#include <map>
#include <set>
#include <stdexcept>
#include <iostream>
#include <vector>
std::vector<uint32_t> LowerMetalDescriptorSlots(std::vector<uint32_t>,
    const std::map<uint32_t,uint32_t>&,uint32_t);
using Words=std::vector<uint32_t>;
void Check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::vector<Words> Instructions(const Words& module){
  std::vector<Words> result;
  for(size_t p=5;p<module.size();){size_t n=module[p]>>16;Check(n&&n<=module.size()-p,"invalid module");
    result.emplace_back(module.begin()+p,module.begin()+p+n);p+=n;}return result;
}
int main(){
  std::vector<uint8_t> cache(g_spirvCacheDecompressedSize);
  Check(ZSTD_decompress(cache.data(),cache.size(),g_compressedSpirvCache,g_spirvCacheCompressedSize)==cache.size(),"cache decode");
  size_t tests=0;
  for(size_t i=0;i<g_shaderCacheEntryCount;++i){
    const auto& e=g_shaderCacheEntries[i];
    if(e.hash!=0xB9589DA9F4B1770FULL&&e.hash!=0xECA7E919FECAAD3CULL&&e.hash!=0x2668E8F9BB250542ULL)continue;
    size_t size=smolv::GetDecodedBufferSize(cache.data()+e.spirvOffset,e.spirvSize);
    Words before(size/4);Check(smolv::Decode(cache.data()+e.spirvOffset,e.spirvSize,before.data(),size),"module decode");
    spirv_cross::Compiler compiler(before);auto resources=compiler.get_shader_resources();
    std::map<uint32_t,uint32_t> kinds;
    for(auto& r:resources.separate_images)kinds[r.id]=compiler.get_decoration(r.id,spv::DecorationDescriptorSet);
    for(auto& r:resources.separate_samplers)kinds[r.id]=compiler.get_decoration(r.id,spv::DecorationDescriptorSet);
    auto after=LowerMetalDescriptorSlots(before,kinds,e.usedTextureMask);
    auto original=Instructions(before),lowered=Instructions(after);
    std::map<uint32_t,Words> constants;std::vector<Words> retained;
    for(auto& w:lowered){
      if((w[0]&65535)==43)constants[w[2]]=w;
      if((w[0]&65535)==43&&w[2]>=before[3])continue;
      retained.push_back(w);
    }
    Check(retained.size()==original.size(),"unrelated instructions added or removed");
    std::set<uint32_t> ranks;size_t accesses=0;
    for(size_t j=0;j<original.size();++j){
      const auto& a=original[j];const auto& b=retained[j];
      const uint32_t op=a[0]&65535;
      if((op==65||op==66)&&a.size()==5&&kinds.contains(a[3])){
        Check(b.size()==5&&std::equal(a.begin(),a.begin()+4,b.begin()),"descriptor access shape changed");
        Check(constants.contains(b[4]),"descriptor index did not become a constant");
        auto& value=constants.at(b[4]);Check(value.size()>=4,"invalid index constant");
        Check(value[3]<std::popcount(e.usedTextureMask),"descriptor index exceeds compact table");
        ranks.insert(value[3]);++accesses;
      }else Check(a==b,"a non-descriptor shader operation changed");
    }
    if(e.usedTextureMask){
      Check(accesses&&ranks.size()==std::popcount(e.usedTextureMask),"missing sparse fetch slot");
      bool rejected=false;
      try{LowerMetalDescriptorSlots(before,kinds,e.usedTextureMask&(e.usedTextureMask-1));}
      catch(const std::exception&){rejected=true;}Check(rejected,"unreflected fetch slot was admitted");
    }else Check(after==before,"constant-only shader was rewritten");
    Words malformed=before;malformed[5]=0;bool rejected=false;
    try{LowerMetalDescriptorSlots(malformed,kinds,e.usedTextureMask);}
    catch(const std::exception&){rejected=true;}Check(rejected,"malformed instruction was admitted");
    ++tests;
  }
  Check(tests==3,"real shader fixtures missing");
  std::cout<<"PASS: real single-slot, sparse slots 0/15, and constant-only shaders; only descriptor operands change; unknown fetches and malformed modules reject\n";
}
