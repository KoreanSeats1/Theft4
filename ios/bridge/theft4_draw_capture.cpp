#include "theft4_draw_capture.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <nlohmann/json.hpp>
#ifndef THEFT4_RENDER_SOURCE_REVISION
#define THEFT4_RENDER_SOURCE_REVISION "development"
#endif

namespace theft4::render {
namespace {
constexpr size_t kResidentLimit = 64 * 1024 * 1024;
constexpr size_t kOutputLimit = 128 * 1024 * 1024;
constexpr size_t kIndexedLimit = 24, kOtherLimit = 8, kQueueLimit = 4;
std::string ConfiguredDirectory() {
  const auto* enabled = std::getenv("THEFT4_METAL_CAPTURE");
  if (!enabled || std::string_view(enabled) != "1") return {};
  const auto* home = std::getenv("HOME");
  if (!home || !*home) return {};
  const auto seconds = std::chrono::system_clock::now().time_since_epoch().count();
  return (std::filesystem::path(home) / "Documents" / "MetalDrawCaptures" /
          ("run-" + std::to_string(seconds))).string();
}
size_t ResourceBytes(const Capture& capture) {
  std::unordered_set<const Bytes*> owners;
  size_t total = 0;
  const auto add = [&](const std::shared_ptr<const Bytes>& b) {
    if (b && owners.insert(b.get()).second) total += b->value.size();
  };
  for (const auto& b : capture.draw.constants) add(b.source);
  for (const auto& b : capture.draw.vertices) add(b.source);
  add(capture.draw.indices.source);
  for (const auto& f : capture.draw.fetches) if (f.image) add(f.image->source);
  return total;
}
}
DrawCaptureRecorder::DrawCaptureRecorder() : DrawCaptureRecorder(ConfiguredDirectory()) {}
DrawCaptureRecorder::DrawCaptureRecorder(std::string directory) : directory_(std::move(directory)) { Start(); }
void DrawCaptureRecorder::Start() {
  if (!directory_.empty()) writer_ = std::thread([this] { WriteLoop(); });
}
DrawCaptureRecorder::~DrawCaptureRecorder() {
  { std::lock_guard lock(mutex_); stopping_ = true; }
  changed_.notify_all();
  if (writer_.joinable()) writer_.join();
}
bool DrawCaptureRecorder::Active() const {
  return !directory_.empty() && submitted_bytes_ < kOutputLimit &&
         (indexed_ < kIndexedLimit || other_ < kOtherLimit);
}
bool DrawCaptureRecorder::Wants(const Pipeline& pipeline, bool indexed) const {
  return Active() && (indexed ? indexed_ < kIndexedLimit : other_ < kOtherLimit) &&
         !families_.contains({pipeline, indexed});
}
std::shared_ptr<const Bytes> DrawCaptureRecorder::Copy(
    std::span<const uint8_t> data, uint64_t generation, std::array<uint64_t, 4> conversion) {
  if (!Active() || data.empty() || data.size() > kResidentLimit) return {};
  const size_t previous = ledger_->resident.fetch_add(data.size());
  if (previous > kResidentLimit - data.size()) {
    ledger_->resident.fetch_sub(data.size());
    Reject("resident capture budget");
    return {};
  }
  auto ledger = ledger_;
  auto bytes = std::shared_ptr<Bytes>(new Bytes, [ledger, size = data.size()](Bytes* b) {
    delete b;
    ledger->resident.fetch_sub(size);
  });
  bytes->generation = generation ? generation : 1;
  bytes->conversion = conversion;
  bytes->value.assign(data.begin(), data.end());
  return bytes;
}
void DrawCaptureRecorder::Reject(const std::string& reason) {
  // Reasons are fixed diagnostic strings, not arbitrary game filenames.
  std::lock_guard lock(mutex_);
  if (rejections_.size() < 64 || rejections_.contains(reason)) ++rejections_[reason];
}
bool DrawCaptureRecorder::Submit(Capture capture, bool indexed) {
  if (!Wants(capture.draw.pipeline, indexed)) return false;
  std::string error;
  if (!Validate(capture, error)) { Reject(error); return false; }
  const size_t bytes = ResourceBytes(capture);
  if (bytes > kOutputLimit - submitted_bytes_) { Reject("total capture budget"); return false; }
  std::lock_guard lock(mutex_);
  if (queue_.size() >= kQueueLimit) { ++rejections_["capture writer queue full"]; return false; }
  const auto filename = "draw-" + std::to_string(families_.size()) + ".t4draw";
  families_.insert({capture.draw.pipeline, indexed});
  (indexed ? indexed_ : other_)++;
  submitted_bytes_ += bytes;
  queue_.emplace_back(std::move(capture), filename);
  changed_.notify_all();
  return true;
}
void DrawCaptureRecorder::Flush() {
  std::unique_lock lock(mutex_);
  changed_.wait(lock, [this] { return queue_.empty() && !writing_; });
}
void DrawCaptureRecorder::WriteLoop() {
  for (;;) {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
    if (queue_.empty() && stopping_) break;
    auto item = std::move(queue_.front()); queue_.pop_front(); writing_ = true;
    lock.unlock();
    std::string error;
    const bool okay = WriteCapture((std::filesystem::path(directory_) / item.second).string(), item.first, error);
    lock.lock();
    if (okay) ++written_; else { ++failures_; ++rejections_[error]; }
    nlohmann::json report = {
      {"schema", 1}, {"source_revision", THEFT4_RENDER_SOURCE_REVISION},
      {"written", written_}, {"failed", failures_}, {"queued", queue_.size()},
      {"indexed_families", indexed_}, {"other_families", other_},
      {"resource_bytes", submitted_bytes_}, {"resident_limit_bytes", kResidentLimit},
      {"output_limit_bytes", kOutputLimit}, {"rejections", rejections_},
      {"purpose", "Private isolated draw replay; original render-target contents and GPU-produced textures are not captured"}
    };
    lock.unlock();
    try {
      std::filesystem::create_directories(directory_);
      const auto file = std::filesystem::path(directory_) / "CAPTURE_REPORT.json";
      const auto temporary = file.string() + ".partial";
      { std::ofstream out(temporary); out << report.dump(2) << '\n'; }
      std::filesystem::rename(temporary, file);
    } catch (...) { /* File failures are also reported by the caller's log. */ }
    lock.lock(); writing_ = false; changed_.notify_all();
  }
}
}
