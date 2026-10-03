#include <catch2/catch_test_macros.hpp>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include "graphics/gta4_native/native_command_packet.h"
#include "graphics/gta4_native/native_command_recycler.h"
#include "graphics/gta4_native/native_worker_batch.h"

using namespace rex::graphics::gta4_native;

TEST_CASE("Compact state validation retains side-effectful and malformed commands", "[graphics][compact-packets]") {
  const auto valid = []<typename T>(T command) {
    REQUIRE_FALSE(IsCompactNativeStateCommand(&command, sizeof(command)));
    command.device = 1;
    REQUIRE(IsCompactNativeStateCommand(&command, sizeof(command)));
    REQUIRE_FALSE(IsCompactNativeStateCommand(&command, sizeof(command) - 1));
    ++command.header.size;
    REQUIRE_FALSE(IsCompactNativeStateCommand(&command, sizeof(command)));
  };
  valid(SetTextureCommand{}); valid(SetVertexStreamCommand{});
  valid(SetIndexBufferCommand{}); valid(SetRenderTargetCommand{});
  valid(SetDepthStencilCommand{});
  SetShaderCommand shader{}; shader.header = {sizeof(shader), CommandType::kSetPixelShader};
  valid(shader); shader.header.type = CommandType::kSetVertexShader; valid(shader);
  valid(SetVertexDeclarationCommand{});
  SetTextureCommand texture{}; texture.device = 1;
  texture.stage = kTextureStageCount;
  REQUIRE_FALSE(IsCompactNativeStateCommand(&texture, sizeof(texture)));
  texture.stage = 0; texture.vector_font_id = 1;
  REQUIRE_FALSE(IsCompactNativeStateCommand(&texture, sizeof(texture)));
  SetVertexStreamCommand stream{}; stream.device = 1; stream.stream = kVertexStreamCount;
  REQUIRE_FALSE(IsCompactNativeStateCommand(&stream, sizeof(stream)));
  SetRenderTargetCommand target{}; target.device = 1; target.index = kRenderTargetCount;
  REQUIRE_FALSE(IsCompactNativeStateCommand(&target, sizeof(target)));
  DrawPrimitiveCommand draw{}; draw.device = 1;
  REQUIRE_FALSE(IsCompactNativeStateCommand(&draw, sizeof(draw)));
  REQUIRE_FALSE(IsCompactNativeStateCommand(nullptr, sizeof(texture)));
}

namespace {
struct FullPacket {
  static inline unsigned constructed = 0;
  std::array<unsigned char, 3000> draw_state{};
  uint64_t sequence = 0;
  CommandType type = CommandType::kDrawPrimitive;
  std::shared_ptr<unsigned> resource;
  FullPacket() { ++constructed; }
};
}

namespace {
NativeStateCommand BindingEntry(uint64_t sequence, uint32_t epoch = 1) {
  SetIndexBufferCommand binding{};
  binding.device = 1;
  binding.buffer = uint32_t(sequence);
  NativeStateCommand state;
  state.sequence = sequence;
  state.epoch = epoch;
  state.type = binding.header.type;
  state.size = sizeof(binding);
  std::memcpy(state.bytes.data(), &binding, sizeof(binding));
  return state;
}
void CheckBinding(const NativeStateCommand& state, uint64_t expected) {
  REQUIRE(state.sequence == expected);
  REQUIRE(state.type == CommandType::kSetIndexBuffer);
  REQUIRE(state.size == sizeof(SetIndexBufferCommand));
  SetIndexBufferCommand binding{};
  std::memcpy(&binding, state.bytes.data(), sizeof(binding));
  REQUIRE(binding.device == 1);
  REQUIRE(binding.buffer == expected);
}
}

