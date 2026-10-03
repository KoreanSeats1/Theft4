#include "theft4_metal_shader_catalog.h"
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
  size_t checked=0, early=0, late=0;
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
      variant ? ++late : ++early; ++checked;
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
  const std::string corrupt[]{"", row+row,
    "../shader\tfragment\t0\t0\tfixture.bin\t\t\n",
    "0000000000000001-late\tfragment\t0\t0\tfixture.bin\t\t\n",
    "0000000000000001\tfragment\t32769\t1794\tfixture.bin\t\t0:0:1,0:15:0,4:0:0,4:15:1,\n",
    "0000000000000001\tfragment\t32769\t1794\tfixture.bin\t\t0:0:0,0:15:1,4:0:0,\n",
    "0000000000000001\tfragment\t32769\t1794\tfixture.bin\t\t0:0:1,0:15:2,4:0:0,4:15:1,\n",
    "0000000000000001\tvertex\t0\t0\tfixture.bin\t0:13:4,0:13:4,\t\n"};
  for (const auto& bad:corrupt) {
    Check(!fixture.Parse(bad,error),"Malformed catalog was admitted");
    Check(fixture.Size()==1 && fixture.Find({1,false},Stage::Fragment),"Failed parse replaced valid catalog");
  }
  std::cout<<"PASS: "<<early<<" stock shaders and "<<late<<" late variants match the runtime catalog; "
      "sparse binding order, stage checks, specialization, and transactional rejection verified\n";
}
