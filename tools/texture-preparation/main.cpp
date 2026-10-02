#include "theft4_texture_manifest.h"
#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--verify-manifest") {
    std::ifstream file(argv[3]);
    const auto manifest = nlohmann::json::parse(file, nullptr, false);
    if (manifest.is_discarded()) { std::cerr << "Invalid manifest\n"; return 1; }
    std::atomic<bool> cancel{false}; std::string error; size_t verified = 0;
    if (!theft4::astc::VisitManifestTextures(argv[2], manifest,
        [&](const auto&, const auto&) { ++verified; return true; }, {}, cancel, error)) {
      std::cerr << error << '\n'; return 1;
    }
    if (verified != manifest.at("uniqueTextures").get<size_t>()) return 1;
    std::cout << verified << " source textures match their manifest cache keys\n";
    return 0;
  }
  if (argc == 4 && std::string(argv[1]) == "--resource") {
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<uint8_t> file{std::istreambuf_iterator<char>(input), {}};
    std::vector<uint8_t> data; size_t cpu; std::string error;
    if (!theft4::astc::DecodeTextureResource(file, data, cpu, error)) {
      std::cerr << error << '\n'; return 1;
    }
    std::ofstream output(argv[3], std::ios::binary);
    output.write(reinterpret_cast<char*>(data.data()), data.size());
    std::cout << "CPU " << cpu << " total " << data.size() << '\n'; return !output;
  }
  if (argc != 4) {
    std::cerr << "Usage: texture_manifest GAME_DIRECTORY AES_KEY OUTPUT_JSON\n"; return 2;
  }
  std::atomic<bool> cancel{false};
  nlohmann::json manifest; std::string error;
  bool ok = theft4::astc::ScanTextureManifest(argv[1], argv[2], {},
    [](const theft4::astc::ScanProgress& p) {
      std::cerr << p.containers << '/' << p.total_containers << " containers, "
                << p.textures << " textures: " << p.current_source << '\n';
    }, cancel, manifest, error);
  if (!ok) { std::cerr << error << '\n'; return 1; }
  std::ofstream file(argv[3]); file << manifest.dump(2) << '\n';
  if (!file) return 1;
  std::cout << manifest["uniqueTextures"] << " unique textures; "
            << manifest["warnings"].size() << " source warnings\n";
}
