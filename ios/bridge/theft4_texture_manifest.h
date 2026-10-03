#pragma once

#include "theft4_astc_texture.h"
#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>

namespace theft4::astc {

struct ScanProgress {
  size_t containers = 0;
  size_t total_containers = 0;
  size_t resources = 0;
  size_t textures = 0;
  std::string current_source;
};

// Read-only. The visitor's spans are valid only until it returns. Uses the
// renderer's TextureInfo, tiling, endian and XXH3 routines, so cache identities
// can be compared directly against observed-textures.jsonl.
using TextureVisitor = std::function<bool(const nlohmann::json&, const Input&)>;
using ScanObserver = std::function<void(const ScanProgress&)>;
bool DecodeTextureResource(std::span<const uint8_t> file, std::vector<uint8_t>& data,
                           size_t& cpu_size, std::string& error);
nlohmann::json TextureSourceFingerprint(const std::filesystem::path& game);
bool ScanTextureManifest(const std::filesystem::path& game,
                         const std::filesystem::path& aes_key,
                         const TextureVisitor& visitor, const ScanObserver& observer,
                         const std::atomic<bool>& cancel, nlohmann::json& manifest,
                         std::string& error);
bool VisitManifestTextures(const std::filesystem::path& game, const nlohmann::json& manifest,
                           const TextureVisitor& visitor, const ScanObserver& observer,
                           const std::atomic<bool>& cancel, std::string& error);

}  // namespace theft4::astc
