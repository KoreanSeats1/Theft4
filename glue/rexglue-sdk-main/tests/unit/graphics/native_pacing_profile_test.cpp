#include <catch2/catch_test_macros.hpp>
#include <rex/graphics/gta4_native/pacing_profile.h>
#include "graphics/gta4_native/native_cpu_profile.h"
#include "graphics/gta4_native/native_pacing_export.h"
#include <chrono>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace pacing = rex::graphics::gta4_native::pacing;
namespace profile = rex::graphics::gta4_native::profile;

TEST_CASE("pacing recorder is opt-in, bounded and immutable after stop", "[native-pacing]") {
  pacing::Recorder<2> recorder;
  pacing::Sample sample;
  sample.frame = 10;
  CHECK_FALSE(recorder.Active());
  CHECK_FALSE(recorder.Record(sample));
  REQUIRE(recorder.Start(100));
  CHECK_FALSE(recorder.Start(101));
  REQUIRE(recorder.Record(sample));
  sample.frame = 11;
  REQUIRE(recorder.Record(sample));
  CHECK_FALSE(recorder.Record(sample));
  recorder.Stop(200);
  recorder.Stop(300);
  CHECK_FALSE(recorder.Record(sample));
  CHECK_FALSE(recorder.Start(400));
  const auto result = recorder.Read();
  REQUIRE(result.samples.size() == 2);
  CHECK(result.samples[0].frame == 10);
  CHECK(result.samples[1].frame == 11);
  CHECK(result.dropped == 1);
  CHECK(result.begin_tick == 100);
  CHECK(result.end_tick == 200);
  CHECK(result.started);
  CHECK(result.stopped);
}

TEST_CASE("multiple producers retain complete pacing records while stop freezes storage",
          "[native-pacing]") {
  pacing::Recorder<256> recorder;
  REQUIRE(recorder.Start(1));
  std::array<std::thread, 4> producers;
  for (uint32_t thread = 0; thread < producers.size(); ++thread) {
    producers[thread] = std::thread([&, thread] {
      for (uint32_t i = 0; i < 100; ++i) {
        pacing::Sample sample;
        sample.frame = thread * 100 + i;
        sample.system_thread = thread;
        sample.hook_begin = uint64_t(sample.frame) * 3 + 1;
        sample.submit_end = sample.hook_begin + 2;
        recorder.Record(sample);
      }
    });
  }
  for (auto& producer : producers) producer.join();
  recorder.Stop(500);
  const auto result = recorder.Read();
  REQUIRE(result.samples.size() == 256);
  CHECK(result.dropped == 144);
  std::unordered_set<uint32_t> identities;
  for (const auto& sample : result.samples) {
    CHECK(sample.frame / 100 == sample.system_thread);
    CHECK(sample.submit_end == uint64_t(sample.frame) * 3 + 3);
    identities.insert(sample.frame);
  }
  CHECK(identities.size() == result.samples.size());
}

TEST_CASE("stopping a pacing capture racing producers permits no post-stop writes",
          "[native-pacing]") {
  pacing::Recorder<> recorder;
  REQUIRE(recorder.Start(1));
  std::atomic<uint32_t> attempts{0};
  std::thread producer([&] {
    for (uint32_t i = 0; i < 10000; ++i) {
      pacing::Sample sample;
      sample.frame = i;
      recorder.Record(sample);
      attempts.fetch_add(1, std::memory_order_release);
    }
  });
  while (!attempts.load(std::memory_order_acquire)) std::this_thread::yield();
  recorder.Stop(100);
  const auto stopped = recorder.Read();
  producer.join();
  const auto after = recorder.Read();
  CHECK(stopped.samples.size() == after.samples.size());
  CHECK(stopped.dropped == after.dropped);
  CHECK(after.stopped);
}

