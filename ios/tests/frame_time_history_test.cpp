#include "theft4_frame_time_history.h"
#include <cassert>
#include <cmath>
#include <thread>

int main() {
  theft4::FrameTimeHistory<8> history;
  double values[8] = {}, pending = -1;
  history.Record(1);
  assert(history.Copy(10, values, 8, pending) == 0 && pending == 0);
  history.SetEnabled(true);
  history.Record(1'000'000);
  history.Record(34'333'333);
  history.Record(84'333'333);
  assert(history.Copy(100'000'000, values, 8, pending) == 2);
  assert(std::abs(values[0] - 33.333333) < 1e-6 && values[1] == 50);
  assert(std::abs(pending - 15.666667) < 1e-6);
  assert(history.Copy(100'000'000, values, 1, pending) == 1 && values[0] == 50);
  history.SetEnabled(false);
  assert(history.Copy(200'000'000, values, 8, pending) == 0 && pending == 0);
  history.SetEnabled(true);
  history.Record(5'000'000'000);
  assert(history.Copy(5'080'000'000, values, 8, pending) == 0 && pending == 80);
  history.Record(5'033'333'333);
  history.SetEnabled(true); // Idempotent visibility refresh must not erase history.
  assert(history.Copy(5'033'333'333, values, 8, pending) == 1);
  assert(std::abs(values[0] - 33.333333) < 1e-6);
  for (uint64_t i = 1; i <= 100; ++i) history.Record(6'000'000'000 + i * 1'000'000);
  assert(history.Copy(6'100'000'000, values, 8, pending) == 7);
  for (unsigned i = 0; i < 7; ++i) assert(values[i] == 1);
  // One publication writer races the UI reader and visibility toggles. No
  // accepted interval may contain a torn timestamp or span disabled history.
  theft4::FrameTimeHistory<32> concurrent;
  concurrent.SetEnabled(true);
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (uint64_t i = 1; i <= 100000; ++i) concurrent.Record(i * 1'000'000);
    done.store(true);
  });
  size_t reads = 0;
  while (!done.load()) {
    auto count = concurrent.Copy(0, values, 8, pending);
    assert(count <= 8 && pending == 0);
    for (size_t i = 0; i < count; ++i) assert(values[i] == 1);
    if (++reads % 17 == 0) { concurrent.SetEnabled(false); concurrent.SetEnabled(true); }
  }
  writer.join();
}
