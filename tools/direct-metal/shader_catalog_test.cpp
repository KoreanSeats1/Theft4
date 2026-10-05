#include "theft4_metal_shader_catalog.h"
#include "native_masked_constants.h"
#include "shader_cache.h"
#include "smolv.h"
#include "spirv_cross.hpp"
#include <zstd.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace theft4::metal;
void Check(bool truth, const char* reason) { if (!truth) throw std::runtime_error(reason); }
int main(int argc, char** argv) {
  Check(argc == 2, "Usage: metal_shader_catalog_test manifest.tsv");
  std::ifstream input(argv[1]);
  Check(bool(input), "Missing full stock shader manifest");
  std::string manifest((std::istreambuf_iterator<char>(input)), {}), error;
  ShaderCatalog catalog;
  Check(catalog.Parse(manifest, error), error.c_str());
  std::vector<uint8_t> cache(g_spirvCacheDecompressedSize);
  Check(ZSTD_decompress(cache.data(),cache.size(),g_compressedSpirvCache,
      g_spirvCacheCompressedSize)==cache.size(), "Stock shader cache decode failed");
  size_t checked=0, early=0, late=0, clip=0;
  for (size_t i=0;i<g_shaderCacheEntryCount;++i) {
    const auto& entry=g_shaderCacheEntries[i];
    const auto bytes=smolv::GetDecodedBufferSize(cache.data()+entry.spirvOffset,entry.spirvSize);
    std::vector<uint32_t> code(bytes/4);
    Check(smolv::Decode(cache.data()+entry.spirvOffset,entry.spirvSize,code.data(),bytes),"Shader decode failed");
    spirv_cross::Compiler reflected(std::move(code));
    const Stage stage=reflected.get_execution_model()==spv::ExecutionModelVertex ? Stage::Vertex : Stage::Fragment;
    for (bool variant : {false,true}) {
      if (variant && !entry.lateSpirvSize) continue;
      const auto* metadata=catalog.Find({entry.hash,variant},stage);
      Check(metadata,"Stock shader variant missing from runtime catalog");
      Check(metadata->filename==entry.filename && metadata->used_texture_mask==entry.usedTextureMask &&
          metadata->specialization_mask==entry.specConstantsMask,"Runtime shader metadata differs from stock cache");
      Check(!catalog.Find({entry.hash,variant},stage==Stage::Vertex ? Stage::Fragment : Stage::Vertex),
            "Shader lookup admitted the wrong stage");
      Check(metadata->Specialization(UINT32_MAX)==entry.specConstantsMask,"Specialization contains unused bits");
      const auto offset=variant?entry.lateSpirvOffset:entry.spirvOffset;
      const auto size=variant?entry.lateSpirvSize:entry.spirvSize;
      const auto decoded=smolv::GetDecodedBufferSize(cache.data()+offset,size);
      std::vector<uint32_t> module(decoded/4);
      Check(smolv::Decode(cache.data()+offset,size,module.data(),decoded),"Variant constant reflection decode failed");
      const auto usage=rex::graphics::gta4_native::ReflectNativeConstantUsage(module);
      std::array<uint32_t,3> bounds{4096,3584,1056};
      if(usage.known)for(size_t bank=0;bank<2;++bank)bounds[bank]=uint32_t(rex::graphics::gta4_native::NativeMaskedConstantExtent(usage.banks[bank]));
      if(manifest.find("\t0:0:1056")!=std::string::npos)
        Check(metadata->constant_bytes==bounds,"Catalog constant bounds differ from executing stock variant");
      variant ? ++late : ++early; ++checked;
      if (stage==Stage::Vertex) {
        const auto* converted=catalog.Find({entry.hash,false,true},stage);
        Check(converted && converted->filename==metadata->filename &&
            converted->used_texture_mask==metadata->used_texture_mask &&
            converted->specialization_mask==metadata->specialization_mask &&
            converted->inputs==metadata->inputs && converted->bindings==metadata->bindings,
            "Clip conversion changed the stock vertex interface");
        Check(!catalog.Find({entry.hash,false,true},Stage::Fragment),"Vertex clip variant admitted as a fragment shader");
        ++clip;++checked;
      }
    }
  }
  Check(checked==catalog.Size() && early==g_shaderCacheEntryCount,"Unexpected stock catalog entries");
  const auto* sparse=catalog.Find({0xECA7E919FECAAD3Cull,false},Stage::Fragment);
  Check(sparse && sparse->used_texture_mask==0x8001,"Sparse stock shader fixture missing");
  Check(sparse->bindings==std::vector<ShaderFetchBinding>{{FetchKind::Texture2D,0,0},
      {FetchKind::Texture2D,15,1},{FetchKind::Sampler,0,0},{FetchKind::Sampler,15,1}},
      "Sparse game fetch slots 0/15 did not become Metal indices 0/1");
  const std::string row="0000000000000001\tfragment\t32769\t1794\tfixture.bin\t0:13:4,\t0:0:0,0:15:1,4:0:0,4:15:1,\n";
  ShaderCatalog fixture;Check(fixture.Parse(row,error),error.c_str());
  const auto bounded_row=row.substr(0,row.size()-1)+"\t16:32:1056\n";
  Check(fixture.Parse(bounded_row,error),error.c_str());
  Check(fixture.Find({1,false},Stage::Fragment)->constant_bytes==std::array<uint32_t,3>{16,32,1056},"Constant bounds lost");
  for(const auto* bounds:{"17:32:1056","4112:0:1056","0:0:0","0:0:1056:16","-1:0:1056"})
    Check(!fixture.Parse(row.substr(0,row.size()-1)+"\t"+bounds+"\n",error),"Malformed constant bounds admitted");
  const std::string corrupt[]{"", row+row,
    "../shader\tfragment\t0\t0\tfixture.bin\t\t\n",
    "0000000000000001-late\tfragment\t0\t0\tfixture.bin\t\t\n",
    "0000000000000001-clip-neg\tvertex\t0\t0\tfixture.bin\t\t\n",
    "0000000000000001-clip-neg\tfragment\t0\t0\tfixture.bin\t\t\n",
    "0000000000000001-late-clip-neg\tvertex\t0\t0\tfixture.bin\t\t\n",
    "0000000000000001\tfragment\t32769\t1794\tfixture.bin\t\t0:0:1,0:15:0,4:0:0,4:15:1,\n",
    "0000000000000001\tfragment\t32769\t1794\tfixture.bin\t\t0:0:0,0:15:1,4:0:0,\n",
    "0000000000000001\tfragment\t32769\t1794\tfixture.bin\t\t0:0:1,0:15:2,4:0:0,4:15:1,\n",
    "0000000000000001\tvertex\t0\t0\tfixture.bin\t0:13:4,0:13:4,\t\n"};
  for (const auto& bad:corrupt) {
    Check(!fixture.Parse(bad,error),"Malformed catalog was admitted");
    Check(fixture.Size()==1 && fixture.Find({1,false},Stage::Fragment),"Failed parse replaced valid catalog");
  }
  const std::string vertex="0000000000000002\tvertex\t0\t0\tfixture.bin\t0:13:4,\t\n";
  const std::string converted="0000000000000002-clip-neg\tvertex\t0\t0\tfixture.bin\t0:13:4,\t\n";
  Check(fixture.Parse(vertex+converted,error) && fixture.Size()==2,error.c_str());
  Check(fixture.Find({2,false,true},Stage::Vertex) && ShaderKey{2,false,true}.Name()=="0000000000000002-clip-neg",
        "Clip variant identity is not canonical");
  Check(!fixture.Parse(vertex+"0000000000000002-clip-neg\tvertex\t0\t0\tfixture.bin\t0:13:3,\t\n",error) && fixture.Size()==2,
        "Incompatible clip variant replaced the valid catalog");
  std::cout<<"PASS: "<<early<<" stock shaders and "<<late<<" late variants and "<<clip<<" depth-clip variants match the runtime catalog; "
      "sparse binding order, stage checks, specialization, and transactional rejection verified\n";
}
