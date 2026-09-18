#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstring>
#include <memory>
#include <type_traits>

#include "graphics/gta4_native/native_fixed_state.h"
#include "graphics/gta4_native/native_inline_bytes.h"
#include "graphics/gta4_native/native_prepared_bindings.h"
#include <rex/graphics/gta4_native/title_commands.h>

using namespace rex::graphics::gta4_native;
using FixedState = NativeFixedFunctionStateStorage<4>;

static_assert(sizeof(DrawPrimitiveCommand) <= 192);
static_assert(sizeof(DrawPrimitiveUpCommand) <= 192);
static_assert(sizeof(DrawIndexedPrimitiveCommand) <= 192);
static_assert(sizeof(ClearCommand) <= 192);
static_assert(sizeof(ResolveCommand) <= 192);
static_assert(sizeof(PresentCommand) <= 192);
static_assert(sizeof(UpdateEnvironmentalDataCommand) > 192);

TEST_CASE("hot command bytes remain inline across copies and moves") {
  NativeInlineBytes<192> bytes;
  bytes.resize(sizeof(DrawIndexedPrimitiveCommand));
  REQUIRE(bytes.uses_inline_storage());
  REQUIRE(bytes.heap_capacity() == 0);
  for (size_t i = 0; i < bytes.size(); ++i) bytes.data()[i] = uint8_t(i);

  const NativeInlineBytes<192> copied = bytes;
  NativeInlineBytes<192> moved = std::move(bytes);
  REQUIRE(moved.uses_inline_storage());
  REQUIRE(bytes.size() == 0);
  REQUIRE(std::memcmp(moved.data(), copied.data(), moved.size()) == 0);
}

TEST_CASE("large command bytes use and safely leave overflow storage") {
  NativeInlineBytes<192> bytes;
  bytes.resize(sizeof(UpdateEnvironmentalDataCommand));
  REQUIRE_FALSE(bytes.uses_inline_storage());
  for (size_t i = 0; i < bytes.size(); ++i) bytes.data()[i] = uint8_t(i * 17u);
  bytes.resize(sizeof(PresentCommand));
  REQUIRE(bytes.uses_inline_storage());
  REQUIRE(bytes.heap_capacity() == 0);
  for (size_t i = 0; i < bytes.size(); ++i) REQUIRE(bytes.data()[i] == uint8_t(i * 17u));
}

TEST_CASE("fixed-state fingerprint covers fields and ignores object padding") {
  FixedState first;
  FixedState second;
  std::memset(&first, 0xA5, sizeof(first));
  std::memset(&second, 0x5A, sizeof(second));
  VisitNativeFixedStateFields(first, [](auto& field) { field = {}; });
  VisitNativeFixedStateFields(second, [](auto& field) { field = {}; });
  REQUIRE(NativeFixedStateBytes(first) == NativeFixedStateBytes(second));
  REQUIRE(HashNativeFixedFunctionState(first) == HashNativeFixedFunctionState(second));

  const auto original_hash = HashNativeFixedFunctionState(first);
  size_t fields = 0;
  VisitNativeFixedStateFields(first, [&](auto& field) {
    ++fields;
    using Field = std::remove_cvref_t<decltype(field)>;
    if constexpr (std::is_same_v<Field, uint32_t>) field ^= 1u;
  });
  REQUIRE(fields == 49);
  REQUIRE(HashNativeFixedFunctionState(first) != original_hash);
}

namespace {
struct Pipeline { std::array<uint32_t, 16> textures{}; };
struct Draw {
  std::shared_ptr<Pipeline> pipeline_state = std::make_shared<Pipeline>();
  std::array<std::shared_ptr<int>, 16> textures{};
  std::array<std::array<uint32_t, 6>, 16> texture_fetches{};
  uint32_t used_texture_mask = 1;
  uint32_t failed_texture_mask = 0;
  bool bindings_prepared = true;
};
}

TEST_CASE("prepared-binding memo verifies every hinted hit") {
  NativePreparedBindingMemo<Draw, 1> memo;
  Draw first;
  first.textures[0] = std::make_shared<int>(1);
  memo.Remember(first);
  Draw match = first;
  REQUIRE(memo.Find(match) == &first);

  match.texture_fetches[0][3] ^= 1;
  REQUIRE(memo.Find(match) == nullptr);
  match = first;
  match.textures[0] = std::make_shared<int>(1);
  REQUIRE(memo.Find(match) == nullptr);
}
