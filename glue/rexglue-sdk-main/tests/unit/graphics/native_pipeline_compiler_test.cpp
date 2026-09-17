#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <future>
#include <stdexcept>

#include "graphics/gta4_native/native_pipeline_compiler.h"

using Compiler = rex::graphics::gta4_native::NativePipelineCompiler<int, int, std::hash<int>>;

TEST_CASE("pipeline compiler bounds admission and deduplicates exact demand",
          "[gta4-native][pipeline-compiler]") {
  Compiler compiler(2);
  std::promise<void> entered;
  std::promise<void> release;
  auto gate = release.get_future().share();
  std::atomic<int> calls{0};
  REQUIRE(compiler.Enqueue(1, [&] {
    entered.set_value();
    gate.wait();
    ++calls;
    return 10;
  }));
  entered.get_future().wait();
  CHECK(compiler.Enqueue(1, [&] { ++calls; return 100; }));
  CHECK(compiler.Enqueue(2, [] { return 20; }));
  CHECK_FALSE(compiler.Enqueue(3, [] { return 30; }));
  CHECK(compiler.Enqueue(3, [] { return 30; }, true));
  CHECK_FALSE(compiler.Contains(2));
  release.set_value();
  const auto demanded = compiler.Take(3, true);
  REQUIRE(demanded);
  CHECK(*demanded == 30);
  const auto first = compiler.Take(1, true);
  REQUIRE(first);
  CHECK(*first == 10);
  CHECK(calls == 1);
}

TEST_CASE("pipeline compiler joins active work and contains callback failures",
          "[gta4-native][pipeline-compiler]") {
  Compiler compiler(2);
  REQUIRE(compiler.Enqueue(1, []() -> int { throw std::runtime_error("cache I/O"); }));
  const auto failed = compiler.Take(1, true);
  REQUIRE(failed);
  CHECK(*failed == 0);
  std::promise<void> entered;
  std::promise<void> release;
  auto gate = release.get_future().share();
  std::atomic<bool> finished{false};
  REQUIRE(compiler.Enqueue(2, [&] {
    entered.set_value();
    gate.wait();
    finished = true;
    return 20;
  }));
  entered.get_future().wait();
  auto stop = std::async(std::launch::async, [&] { compiler.Stop(); });
  CHECK_FALSE(finished);
  release.set_value();
  stop.get();
  CHECK(finished);
  CHECK_FALSE(compiler.Enqueue(3, [] { return 30; }));
  const auto completed = compiler.TakeCompleted();
  REQUIRE(completed.size() == 1);
  CHECK(completed.front().second == 20);
}

TEST_CASE("pipeline compiler supports a nonblocking first-use probe",
          "[gta4-native][pipeline-compiler]") {
  Compiler compiler(3);
  std::promise<void> entered;
  std::promise<void> release;
  auto gate = release.get_future().share();
  REQUIRE(compiler.Enqueue(7, [&] {
    entered.set_value();
    gate.wait();
    return 70;
  }));
  entered.get_future().wait();
  REQUIRE(compiler.Enqueue(8, [] { return 80; }));
  REQUIRE(compiler.Enqueue(9, [] { return 90; }));

  CHECK_FALSE(compiler.Take(9, false));
  CHECK(compiler.Contains(9));

  release.set_value();
  const auto compiled = compiler.Take(9, true);
  REQUIRE(compiled);
  CHECK(*compiled == 90);
  CHECK_FALSE(compiler.Contains(9));
  CHECK(compiler.Contains(8));
}
