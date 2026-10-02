#import "Theft4TexturePreparation.h"
#include "theft4_texture_manifest.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <unordered_set>

namespace {
using Json = nlohmann::json;
using Path = std::filesystem::path;
constexpr uint64_t GiB = 1024ull * 1024 * 1024;
Path Root(NSURL *support) { return Path(support.fileSystemRepresentation) / "texture-preparation"; }
Json Load(const Path& path) {
  std::ifstream file(path);
  if (!file || std::filesystem::file_size(path) > 128ull * 1024 * 1024) return Json::object();
  return Json::parse(file, nullptr, false);
}
void Save(const Path& path, const Json& value) {
  std::filesystem::create_directories(path.parent_path());
  const auto temp = path.string() + ".tmp";
  { std::ofstream file(temp); file << value.dump() << '\n'; file.flush();
    if (!file) throw std::runtime_error("Cannot save texture preparation progress."); }
  std::filesystem::rename(temp, path);
}
NSDictionary *Dictionary(const Json& value) {
  const auto text = value.dump();
  NSData *data = [NSData dataWithBytes:text.data() length:text.size()];
  return [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] ?: @{};
}
}

@implementation Theft4TexturePreparation {
  std::atomic<bool> _cancelRequested;
  BOOL _running;
}
- (BOOL)running { return _running; }
- (void)cancel { _cancelRequested.store(true); }
+ (BOOL)isCompleteForGame:(NSURL *)game support:(NSURL *)support {
  try {
    const auto state = Load(Root(support) / "preparation-state.json");
    if (!state.value("complete", false) || state.value("encoderSchema", 0) != 1 ||
        state.at("sourceFingerprint") != theft4::astc::TextureSourceFingerprint(game.fileSystemRepresentation)) return NO;
    // Check names without reading 2.4 GB of payloads on each launch. Missing
    // prepared files invalidate completion; the encoder verifies checksums
    // whenever a cached texture is actually read.
    const auto index = Load(Root(support) / "prepared-cache-index.json");
    if (!index.is_array() || index.size() != state.at("total").get<size_t>()) return NO;
    std::unordered_set<std::string> available;
    for (const auto& entry : std::filesystem::directory_iterator(Root(support) / "astc-v1"))
      if (entry.path().extension() == ".bin") available.insert(entry.path().stem().string());
    for (const auto& key : index) if (!available.contains(key.get<std::string>())) return NO;
    return YES;
  } catch (...) { return NO; }
}
- (void)startForGame:(NSURL *)game support:(NSURL *)support
           progress:(void (^)(NSDictionary *))progress
         completion:(void (^)(BOOL, NSDictionary *, NSString *))completion {
  if (_running) return;
  _running = YES; _cancelRequested.store(false);
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    BOOL complete = NO; Json summary = Json::object(); std::string error;
    const auto root = Root(support);
    auto last_update = std::chrono::steady_clock::time_point{};
    auto last_log = std::chrono::steady_clock::time_point{};
    auto notify = [&](const Json& status, bool force = false) {
      const auto now = std::chrono::steady_clock::now();
      if (force || now - last_log >= std::chrono::seconds(10)) {
        last_log = now;
        NSLog(@"[texture-preparation] %s", status.dump().c_str());
      }
      if (!force && now - last_update < std::chrono::milliseconds(250)) return;
      last_update = now;
      NSDictionary *payload = Dictionary(status);
      dispatch_async(dispatch_get_main_queue(), ^{ progress(payload); });
    };
    try {
      const Path game_path(game.fileSystemRepresentation);
      const auto fingerprint = theft4::astc::TextureSourceFingerprint(game_path);
      Json manifest = Load(root / "archive-texture-manifest.json");
      if (!manifest.is_object() || manifest.value("schemaVersion", 0) != 1 ||
          manifest.value("sourceFingerprint", Json::object()) != fingerprint) {
        notify({{"phase", "indexing"}, {"completed", 0}, {"total", 0}}, true);
        if (!theft4::astc::ScanTextureManifest(game_path, game_path / "aes_key.bin", {},
          [&](const theft4::astc::ScanProgress& p) {
            notify({{"phase", "indexing"}, {"completed", p.containers},
                    {"total", p.total_containers}, {"textures", p.textures},
                    {"current", p.current_source}});
          }, self->_cancelRequested, manifest, error)) throw std::runtime_error(error);
        Save(root / "archive-texture-manifest.json", manifest);
      }
      const auto total = manifest.at("uniqueTextures").get<size_t>();
      if (!total) throw std::runtime_error("No supported static BC textures were found.");
      uint64_t cache_bytes = 0, required_bytes = 0;
      std::error_code ec;
      std::filesystem::create_directories(root / "astc-v1");
      for (const auto& entry : std::filesystem::directory_iterator(root / "astc-v1"))
        if (entry.is_regular_file()) cache_bytes += entry.file_size();
      std::unordered_set<std::string> counted;
      for (const auto& record : manifest.at("textures")) {
        const auto key = record.at("key").get<std::string>();
        if (!counted.insert(key).second) continue;
        if (!std::filesystem::exists(root / "astc-v1" / (key + ".bin")))
          required_bytes += record.at("astcBytes").get<uint64_t>() + 64 +
                            record.at("mips").get<uint64_t>() * 32;
      }
      const auto available = std::filesystem::space(root).available;
      if (available < required_bytes + GiB)
        throw std::runtime_error("Not enough free storage. Free space for the remaining prepared textures plus 1 GB, then resume.");
      // Include the existing cache and the new output; retain all completed
      // entries, including textures from earlier game versions or test runs.
      const uint64_t budget = std::max(GiB, cache_bytes + required_bytes + 64 * 1024 * 1024);
      if (!theft4::astc::SetPreparationCacheBudget(root, budget, &error))
        throw std::runtime_error(error);
      Save(root / "preparation-state.json", {{"complete", false}, {"encoderSchema", 1},
           {"sourceFingerprint", fingerprint}, {"total", total}});
      size_t encoded = 0, reused = 0, processed = 0;
      const auto started = std::chrono::steady_clock::now();
      notify({{"phase", "preparing"}, {"completed", 0}, {"total", total},
              {"requiredBytes", required_bytes}}, true);
      std::string visit_error;
      bool ok = theft4::astc::VisitManifestTextures(game_path, manifest,
        [&](const Json& record, const theft4::astc::Input& input) {
          if (self->_cancelRequested.load()) return false;
          theft4::astc::Prepared result;
          if (!theft4::astc::PrepareAstc4x4(input, root, result, &error)) return false;
          if (!result.cache_persisted) { error = "Texture converted but could not be saved. Check free storage and resume."; return false; }
          if (result.cache_hit) ++reused; else ++encoded;
          ++processed;
          const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
          notify({{"phase", "preparing"}, {"completed", processed}, {"total", total},
                  {"encoded", encoded}, {"reused", reused}, {"current", record.at("name")},
                  {"remainingSeconds", processed >= 32 ? elapsed * (total - processed) / processed : -1}});
          return true;
        }, {}, self->_cancelRequested, visit_error);
      // VisitManifestTextures returns its own stopping message. Preserve a
      // useful encoder/disk error when the visitor failed.
      if (!ok) throw std::runtime_error(self->_cancelRequested.load()
        ? "Preparation paused. Completed textures are saved; resume before Play."
        : !error.empty() ? error : visit_error);
      Json cache_index = Json::array();
      for (const auto& key : counted) cache_index.push_back(key);
      Save(root / "prepared-cache-index.json", cache_index);
      summary = {{"complete", true}, {"encoderSchema", 1}, {"sourceFingerprint", fingerprint},
                 {"total", total}, {"encoded", encoded}, {"reused", reused},
                 {"astcPayloadBytes", manifest.at("astcPayloadBytes")},
                 {"sourceWarnings", manifest.at("warnings").size()}};
      Save(root / "preparation-state.json", summary); complete = YES;
    } catch (const std::exception& failure) { error = failure.what(); }
    NSLog(@"[texture-preparation] finished complete=%d %s %s", complete, summary.dump().c_str(), error.c_str());
    NSDictionary *result = Dictionary(summary);
    NSString *failure = error.empty() ? nil : [NSString stringWithUTF8String:error.c_str()];
    dispatch_async(dispatch_get_main_queue(), ^{
      self->_running = NO; completion(complete, result, failure);
    });
  });
}
@end
