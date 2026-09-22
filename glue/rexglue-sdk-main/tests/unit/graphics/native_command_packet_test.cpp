#include <catch2/catch_test_macros.hpp>
#include <deque>
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
  std::shared_ptr<unsigned> resource;
  FullPacket() { ++constructed; }
};
}

TEST_CASE("Mixed packet transport preserves FIFO owners without constructing draw packets for state", "[graphics][compact-packets]") {
  using Packet = NativeCommandPacket<FullPacket>;
  NativeCommandRecycler<NativeStatePacket, 8, 32> state_pool;
  NativeCommandRecycler<FullPacket, 8, 32> full_pool;
  NativeWorkerBatch<Packet, 16> batch;
  std::deque<Packet> queue;
  std::vector<std::weak_ptr<unsigned>> resources;
  FullPacket::constructed = 0;
  unsigned expected_full = 0;
  for (unsigned sequence = 1; sequence <= 1000; ++sequence) {
    if (sequence % 4) {
      auto state = state_pool.Acquire(); state->sequence = sequence;
      SetIndexBufferCommand binding{}; binding.device = 1; binding.buffer = sequence;
      REQUIRE(IsCompactNativeStateCommand(&binding, sizeof(binding)));
      state->type = binding.header.type; state->size = sizeof(binding);
      std::memcpy(state->bytes.data(), &binding, sizeof(binding));
      queue.emplace_back(std::move(state));
    } else {
      auto full = full_pool.Acquire(); full->sequence = sequence;
      full->resource = std::make_shared<unsigned>(sequence);
      resources.push_back(full->resource); ++expected_full;
      queue.emplace_back(std::move(full));
    }
  }
  REQUIRE(FullPacket::constructed == expected_full);
  uint64_t expected = 1;
  while (!queue.empty()) {
    while (!queue.empty() && batch.size() < batch.capacity()) {
      batch.push_back(std::move(queue.front())); queue.pop_front();
    }
    while (!batch.empty()) {
      auto packet = batch.take_front();
      if (auto* state = packet.State()) {
        REQUIRE_FALSE(NativeQueueHasResources(packet));
        REQUIRE(state->sequence == expected);
        SetIndexBufferCommand binding{};
        std::memcpy(&binding, state->bytes.data(), sizeof(binding));
        REQUIRE(binding.buffer == expected);
        state_pool.Recycle(packet.TakeState());
      } else {
        REQUIRE(NativeQueueHasResources(packet));
        REQUIRE(NativeQueueCommand(packet).sequence == expected);
        full_pool.Recycle(packet.TakeFull());
      }
      ++expected;
    }
  }
  REQUIRE(expected == 1001);
  for (const auto& resource : resources) REQUIRE(resource.expired());
  REQUIRE(state_pool.SharedSize() <= 32);
  REQUIRE(full_pool.SharedSize() <= 32);
  auto reused = state_pool.Acquire();
  REQUIRE(reused->sequence == 0);
  REQUIRE(reused->size == 0);
  REQUIRE(reused->transport.enqueued == 0);
  REQUIRE(sizeof(NativeStatePacket) < sizeof(FullPacket) / 8);
}