TEST_CASE("Mixed packet transport preserves FIFO owners without constructing draw packets for state", "[graphics][compact-packets]") {
  using Packet = NativeCommandPacket<FullPacket>;
  NativeCommandRecycler<NativeStatePacket, 8, 32> state_pool;
  NativeCommandRecycler<FullPacket, 8, 32> full_pool;
  NativeWorkerBatch<Packet, 16> batch;
  NativeCommandQueue<FullPacket> queue;
  std::vector<std::weak_ptr<unsigned>> resources;
  FullPacket::constructed = 0;
  unsigned expected_full = 0;
  for (unsigned sequence = 1; sequence <= 1000; ++sequence) {
    if (sequence % 4) {
      auto entry = BindingEntry(sequence);
      if (!queue.TryAppendState(entry)) {
        auto state = state_pool.Acquire();
        REQUIRE(state->TryAppend(entry));
        queue.push_back(Packet(std::move(state)));
      }
    } else {
      auto full = full_pool.Acquire(); full->sequence = sequence;
      full->resource = std::make_shared<unsigned>(sequence);
      resources.push_back(full->resource); ++expected_full;
      queue.push_back(Packet(std::move(full)));
    }
  }
  REQUIRE(FullPacket::constructed == expected_full);
  REQUIRE(queue.size() == 1000);
  REQUIRE(queue.packet_count() == 500);
  uint64_t expected = 1;
  while (!queue.empty()) {
    const size_t staged = queue.TransferTo(batch, batch.capacity());
    REQUIRE(staged > 0);
    REQUIRE(staged <= batch.capacity());
    while (!batch.empty()) {
      auto packet = batch.take_front();
      if (auto* state = packet.State()) {
        REQUIRE_FALSE(NativeQueueHasResources(packet));
        for (size_t i = 0; i < state->size(); ++i) CheckBinding((*state)[i], expected++);
        state_pool.Recycle(packet.TakeState());
      } else {
        REQUIRE(NativeQueueHasResources(packet));
        REQUIRE(NativeQueueCommand(packet).sequence == expected++);
        full_pool.Recycle(packet.TakeFull());
      }
    }
  }
  REQUIRE(expected == 1001);
  REQUIRE(queue.size() == 0);
  for (const auto& resource : resources) REQUIRE(resource.expired());
  REQUIRE(state_pool.SharedSize() <= 32);
  REQUIRE(full_pool.SharedSize() <= 32);
  auto reused = state_pool.Acquire();
  REQUIRE(reused->size() == 0);
  REQUIRE_FALSE(reused->HasMeasuredEntries());
  auto clean = BindingEntry(1);
  REQUIRE(clean.transport.enqueued == 0);
  REQUIRE(reused->TryAppend(clean));
  REQUIRE((*reused)[0].transport.enqueued == 0);
  REQUIRE(sizeof(NativeStateCommand) < sizeof(FullPacket) / 8);
  REQUIRE(sizeof(NativeStatePacket) < sizeof(FullPacket));
}

TEST_CASE("State batch capacity epochs and transferred ownership bound logical commands", "[graphics][compact-packets]") {
  using Packet = NativeCommandPacket<FullPacket>;
  NativeCommandQueue<FullPacket> queue;
  auto state = std::make_unique<NativeStatePacket>();
  REQUIRE(state->TryAppend(BindingEntry(1)));
  queue.push_back(Packet(std::move(state)));
  for (size_t i = 2; i <= NativeStatePacket::kCapacity; ++i) {
    REQUIRE(queue.CanAccept(true, NativeStatePacket::kCapacity, 0));
    REQUIRE(queue.TryAppendState(BindingEntry(i)));
  }
  REQUIRE(queue.size() == NativeStatePacket::kCapacity);
  REQUIRE(queue.packet_count() == 1);
  REQUIRE_FALSE(queue.CanAccept(true, NativeStatePacket::kCapacity, 0));
  REQUIRE_FALSE(queue.TryAppendState(BindingEntry(9)));
  REQUIRE_FALSE(queue.CanAccept(false, 100, 0));
  REQUIRE_FALSE(queue.CanAccept(true, 100, 2));
  auto transferred = queue.take_front();
  REQUIRE(queue.empty());
  REQUIRE(queue.size() == 0);
  REQUIRE_FALSE(queue.TryAppendState(BindingEntry(9)));
  auto next = std::make_unique<NativeStatePacket>();
  REQUIRE(next->TryAppend(BindingEntry(9, 2)));
  queue.push_back(Packet(std::move(next)));
  REQUIRE_FALSE(queue.TryAppendState(BindingEntry(10, 3)));
  REQUIRE(queue.TryAppendState(BindingEntry(10, 2)));
  REQUIRE(transferred.State()->size() == NativeStatePacket::kCapacity);
  for (size_t i = 0; i < transferred.State()->size(); ++i)
    CheckBinding((*transferred.State())[i], i + 1);
  REQUIRE(queue.size() == 2);
  queue.clear();
  REQUIRE(queue.empty());
  REQUIRE(queue.size() == 0);
}

