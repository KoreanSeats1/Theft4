// Standalone host audit of the exact embedded SPIR-V corpus. No device needed.
#include "native_constant_projection.h"
#include "shader_cache.h"
#include "smolv.h"
#include <zstd.h>
#include <bit>
#include <cassert>
#include <iostream>
#define SPV_ENABLE_UTILITY_CODE
#include <spirv/unified1/spirv.hpp11>
using namespace rex::graphics::gta4_native;
int main(int argc, char** argv) {
  std::vector<uint8_t> cache(g_spirvCacheDecompressedSize);
  assert(ZSTD_decompress(cache.data(), cache.size(), g_compressedSpirvCache,
      g_spirvCacheCompressedSize) == cache.size());
  size_t recognized = 0;
  for (size_t i = 0; i < g_shaderCacheEntryCount; ++i) {
    const auto& e = g_shaderCacheEntries[i];
    auto decode = [&](uint32_t offset, uint32_t size) {
      assert(offset <= cache.size() && size <= cache.size() - offset);
      size_t bytes = smolv::GetDecodedBufferSize(cache.data() + offset, size);
      assert(bytes && bytes % 4 == 0);
      std::vector<uint32_t> code(bytes / 4);
      assert(smolv::Decode(cache.data() + offset, size, code.data(), bytes));
      if (argc > 1 && std::string(e.filename).find(argv[1]) != std::string::npos) {
        std::cout << e.filename << '\n';
        for (size_t p = 5; p < code.size(); p += code[p] >> 16) {
          std::cout << spv::OpToString(spv::Op(code[p] & 65535));
          for (size_t w = 1; w < (code[p] >> 16); ++w) std::cout << ' ' << code[p + w];
          std::cout << '\n';
        }
      }
      return ReflectNativeConstantUsage(code);
    };
    auto usage = decode(e.spirvOffset, e.spirvSize);
    if (e.lateSpirvSize) usage.Merge(decode(e.lateSpirvOffset, e.lateSpirvSize));
    recognized += usage.known;
    if (usage.known) {
      size_t registers = 0;
      for (auto bank : usage.banks) for (auto word : bank) registers += std::popcount(word);
      std::cout << e.filename << " registers=" << registers << '\n';
    }
  }
  std::cout << "recognized=" << recognized << '/' << g_shaderCacheEntryCount << '\n';
  if (argc == 1) assert(recognized > 0);
}
