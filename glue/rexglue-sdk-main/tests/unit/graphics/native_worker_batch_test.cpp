#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <memory_resource>
#include <random>
#include <type_traits>
#include <unordered_set>
#include <vector>
#include "graphics/gta4_native/native_worker_batch.h"
#include "graphics/gta4_native/native_command_recycler.h"
#include "graphics/gta4_native/native_texture_protection.h"

using namespace rex::graphics::gta4_native;
namespace {
struct Command {
  uint64_t sequence;
  std::unique_ptr<uint64_t> payload;
  std::shared_ptr<uint64_t> owner;
  std::array<uint64_t, 8> textures{};
};
class CountingResource : public std::pmr::memory_resource {
 public:
  size_t allocations = 0, live = 0, peak = 0;
 private:
  void* do_allocate(size_t bytes, size_t alignment) override {
    void* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    ++allocations; live += bytes; peak = std::max(peak, live); return result;
  }
  void do_deallocate(void* pointer, size_t bytes, size_t alignment) override {
    live -= bytes; std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};
}

TEST_CASE("Worker staging preserves FIFO and move-only payloads across wraps", "[graphics][worker-batch]") {
  NativeWorkerBatch<Command, 64> batch;
  std::deque<uint64_t> oracle;
  std::mt19937 random(83199);
  uint64_t sequence = 0;
  for (size_t operation = 0; operation < 30000; ++operation) {
    if (batch.empty() || (batch.size() < batch.capacity() && random() % 3)) {
      Command command{++sequence, std::make_unique<uint64_t>(sequence), {}};
      batch.push_back(std::move(command));
      REQUIRE_FALSE(command.payload);
      oracle.push_back(sequence);
      REQUIRE(batch.back().sequence == sequence);
    } else {
      REQUIRE(batch.front().sequence == oracle.front());
      REQUIRE(*batch.front().payload == oracle.front());
      batch.pop_front(); oracle.pop_front();
    }
    REQUIRE(batch.size() == oracle.size());
    if (operation % 137 == 0) {
      auto expected = oracle.begin();
      for (const auto& command : batch) REQUIRE(command.sequence == *expected++);
      REQUIRE(expected == oracle.end());
    }
  }
  batch.clear(); REQUIRE(batch.empty());
}

TEST_CASE("Indirect queue transfer preserves addresses and resource ownership",
          "[graphics][worker-batch][memory]") {
  using Owner = std::unique_ptr<Command>;
  NativeWorkerBatch<Owner, 128> batch;
  std::deque<Owner> queue;
  std::vector<std::weak_ptr<uint64_t>> resources;
  std::vector<Command*> addresses;
  uint64_t expected = 0;
  for (unsigned round = 0; round < 8; ++round) {
    addresses.clear();
    resources.clear();
    for (size_t i = 0; i < batch.capacity(); ++i) {
      auto command = std::make_unique<Command>();
      command->sequence = round * batch.capacity() + i;
      command->payload = std::make_unique<uint64_t>(command->sequence);
      command->owner = std::make_shared<uint64_t>(command->sequence);
      resources.push_back(command->owner);
      addresses.push_back(command.get());
      queue.push_back(std::move(command));
    }
    while (!queue.empty()) {
      batch.push_back(std::move(queue.front()));
      REQUIRE_FALSE(queue.front());
      queue.pop_front();
    }
    const auto& read_only_batch = batch;
    size_t index = 0;
    for (const auto& owner : read_only_batch) {
      REQUIRE(&NativeQueueCommand(owner) == addresses[index++]);
      REQUIRE(owner->owner.use_count() == 1);
    }
    index = 0;
    while (!batch.empty()) {
      Command& command = NativeQueueCommand(batch.front());
      REQUIRE(&command == addresses[index]);
      REQUIRE(command.sequence == expected++);
      REQUIRE(*command.payload == command.sequence);
      // Actual renderer retains frame draws by moving the command once.
      Command retained = std::move(command);
      batch.pop_front();
      REQUIRE_FALSE(resources[index].expired());
      retained.owner.reset();
      REQUIRE(resources[index++].expired());
    }
  }
  auto pending = std::make_unique<Command>();
  pending->owner = std::make_shared<uint64_t>(1);
  std::weak_ptr<uint64_t> pending_resource = pending->owner;
  batch.push_back(std::move(pending));
  batch.clear(); // Shutdown before processing must release pending resources.
  REQUIRE(pending_resource.expired());
}

TEST_CASE("Recycled command storage releases resources and preserves staged order",
          "[graphics][worker-batch][memory]") {
  NativeCommandRecycler<Command, 4, 8> recycler;
  NativeWorkerBatch<std::unique_ptr<Command>, 4> batch;
  std::array<Command*, 4> addresses{};
  std::array<std::weak_ptr<uint64_t>, 4> resources{};
  for (size_t i = 0; i < 4; ++i) {
    bool reused = true;
    auto command = recycler.Acquire(&reused);
    REQUIRE_FALSE(reused);
    addresses[i] = command.get();
    command->sequence = i;
    command->owner = std::make_shared<uint64_t>(i);
    resources[i] = command->owner;
    batch.push_back(std::move(command));
  }
  for (size_t i = 0; i < 4; ++i) {
    REQUIRE(batch.front()->sequence == i);
    auto command = batch.take_front();
    recycler.Recycle(std::move(command));
    REQUIRE(resources[i].expired());
  }
  REQUIRE(recycler.SharedSize() == 4);
  for (size_t i = 0; i < 4; ++i) {
    bool reused = false;
    auto command = recycler.Acquire(&reused);
    REQUIRE(reused);
    REQUIRE(std::find(addresses.begin(), addresses.end(), command.get()) != addresses.end());
    REQUIRE(command->sequence == 0);
    REQUIRE_FALSE(command->owner);
    REQUIRE_FALSE(command->payload);
    batch.push_back(std::move(command));
  }
  batch.clear();
  for (size_t i = 0; i < 16; ++i) recycler.Recycle(std::make_unique<Command>());
  REQUIRE(recycler.SharedSize() == 8); // capped after burst
  REQUIRE(recycler.SharedHighWater() == 8);
}

TEST_CASE("Worker slots release owners on pop clear and destruction", "[graphics][worker-batch][memory]") {
  std::vector<std::weak_ptr<uint64_t>> observers;
  {
    NativeWorkerBatch<Command, 64> batch;
    for (size_t i = 0; i < batch.capacity(); ++i) {
      auto owner = std::make_shared<uint64_t>(i); observers.push_back(owner);
      batch.push_back(Command{i, {}, std::move(owner)});
    }
    REQUIRE(batch.size() == 64);
    auto moved = std::move(batch.front());
    batch.pop_front(); REQUIRE_FALSE(observers[0].expired());
    moved.owner.reset(); REQUIRE(observers[0].expired());
    batch.pop_front(); REQUIRE(observers[1].expired());
    batch.clear(); for (const auto& owner : observers) REQUIRE(owner.expired());
    auto owner = std::make_shared<uint64_t>(99); observers.push_back(owner);
    batch.push_back(Command{99, {}, std::move(owner)});
    REQUIRE_FALSE(observers.back().expired());
  }
  REQUIRE(observers.back().expired());
}

TEST_CASE("Queue batch active and frame pins agree with a resource scan", "[graphics][worker-batch][memory]") {
  const auto exercise = []<bool Indirect>() {
  using Entry = std::conditional_t<Indirect, std::unique_ptr<Command>, Command>;
  NativeTextureProtectionIndex queued, staged, deferred;
  NativeWorkerBatch<Entry, 64> batch;
  std::deque<Entry> queue;
  std::vector<Command> frame;
  std::mt19937 random(407);
  uint64_t sequence = 0, expected_sequence = 0;
  auto add = [](const Command& command, auto& pins) {
    for (auto generation : command.textures) if (generation) pins.insert(generation);
  };
  for (size_t round = 0; round < 160; ++round) {
    for (size_t i = 0; i < 64; ++i) {
      auto entry = [] {
        if constexpr (Indirect) return std::make_unique<Command>();
        else return Command{};
      }();
      Command& command = NativeQueueCommand(entry);
      command.sequence = ++sequence;
      for (auto& generation : command.textures) {
        generation = random() % 24; // Nulls, repeated bindings and shared sources.
        REQUIRE(queued.Retain(generation));
      }
      queue.push_back(std::move(entry));
    }
    while (!queue.empty()) {
      batch.push_back(std::move(queue.front())); queue.pop_front();
      for (auto generation : NativeQueueCommand(batch.back()).textures) {
        REQUIRE(staged.Retain(generation));
      }
    }
    REQUIRE(deferred.AssignFrom(staged));
    while (!batch.empty()) {
      const auto& active = NativeQueueCommand(batch.front());
      REQUIRE(active.sequence == ++expected_sequence);
      for (auto generation : active.textures) REQUIRE(staged.Release(generation));
      std::unordered_set<uint64_t> actual, oracle;
      REQUIRE(queued.AppendToExcluding(deferred, actual)); staged.AppendTo(actual);
      add(active, actual);
      for (const auto& command : frame) add(command, actual);
      for (const auto& command : frame) add(command, oracle);
      for (const auto& command : batch) add(NativeQueueCommand(command), oracle);
      for (const auto& command : queue) add(NativeQueueCommand(command), oracle);
      REQUIRE(actual == oracle);
      frame.push_back(std::move(NativeQueueCommand(batch.front()))); batch.pop_front();
      if (expected_sequence % 19 == 0) frame.clear(); // Present/flush within a batch.
    }
    REQUIRE(queued.ReleaseAllFrom(deferred)); deferred.Reset();
    REQUIRE(queued.size() == 0); REQUIRE(staged.size() == 0);
    REQUIRE(queued.valid()); REQUIRE(staged.valid()); REQUIRE(deferred.valid());
  }
  };
  exercise.template operator()<false>();
  exercise.template operator()<true>();
}

TEST_CASE("Deferred batch counts preserve producer references with shared generations",
          "[graphics][worker-batch][memory]") {
  NativeTextureProtectionIndex queued, staged, deferred;
  for (uint64_t generation : {3, 3, 7, 11, 11, 11}) {
    REQUIRE(queued.Retain(generation));
    REQUIRE(staged.Retain(generation));
  }
  REQUIRE(deferred.AssignFrom(staged));
  // Producers may enqueue more references while the worker owns its batch.
  for (uint64_t generation : {3, 7, 7, 13}) REQUIRE(queued.Retain(generation));
  std::unordered_set<uint64_t> visible;
  REQUIRE(queued.AppendToExcluding(deferred, visible));
  REQUIRE(visible == std::unordered_set<uint64_t>{3, 7, 13});
  REQUIRE(queued.ReleaseAllFrom(deferred));
  visible.clear(); queued.AppendTo(visible);
  REQUIRE(visible == std::unordered_set<uint64_t>{3, 7, 13});

  NativeTextureProtectionIndex impossible;
  REQUIRE(impossible.Retain(11));
  REQUIRE_FALSE(queued.AppendToExcluding(impossible, visible));
  REQUIRE_FALSE(queued.ReleaseAllFrom(impossible));
  REQUIRE_FALSE(queued.valid());
}

#ifdef THEFT4_LAB_BUILD
TEST_CASE("Pooled texture pins recycle storage without retaining historical IDs", "[graphics][worker-batch][memory]") {
  CountingResource memory;
  {
    NativeTextureProtectionIndex pins(&memory);
    size_t warm_allocations = 0, warm_live = 0;
    for (uint64_t round = 0; round < 1200; ++round) {
      for (uint64_t i = 1; i <= 256; ++i) {
        REQUIRE(pins.Retain(round * 256 + i)); REQUIRE(pins.Retain(round * 256 + i));
      }
      for (uint64_t i = 1; i <= 256; ++i) {
        REQUIRE(pins.Release(round * 256 + i));
        REQUIRE(pins.Contains(round * 256 + i));
        REQUIRE(pins.Release(round * 256 + i));
        REQUIRE_FALSE(pins.Contains(round * 256 + i));
      }
      REQUIRE(pins.size() == 0); REQUIRE(pins.valid());
      if (round == 8) { warm_allocations = memory.allocations; warm_live = memory.live; }
      if (round > 8) { REQUIRE(memory.allocations == warm_allocations); REQUIRE(memory.live == warm_live); }
    }
    REQUIRE_FALSE(pins.Release(777)); REQUIRE_FALSE(pins.valid());
    pins.Reset(); REQUIRE(pins.valid()); REQUIRE(pins.size() == 0);
    REQUIRE(pins.Retain(777)); REQUIRE(pins.Release(777));
  }
  REQUIRE(memory.live == 0);
}
#endif
