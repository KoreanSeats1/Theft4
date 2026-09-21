#include <catch2/catch_test_macros.hpp>
#include <array>
#include <random>
#include "graphics/gta4_native/native_producer_binding_cache.h"

using namespace rex::graphics::gta4_native;

TEST_CASE("Producer binding reuse requires a successfully queued identical command", "[graphics][producer-bindings]") {
  NativeProducerBindingCache cache;
  SetTextureCommand texture{};
  texture.device = 1; texture.stage = 2; texture.texture = 100;
  REQUIRE_FALSE(cache.CanSkip(&texture, sizeof(texture)));
  REQUIRE_FALSE(cache.CanSkip(&texture, sizeof(texture))); // observing is not committing
  cache.RememberQueued(&texture, sizeof(texture));
  REQUIRE(cache.CanSkip(&texture, sizeof(texture)));
  auto changed = texture; changed.device = 2;
  REQUIRE_FALSE(cache.CanSkip(&changed, sizeof(changed)));
  changed = texture; changed.texture = 101;
  REQUIRE_FALSE(cache.CanSkip(&changed, sizeof(changed)));
  changed = texture; changed.stage = kTextureStageCount;
  REQUIRE_FALSE(cache.CanSkip(&changed, sizeof(changed)));
  REQUIRE_FALSE(cache.CanSkip(&texture, sizeof(texture))); // malformed boundary invalidates
  cache.RememberQueued(&texture, sizeof(texture));
  changed = texture; changed.header.size--;
  REQUIRE_FALSE(cache.CanSkip(&changed, sizeof(changed)));
  cache.RememberQueued(&texture, sizeof(texture));
  REQUIRE_FALSE(cache.CanSkip(&texture, sizeof(texture) - 1));
  cache.RememberQueued(&texture, sizeof(texture));
  changed = texture; changed.vector_font_id = 1;
  REQUIRE_FALSE(cache.CanSkip(&changed, sizeof(changed)));
  cache.RememberQueued(&changed, sizeof(changed));
  REQUIRE_FALSE(cache.CanSkip(&changed, sizeof(changed))); // retain font side effects
}

TEST_CASE("Binding reuse preserves draw state and invalidates at resource and frame boundaries", "[graphics][producer-bindings]") {
  NativeProducerBindingCache cache;
  std::array<uint32_t, kTextureStageCount> reference{}, optimized{};
  std::mt19937 random(39);
  unsigned skipped = 0;
  for (unsigned iteration = 0; iteration < 20000; ++iteration) {
    SetTextureCommand texture{};
    texture.device = 1; texture.stage = random() % kTextureStageCount;
    texture.texture = random() % 3;
    reference[texture.stage] = texture.texture;
    if (cache.CanSkip(&texture, sizeof(texture))) ++skipped;
    else {
      optimized[texture.stage] = texture.texture;
      cache.RememberQueued(&texture, sizeof(texture));
    }
    DrawIndexedPrimitiveCommand draw{}; draw.device = 1;
    REQUIRE_FALSE(cache.CanSkip(&draw, sizeof(draw)));
    REQUIRE(reference == optimized);
    if (iteration % 101 == 0) {
      ReleaseResourceCommand release{}; release.resource = texture.texture;
      REQUIRE_FALSE(cache.CanSkip(&release, sizeof(release)));
      REQUIRE_FALSE(cache.CanSkip(&texture, sizeof(texture)));
      cache.RememberQueued(&texture, sizeof(texture));
    }
    if (iteration % 503 == 0) {
      PresentCommand present{}; present.device = 1;
      REQUIRE_FALSE(cache.CanSkip(&present, sizeof(present)));
      REQUIRE_FALSE(cache.CanSkip(&texture, sizeof(texture)));
    }
  }
  REQUIRE(skipped > 1000);
  SetShaderCommand shader{}; shader.device = 1;
  shader.header = {sizeof(shader), CommandType::kSetPixelShader};
  REQUIRE_FALSE(cache.CanSkip(&shader, sizeof(shader)));
  cache.RememberQueued(&shader, sizeof(shader));
  REQUIRE_FALSE(cache.CanSkip(&shader, sizeof(shader)));
}

TEST_CASE("Producer cache compares all binding fields and keeps independent slots", "[graphics][producer-bindings]") {
  NativeProducerBindingCache cache;
  SetVertexStreamCommand stream{};
  stream.device = 1; stream.stream = 3; stream.buffer = 8; stream.stride = 16;
  cache.RememberQueued(&stream, sizeof(stream));
  REQUIRE(cache.CanSkip(&stream, sizeof(stream)));
  for (unsigned field = 0; field < 4; ++field) {
    auto changed = stream;
    switch (field) {
      case 0: ++changed.buffer; break;
      case 1: ++changed.offset; break;
      case 2: ++changed.stride; break;
      case 3: ++changed.stride_words; break;
    }
    REQUIRE_FALSE(cache.CanSkip(&changed, sizeof(changed)));
  }
  SetIndexBufferCommand index{}; index.device = 1; index.buffer = 99;
  cache.RememberQueued(&index, sizeof(index));
  REQUIRE(cache.CanSkip(&stream, sizeof(stream)));
  REQUIRE(cache.CanSkip(&index, sizeof(index)));
  SetRenderTargetCommand target{};
  target.device = 1; target.index = 2; target.surface.handle = 77;
  cache.RememberQueued(&target, sizeof(target));
  auto changed_target = target; ++changed_target.surface.width;
  REQUIRE_FALSE(cache.CanSkip(&changed_target, sizeof(changed_target)));
  REQUIRE(cache.CanSkip(&target, sizeof(target)));
  SetDepthStencilCommand depth{};
  depth.device = 1; depth.surface.handle = 44; depth.trace_caller = 22;
  cache.RememberQueued(&depth, sizeof(depth));
  auto changed_depth = depth; ++changed_depth.trace_caller;
  REQUIRE_FALSE(cache.CanSkip(&changed_depth, sizeof(changed_depth)));
  REQUIRE(cache.CanSkip(&depth, sizeof(depth)));
  cache.Reset();
  REQUIRE_FALSE(cache.CanSkip(&stream, sizeof(stream)));
  REQUIRE_FALSE(cache.CanSkip(&index, sizeof(index)));
  REQUIRE_FALSE(cache.CanSkip(&target, sizeof(target)));
  REQUIRE_FALSE(cache.CanSkip(&depth, sizeof(depth)));
}
