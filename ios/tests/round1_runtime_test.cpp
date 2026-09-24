#include "theft4_frame_stage_trace.h"
#include "gta4_frame_limiter.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
int main() {
  theft4::FrameStageTrace<16> trace;
  theft4::FrameStageSample samples[16];
  uint64_t cursor = trace.Start(), lost = 0;
  for (unsigned n = 1; n <= 32; ++n) trace.Record({1, n, n * 1000u, n * 2u, n * 3u});
  assert(trace.CopyAfter(cursor, samples, 16, lost) == 16 && lost == 16);
  assert(samples[0].frame == 17 && samples[15].frame == 32);
  trace.Stop(); trace.Record({});
  assert(trace.CopyAfter(cursor, samples, 16, lost) == 0);
  assert(trace.Start() == cursor);
  theft4::FrameStageTrace<512> concurrent;
  cursor = concurrent.Start();
  std::atomic<unsigned> done{0};
  std::vector<std::thread> writers;
  for (unsigned i = 0; i < 4; ++i) writers.emplace_back([&, i] {
    for (unsigned j = 0; j < 20000; ++j) concurrent.Record({i, j, j * 1000u, j * 2u, j * 3u});
    ++done;
  });
  uint64_t received = 0, total_lost = 0;
  do {
    auto n = concurrent.CopyAfter(cursor, samples, 16, lost);
    received += n; total_lost += lost;
    for (size_t j = 0; j < n; ++j) {
      const auto& s = samples[j];
      assert(s.stage < 4 && s.monotonic_ns == s.frame * 1000 && s.a == s.frame * 2 && s.b == s.frame * 3);
    }
  } while (done != 4);
  for (auto& t : writers) t.join();
  size_t n;
  do { n = concurrent.CopyAfter(cursor, samples, 16, lost); received += n; total_lost += lost; } while (n);
  assert(received + total_lost == 80000);
  namespace limiter = gta4::frame_limiter;
  auto d = limiter::PlanDisplayAligned({}, 30, 1'000'000'000, 1'010'000'000);
  assert(d.wait_until_ns == 1'010'000'000);
  auto next = limiter::PlanDisplayAligned(d.next_state, 30, 1'030'000'000, 1'043'333'333);
  assert(next.wait_until_ns == d.next_state.next_deadline_ns);
  auto late = limiter::PlanDisplayAligned(next.next_state, 30, 1'200'000'000, 1'210'000'000);
  assert(late.late_reset && late.wait_until_ns == 0 && late.next_state.next_deadline_ns == 1'210'000'000);
  auto disabled = limiter::PlanDisplayAligned({}, 0, 1'000'000'000, 1'010'000'000);
  assert(disabled.next_state.frames_per_second == 0 && disabled.wait_until_ns == 0);
  auto stale = limiter::PlanDisplayAligned({}, 30, 1'000'000'000, 1);
  assert(stale.wait_until_ns == 0);
  auto sixty = limiter::PlanDisplayAligned({}, 60, 1'000'000'000, 1'010'000'000);
  assert(sixty.wait_until_ns == 0 && sixty.next_state.frames_per_second == 60);
  std::cout << "multi-producer trace and display pacing tests passed\n";
}
