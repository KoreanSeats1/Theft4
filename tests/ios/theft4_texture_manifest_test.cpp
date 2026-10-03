#include "../../ios/bridge/theft4_texture_manifest.h"
#include <rex/graphics/xenos.h>
#include <xxhash.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Bytes = std::vector<uint8_t>;
void Check(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
void Put32(Bytes& b, size_t p, uint32_t v, bool be = false) {
  if (be) v = __builtin_bswap32(v); std::memcpy(b.data() + p, &v, 4);
}
void Put16(Bytes& b, size_t p, uint16_t v, bool be = false) {
  if (be) v = __builtin_bswap16(v); std::memcpy(b.data() + p, &v, 2);
}
void Write(const std::filesystem::path& p, const Bytes& bytes) {
  std::ofstream file(p, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
Bytes Fixture() {
  Bytes resource(32768);
  Put32(resource, 0x40, 0x00631A9C);
  Put32(resource, 0x40 + 20, 0x50000100, true);
  Put32(resource, 0x40 + 24, 0x50000200, true);
  Put16(resource, 0x40 + 28, 4, true); Put16(resource, 0x40 + 30, 4, true);
  std::memcpy(resource.data() + 0x100, "red", 4);
  rex::graphics::xenos::xe_gpu_texture_fetch_t fetch{};
  fetch.type = rex::graphics::xenos::FetchConstantType::kTexture;
  fetch.dimension = rex::graphics::xenos::DataDimension::k2DOrStacked;
  fetch.size_2d.width = 3; fetch.size_2d.height = 3;
  fetch.pitch = 1; fetch.base_address = 0x60000;
  fetch.format = rex::graphics::xenos::TextureFormat::k_DXT1;
  const uint32_t* words = &fetch.dword_0;
  for (size_t i = 0; i < 6; ++i) Put32(resource, 0x200 + 28 + i * 4, words[i], true);
  resource[4097] = 0xF8; // opaque red BC1 block: 00 F8 00 00 00 00 00 00
  Bytes frame{0x08, 0x30, 0x00, 0x00, 1,0,0,0, 1,0,0,0, 1,0,0,0};
  frame.insert(frame.end(), resource.begin(), resource.end());
  frame.insert(frame.end(), {0xA5, 0x5A, 0xC3, 0x3C});
  Bytes file(22);
  Put32(file, 0, 0x05435352); Put32(file, 4, 7);
  Put32(file, 8, 16 | (112 << 15));
  Put32(file, 16, uint32_t(frame.size() + 2), true);
  Put16(file, 20, uint16_t(frame.size()), true);
  file.insert(file.end(), frame.begin(), frame.end()); return file;
}
}
int main() {
  namespace fs = std::filesystem;
  using namespace theft4::astc;
  const auto root = fs::temp_directory_path() / "theft4-manifest-fixtures";
  fs::remove_all(root); fs::create_directories(root);
  const auto file = Fixture(); Write(root / "fixture.xtd", file);
  Bytes decoded; size_t cpu = 0; std::string error;
  Check(DecodeTextureResource(file, decoded, cpu, error), "Synthetic RSC5 decoding failed");
  Check(cpu == 4096 && decoded.size() == 32768 && decoded[4097] == 0xF8,
        "Resource sizes or decompression differ");
  auto truncated = file; truncated.resize(25);
  Check(!DecodeTextureResource(truncated, decoded, cpu, error), "Truncated RSC5 accepted");
  const size_t allocated = ((file.size() + 2047) / 2048) * 2048;
  Bytes img(2048 + allocated);
  Put32(img, 0, 0xA94E2A52); Put32(img, 4, 3); Put32(img, 8, 1);
  Put32(img, 12, 28); Put16(img, 16, 16);
  Put32(img, 20, 0xC0000000); Put32(img, 24, 7); Put32(img, 28, 1);
  Put16(img, 32, allocated / 2048); Put16(img, 34, allocated - file.size());
  std::memcpy(img.data() + 36, "fixture.xtd", 12);
  std::memcpy(img.data() + 2048, file.data(), file.size()); Write(root / "fixture.img", img);
  std::atomic<bool> cancel{false}; nlohmann::json manifest; size_t visits = 0;
  Check(ScanTextureManifest(root, root / "missing-key", [&](const auto&, const Input& input) {
    ++visits; Check(input.width == 4 && input.height == 4 && input.payload.size() == 8,
                   "Texture layout differs from expected visible BC blocks");
    const std::array<uint8_t, 8> expected{0, 0xF8, 0, 0, 0, 0, 0, 0};
    Check(std::equal(expected.begin(), expected.end(), input.payload.begin()), "BC untile changed payload");
    Check(input.content_hash == XXH3_64bits(expected.data(), expected.size()), "Canonical hash differs");
    return true;
  }, {}, cancel, manifest, error), "Manifest scan failed");
  Check(visits == 1 && manifest["uniqueTextures"] == 1 && manifest["textures"].size() == 2 &&
        manifest["warnings"].empty(), "IMG parsing or duplicate detection failed");
  visits = 0;
  Check(VisitManifestTextures(root, manifest, [&](const auto&, const auto&) { ++visits; return true; },
        {}, cancel, error) && visits == 1, "Manifest revisit failed");
  cancel.store(true);
  Check(!VisitManifestTextures(root, manifest, [](const auto&, const auto&) { return true; },
        {}, cancel, error), "Cancellation ignored"); cancel.store(false);
  auto malicious = manifest; malicious["textures"][0]["container"] = "../escape.xtd";
  Check(!VisitManifestTextures(root, malicious, [](const auto&, const auto&) { return true; },
        {}, cancel, error), "Manifest path traversal accepted");
  img.resize(40); Write(root / "fixture.img", img);
  Check(!VisitManifestTextures(root, manifest, [](const auto&, const auto&) { return true; },
        {}, cancel, error), "Changed source accepted");
  Check(ScanTextureManifest(root, root / "missing-key", {}, {}, cancel, manifest, error) &&
        !manifest["warnings"].empty(), "Malformed archive not reported");
  fs::remove_all(root); std::cout << "Texture manifest, bounds, resume and source identity: PASS\n";
}
