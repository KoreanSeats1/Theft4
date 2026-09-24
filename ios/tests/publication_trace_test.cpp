#include "theft4_publication_trace.h"
#include <cassert>
#include <thread>

int main() {
  theft4::PublicationTrace<8> trace;
  theft4::PublicationSample samples[8];
  uint64_t cursor = 0, lost = 0;
  trace.Record(1, 10);
  assert(trace.CopyAfter(cursor, samples, 8, lost) == 0);
  trace.Start();
  for (uint64_t n = 1; n <= 3; ++n) trace.Record(100 + n, n * 1000);
  assert(trace.CopyAfter(cursor, samples, 2, lost) == 2 && lost == 0);
  assert(samples[0].frame == 101 && samples[1].monotonic_ns == 2000);
  assert(trace.CopyAfter(cursor, samples, 8, lost) == 1 && samples[0].frame == 103);
  for (uint64_t n = 4; n <= 20; ++n) trace.Record(100 + n, n * 1000);
  assert(trace.CopyAfter(cursor, samples, 8, lost) == 8 && lost == 9);
  assert(samples[0].frame == 113 && samples[7].frame == 120);
  trace.Stop();
  trace.Record(121, 21000);
  assert(trace.CopyAfter(cursor, samples, 8, lost) == 0);
  cursor = trace.Start();
  assert(cursor == 20);  // A manual restart does not replay the previous run.
  trace.Record(122, 22000);
  assert(trace.CopyAfter(cursor, samples, 8, lost) == 1 && lost == 0);
  assert(samples[0].frame == 122 && samples[0].monotonic_ns == 22000);

  theft4::PublicationTrace<64> concurrent;
  concurrent.Start();
  std::thread producer([&] {
    for (uint64_t n = 1; n <= 100000; ++n) concurrent.Record(n, n * 1000);
  });
  cursor = 0;
  uint64_t previous = 0, total_lost = 0;
  while (cursor < 100000) {
    const size_t count = concurrent.CopyAfter(cursor, samples, 8, lost);
    total_lost += lost;
    for (size_t i = 0; i < count; ++i) {
      assert(samples[i].frame > previous);
      assert(samples[i].monotonic_ns == samples[i].frame * 1000);
      previous = samples[i].frame;
    }
  }
  producer.join();
  assert(total_lost + 1 <= 100000);
}