TEST_CASE("Every full packet ends a state run including resource present and synchronous boundaries", "[graphics][compact-packets]") {
  using Packet = NativeCommandPacket<FullPacket>;
  const CommandType boundaries[] = {
      CommandType::kDrawPrimitive, CommandType::kDrawIndexedPrimitive,
      CommandType::kDrawPrimitiveUp, CommandType::kClear, CommandType::kResolve,
      CommandType::kRegisterShader, CommandType::kRegisterVertexDeclaration,
      CommandType::kRegisterVirtualResource, CommandType::kReleaseResource,
      CommandType::kResourceUnlock, CommandType::kTextureLock, CommandType::kPresent,
      CommandType::kDeviceDestroyed};
  for (const auto boundary : boundaries) {
    NativeCommandQueue<FullPacket> queue;
    auto before = std::make_unique<NativeStatePacket>();
    REQUIRE(before->TryAppend(BindingEntry(1)));
    queue.push_back(Packet(std::move(before)));
    REQUIRE(queue.TryAppendState(BindingEntry(2)));
    auto full = std::make_unique<FullPacket>();
    full->type = boundary; full->sequence = 3;
    full->resource = std::make_shared<unsigned>(3);
    std::weak_ptr<unsigned> resource = full->resource;
    queue.push_back(Packet(std::move(full)));
    REQUIRE_FALSE(queue.TryAppendState(BindingEntry(4)));
    auto after = std::make_unique<NativeStatePacket>();
    REQUIRE(after->TryAppend(BindingEntry(4)));
    queue.push_back(Packet(std::move(after)));
    REQUIRE(queue.size() == 4);
    REQUIRE(queue.packet_count() == 3);
    auto first = queue.take_front();
    CheckBinding((*first.State())[0], 1); CheckBinding((*first.State())[1], 2);
    REQUIRE(queue.size() == 2);
    {
      auto middle = queue.take_front();
      REQUIRE(NativeQueueHasResources(middle));
      REQUIRE(middle.FullCommand()->type == boundary);
      REQUIRE(middle.FullCommand()->sequence == 3);
      REQUIRE_FALSE(resource.expired());
    }
    REQUIRE(resource.expired());
    auto last = queue.take_front(); CheckBinding((*last.State())[0], 4);
    REQUIRE(queue.size() == 0);
  }
}

TEST_CASE("Stopped producer does not append and queued state remains available to drain", "[graphics][compact-packets]") {
  using Packet = NativeCommandPacket<FullPacket>;
  NativeCommandQueue<FullPacket> queue;
  auto packet = std::make_unique<NativeStatePacket>();
  REQUIRE(packet->TryAppend(BindingEntry(1)));
  queue.push_back(Packet(std::move(packet)));
  std::mutex mutex;
  std::condition_variable condition;
  bool running = true, entered = false, accepted = false;
  std::thread producer([&] {
    std::unique_lock lock(mutex);
    entered = true; condition.notify_all();
    condition.wait(lock, [&] { return !running || queue.CanAccept(running, 1, 0); });
    if (!running) return; // Same post-wait shutdown gate as SubmitTitleCommand.
    accepted = queue.TryAppendState(BindingEntry(2));
  });
  {
    std::unique_lock lock(mutex);
    condition.wait(lock, [&] { return entered; });
    running = false;
  }
  condition.notify_all(); producer.join();
  REQUIRE_FALSE(accepted);
  REQUIRE(queue.size() == 1);
  auto remaining = queue.take_front(); CheckBinding((*remaining.State())[0], 1);
  REQUIRE(queue.empty());
}

