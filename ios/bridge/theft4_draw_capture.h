#pragma once
#include "theft4_render_plan.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace theft4::render {
// Optional, render-worker-owned diagnostic. Disk encoding happens on its own
// worker. Limits cover resident copies, queued work, total bytes, and families.
// This recorder is not part of the default game or the Metal backend.
class DrawCaptureRecorder {
 public:
  DrawCaptureRecorder();
  explicit DrawCaptureRecorder(std::string directory);
  ~DrawCaptureRecorder();
  bool Active() const;
  bool Wants(const Pipeline& pipeline, bool indexed) const;
  std::shared_ptr<const Bytes> Copy(std::span<const uint8_t> data,
                                  uint64_t generation = 1,
                                  std::array<uint64_t, 4> conversion = {});
  bool Submit(Capture capture, bool indexed);
  void Reject(const std::string& reason);
  void Flush();
  const std::string& Directory() const { return directory_; }
 private:
  struct Ledger { std::atomic<size_t> resident{0}; };
  std::shared_ptr<Ledger> ledger_ = std::make_shared<Ledger>();
  std::string directory_;
  std::set<std::pair<Pipeline, bool>> families_;
  size_t indexed_ = 0, other_ = 0, submitted_bytes_ = 0;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<std::pair<Capture, std::string>> queue_;
  std::map<std::string, size_t> rejections_;
  std::thread writer_;
  size_t written_ = 0, failures_ = 0;
  bool stopping_ = false, writing_ = false;
  void Start();
  void WriteLoop();
};
}