TEST_CASE("worker acquisition partitions time without inventing pure idle time", "[native-pacing]") {
  profile::TransportSummary summary;
  summary.ObserveWorker(100, 5, 50, 20, true, true);
  summary.ObserveWorker(10, 0, 0, 0, false, false);
  CHECK(summary.worker_idle_ticks == 110);
  CHECK(summary.worker_dispatch_ticks == 35);
  CHECK(summary.worker_mutex_ticks + summary.worker_condition_ticks +
        summary.worker_transfer_ticks + summary.worker_dispatch_ticks == summary.worker_idle_ticks);
  CHECK(summary.worker_batches == 1);
  CHECK(summary.worker_condition_waits == 1);
  CHECK(summary.worker_partition_errors == 0);
  summary.Observe({100, 70, 0, 0, 0}, 200, 3, 7);
  summary.Observe({120, 50, 0, 0, 0}, 240, 2, 8);
  summary.Observe({}, 300, 1, 9);
  CHECK(summary.first_capture_tick == 30);
  CHECK(summary.first_enqueue_tick == 100);
  CHECK(summary.last_enqueue_tick == 120);
  CHECK(summary.first_dequeue_tick == 200);
  CHECK(summary.last_dequeue_tick == 240);
  CHECK(summary.first_sequence == 7);
  CHECK(summary.last_sequence == 8);
  summary.ObserveWorker(1, 2, 3, 4, true, true);
  CHECK(summary.worker_partition_errors == 1);
  CHECK(summary.worker_dispatch_ticks == 35);  // No unsigned underflow.
}

TEST_CASE("pacing export separates clock domains, sleep overshoot and unavailable values",
          "[native-pacing]") {
  const auto directory = std::filesystem::temp_directory_path() /
      ("theft4-pacing-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  REQUIRE(std::filesystem::create_directory(directory));
  pacing::Snapshot snapshot;
  snapshot.started = snapshot.stopped = true;
  pacing::Sample sample;
  sample.frame = 17;
  sample.submitted = sample.wait_requested = true;
  sample.hook_begin = 100;
  sample.submit_begin = 120;
  sample.submit_end = 130;
  sample.mutex_begin = 132;
  sample.mutex_acquired = 135;
  sample.decision_ns = 10000000;
  sample.prior_deadline_ns = 9000000;
  sample.wait_until_ns = 14000000;
  sample.sleep_begin_ns = 11000000;
  sample.wake_ns = 15500000;
  snapshot.samples.push_back(sample);
  sample.submitted = false;
  snapshot.samples.push_back(sample);
  REQUIRE(pacing::Export(directory, snapshot, 99, 1000));
  std::ifstream input(directory / "native-performance-pacing.csv");
  std::vector<std::vector<std::string>> rows;
  for (std::string line; std::getline(input, line);) {
    std::istringstream stream(line);
    std::vector<std::string> fields;
    for (std::string field; std::getline(stream, field, ',');) fields.push_back(field);
    if (!line.empty() && line.back() == ',') fields.emplace_back();
    rows.push_back(std::move(fields));
  }
  REQUIRE(rows.size() == 3);
  REQUIRE(rows[1].size() == rows[0].size());
  REQUIRE(rows[2].size() == rows[0].size());
  const auto value = [&](size_t row, const char* column) -> const std::string& {
    const auto found = std::find(rows[0].begin(), rows[0].end(), column);
    REQUIRE(found != rows[0].end());
    return rows[row][size_t(found - rows[0].begin())];
  };
  CHECK(value(1, "capture_id") == "99");
  CHECK(value(1, "frame") == "17");
  CHECK(std::stod(value(1, "limiter_mutex_wait_ms")) == 3);
  CHECK(std::stod(value(1, "decision_wait_ms")) == 4);
  CHECK(std::stod(value(1, "requested_sleep_ms")) == 3);
  CHECK(std::stod(value(1, "actual_sleep_ms")) == 4.5);
  CHECK(std::stod(value(1, "wake_overshoot_ms")) == 1.5);
  CHECK(std::stod(value(1, "entry_lateness_ms")) == 1);
  CHECK(value(2, "actual_sleep_ms").empty());
  snapshot.stopped = false;
  CHECK_FALSE(pacing::Export(directory, snapshot, 100, 1000));
  std::filesystem::remove_all(directory);
}
