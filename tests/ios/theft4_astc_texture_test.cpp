#include "../../ios/bridge/theft4_astc_texture.h"
#include <astcenc.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() {
  namespace fs = std::filesystem;
  using namespace theft4::astc;
  const fs::path root = fs::temp_directory_path() / "theft4-astc-texture-test";
  fs::remove_all(root);
  std::string error;

  // An opaque red BC1 block, followed by a second smaller mip of the same color.
  const std::array<uint8_t, 16> bc1{
      0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0,
      0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0};
  const std::array<Mip, 2> mips{
      Mip{0, 4, 4, 1, 0, 1, 4, 4, 0, 8},
      Mip{1, 2, 2, 1, 0, 1, 4, 4, 8, 8}};
  const Input input{BcFormat::kBc1, 0x0123456789ABCDEFull, 4, 4, bc1, mips};
  Prepared decoded;
  Check(DecodeToRgba8(input, decoded, &error), "BC1 decoding failed");
  Check(decoded.payload.size() == 80 && decoded.mips.size() == 2,
        "BC1 mip layout is incorrect");
  for (size_t i = 0; i < decoded.payload.size(); i += 4) {
    Check(decoded.payload[i] == 255 && decoded.payload[i + 1] == 0 &&
              decoded.payload[i + 2] == 0 && decoded.payload[i + 3] == 255,
          "BC1 color decode is incorrect");
  }
  Prepared encoded;
  Check(PrepareAstc4x4(input, root, encoded, &error), "ASTC conversion failed");
  Check(!encoded.cache_hit && encoded.cache_persisted && encoded.payload.size() == 32 &&
            encoded.mips.size() == 2, "ASTC output layout is incorrect");
  astcenc_config decode_config{};
  Check(astcenc_config_init(ASTCENC_PRF_LDR, 4, 4, 1, ASTCENC_PRE_FAST,
                           ASTCENC_FLG_DECOMPRESS_ONLY, &decode_config) == ASTCENC_SUCCESS,
        "ASTC decoder configuration failed");
  astcenc_context* decode_context = nullptr;
  Check(astcenc_context_alloc(&decode_config, 1, &decode_context) == ASTCENC_SUCCESS,
        "ASTC decoder allocation failed");
  std::array<uint8_t, 64> astc_pixels{};
  void* astc_slice = astc_pixels.data();
  astcenc_image astc_image{4, 4, 1, ASTCENC_TYPE_U8, &astc_slice};
  const astcenc_swizzle astc_swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G,
                                     ASTCENC_SWZ_B, ASTCENC_SWZ_A};
  Check(astcenc_decompress_image(decode_context, encoded.payload.data(),
                                encoded.mips[0].payload_size, &astc_image,
                                &astc_swizzle, 0) == ASTCENC_SUCCESS,
        "ASTC output could not be decompressed");
  astcenc_context_free(decode_context);
  Check(astc_pixels[0] >= 240 && astc_pixels[1] <= 16 &&
            astc_pixels[2] <= 16 && astc_pixels[3] >= 240,
        "ASTC output did not preserve the red test image");
  Prepared cached;
  Check(PrepareAstc4x4(input, root, cached, &error), "ASTC cache read failed");
  Check(cached.cache_hit && cached.cache_persisted && cached.payload == encoded.payload,
        "ASTC cache returned different data");

  // BC2 stores four-bit alpha values separately from its four-color block.
  const std::array<uint8_t, 16> bc2{
      0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0,
      0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0};
  const std::array<Mip, 1> one_mip{Mip{0, 4, 4, 1, 0, 1, 4, 4, 0, 16}};
  Prepared bc2_decoded;
  Check(DecodeToRgba8({BcFormat::kBc2, 2, 4, 4, bc2, one_mip},
                      bc2_decoded, &error), "BC2 decoding failed");
  Check(bc2_decoded.payload[3] == 255 && bc2_decoded.payload[7] == 0,
        "BC2 alpha decode is incorrect");

  // BC3 selector 1 selects its second (zero) alpha endpoint.
  const std::array<uint8_t, 16> bc3{
      255, 0, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24,
      0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0};
  Prepared bc3_decoded;
  Check(DecodeToRgba8({BcFormat::kBc3, 3, 4, 4, bc3, one_mip},
                      bc3_decoded, &error), "BC3 decoding failed");
  Check(bc3_decoded.payload[3] == 0 && bc3_decoded.payload[7] == 0,
        "BC3 alpha decode is incorrect");

  const std::array<uint8_t, 16> bc1_volume{
      0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0,
      0x1F, 0x00, 0x00, 0x00, 0, 0, 0, 0};
  const std::array<Mip, 1> volume_mip{Mip{0, 4, 4, 2, 0, 1, 4, 4, 0, 16}};
  const Input volume{BcFormat::kBc1, 4, 4, 4, bc1_volume, volume_mip};
  Prepared volume_decoded;
  Check(DecodeToRgba8(volume, volume_decoded, &error) &&
            volume_decoded.payload.size() == 128 &&
            volume_decoded.payload[0] == 255 &&
            volume_decoded.payload[64 + 2] == 255,
        "3D BC fallback did not preserve both slices");
  Prepared volume_astc;
  Check(!PrepareAstc4x4(volume, root, volume_astc, &error),
        "3D BC texture was incorrectly sent to the 2D ASTC encoder");

  const auto cache_path = root / "astc-v1" /
      "0123456789abcdef-1-4x4-2.bin";
  Check(fs::exists(cache_path), "ASTC cache file was not saved");
  {
    std::fstream file(cache_path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekg(-1, std::ios::end);
    const char last = char(file.get());
    file.seekp(-1, std::ios::end);
    file.put(char(last ^ 0xFF));
  }
  Prepared rebuilt;
  Check(PrepareAstc4x4(input, root, rebuilt, &error) && !rebuilt.cache_hit,
        "Corrupt ASTC cache was not rebuilt");
  RecordObservedTexture(root, input, &rebuilt, "astc-encoded", 12);
  Check(fs::exists(root / "observed-textures.jsonl"),
        "Observed texture list was not written");
  const uint64_t budget = 3ull * 1024 * 1024 * 1024;
  Check(SetPreparationCacheBudget(root, budget, &error), "Preparation cache budget failed");
  uint64_t saved_budget = 0;
  std::ifstream(root / "cache-budget.txt") >> saved_budget;
  Check(saved_budget == budget && fs::exists(cache_path),
        "Preparation budget was not persisted or removed a completed texture");
  Check(!SetPreparationCacheBudget(root, 17ull * 1024 * 1024 * 1024, &error),
        "Unbounded preparation cache budget was accepted");
  fs::remove_all(root / "astc-v1");
  Check(SetPreparationCacheBudget(root, budget, &error), "Cache accounting did not reset after deletion");
  Prepared after_clear;
  Check(PrepareAstc4x4(input, root, after_clear, &error) && !after_clear.cache_hit &&
            after_clear.cache_persisted, "Cleared cache could not be regenerated");
  for (const char* name : {"preparation-state.json", "prepared-cache-index.json",
                           "archive-texture-manifest.json", "game-save.sav"})
    std::ofstream(root / name) << "fixture";
  Check(DeletePreparedCache(root, &error) && !fs::exists(root / "astc-v1") &&
            !fs::exists(root / "preparation-state.json") &&
            !fs::exists(root / "prepared-cache-index.json"),
        "Prepared cache and completion status were not deleted");
  Check(fs::exists(root / "archive-texture-manifest.json") &&
            fs::exists(root / "observed-textures.jsonl") && fs::exists(root / "game-save.sav"),
        "Cache deletion removed unrelated files");
  Check(SetPreparationCacheBudget(root, budget, &error), "Cache budget did not recover after deletion");
  Prepared after_delete;
  Check(PrepareAstc4x4(input, root, after_delete, &error) &&
            !after_delete.cache_hit && after_delete.cache_persisted,
        "Deleted cache could not be prepared again");
  fs::remove_all(root);
  std::cout << "ASTC texture conversion and cache: PASS\n";
}