TEST_CASE("State batch metrics retain every logical command and count saved packets", "[graphics][compact-packets]") {
  profile::TransportSummary summary;
  for (size_t i = 0; i < NativeStatePacket::kCapacity; ++i) {
    profile::CommandTransport transport;
    transport.enqueued = 100 + i;
    transport.compact_state = true;
    transport.state_batch_append = i != 0;
    transport.reused_storage = i != 0;
    summary.Observe(transport, 200, NativeStatePacket::kCapacity - i, i + 1);
  }
  REQUIRE(summary.commands == NativeStatePacket::kCapacity);
  REQUIRE(summary.measured_commands == NativeStatePacket::kCapacity);
  REQUIRE(summary.compact_state_commands == NativeStatePacket::kCapacity);
  REQUIRE(summary.state_batch_packets == 1);
  REQUIRE(summary.state_batch_appends == NativeStatePacket::kCapacity - 1);
  REQUIRE(summary.queue_peak == NativeStatePacket::kCapacity);
  REQUIRE(summary.first_sequence == 1);
  REQUIRE(summary.last_sequence == NativeStatePacket::kCapacity);
}

TEST_CASE("Logical worker transfer leaves a whole tail packet queued without exceeding the limit", "[graphics][compact-packets]") {
  using Packet = NativeCommandPacket<FullPacket>;
  NativeCommandQueue<FullPacket> queue;
  NativeWorkerBatch<Packet, 8> batch;
  for (uint64_t i = 1; i <= 7; ++i) {
    auto full = std::make_unique<FullPacket>(); full->sequence = i;
    queue.push_back(Packet(std::move(full)));
  }
  auto states = std::make_unique<NativeStatePacket>();
  for (uint64_t i = 8; i <= 15; ++i) REQUIRE(states->TryAppend(BindingEntry(i)));
  queue.push_back(Packet(std::move(states)));
  REQUIRE(queue.size() == 15);
  REQUIRE(queue.TransferTo(batch, 8) == 7);
  REQUIRE(queue.size() == 8);
  REQUIRE(queue.packet_count() == 1);
  for (uint64_t i = 1; i <= 7; ++i) {
    auto full = batch.take_front(); REQUIRE(full.FullCommand()->sequence == i);
  }
  REQUIRE(batch.empty());
  REQUIRE(queue.TransferTo(batch, 8) == 8);
  REQUIRE(queue.empty());
  REQUIRE(batch.size() == 1);
  auto tail = batch.take_front();
  for (size_t i = 0; i < tail.State()->size(); ++i) CheckBinding((*tail.State())[i], i + 8);
}

TEST_CASE("Only active state entries are constructed and measured flags reset with recycling", "[graphics][compact-packets]") {
  NativeCommandRecycler<NativeStatePacket, 1, 1> pool;
  auto packet = pool.Acquire();
  REQUIRE(packet->size() == 0);
  REQUIRE_FALSE(packet->HasMeasuredEntries());
  auto entry = BindingEntry(1);
  entry.transport.enqueued = 100;
  REQUIRE(packet->TryAppend(entry));
  REQUIRE(packet->HasMeasuredEntries());
  pool.Recycle(std::move(packet));
  bool reused = false;
  auto next = pool.Acquire(&reused);
  REQUIRE(reused);
  REQUIRE(next->size() == 0);
  REQUIRE_FALSE(next->HasMeasuredEntries());
  REQUIRE(next->TryAppend(BindingEntry(2)));
  REQUIRE_FALSE(next->HasMeasuredEntries());
  CheckBinding((*next)[0], 2);
}
