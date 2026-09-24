#include "native_constant_projection.h"
#include <cassert>
#include <iostream>
#include <random>
using namespace rex::graphics::gta4_native;

static std::vector<uint32_t> Shader(uint32_t bank, uint32_t offset) {
  std::vector<uint32_t> c{0x07230203, 0x00010500, 0, 100, 0};
  auto op = [&](uint32_t code, std::initializer_list<uint32_t> args) {
    c.push_back((uint32_t(args.size() + 1) << 16) | code); c.insert(c.end(), args);
  };
  op(15, {4, 50, 0});
  op(21, {1, 64, 0}); op(21, {2, 32, 0}); op(22, {3, 32});
  op(23, {4, 3, 4}); op(30, {5, 1, 1, 1}); op(32, {6, 9, 5});
  op(32, {7, 9, 1}); op(32, {8, 5349, 4});
  op(72, {5, 0, 35, 0}); op(72, {5, 1, 35, 8}); op(72, {5, 2, 35, 16});
  op(43, {2, 9, bank}); op(43, {1, 10, offset, 0}); op(59, {6, 11, 9});
  op(54, {2, 50, 0, 60}); op(248, {51});
  op(65, {7, 12, 11, 9}); op(61, {1, 13, 12});
  op(128, {1, 14, 13, 10}); op(120, {8, 15, 14}); op(61, {4, 16, 15});
  op(253, {}); op(56, {});
  return c;
}
int main() {
  auto c = Shader(0, 32);
  auto u = ReflectNativeConstantUsage(c);
  assert(u.known && u.banks[0][0] == 4 && u.banks[1][0] == 0);
  auto p = ReflectNativeConstantUsage(Shader(1, 48));
  u.Merge(p); assert(u.known && u.banks[1][0] == 8);
  assert(!ReflectNativeConstantUsage(Shader(1, 3584)).known);
  assert(!ReflectNativeConstantUsage(Shader(0, 4090)).known);
  assert(ReflectNativeConstantUsage(Shader(2, 752)).known);
  auto replace = [](auto& code, uint32_t op, unsigned operand, uint32_t value) {
    for (size_t i = 5; i < code.size(); i += code[i] >> 16)
      if ((code[i] & 65535) == op) { code[i + operand] = value; return; }
  };
  auto bitcast = Shader(0, 32);
  replace(bitcast, 120, 0, (4u << 16) | 124);
  assert(ReflectNativeConstantUsage(bitcast).banks == ReflectNativeConstantUsage(Shader(0, 32)).banks);
  replace(c, 128, 4, 70); // Unknown/dynamic offset.
  assert(!ReflectNativeConstantUsage(c).known);
  c = Shader(0, 0); replace(c, 61, 1, 2); // Wrong push load width.
  assert(!ReflectNativeConstantUsage(c).known);
  c = Shader(0, 0); c.insert(c.end() - 2, {(3u << 16) | 62u, 15, 16});
  assert(!ReflectNativeConstantUsage(c).known); // Pointer write/escape.
  c = Shader(0, 0); c[5] = 0;
  assert(!ReflectNativeConstantUsage(c).known);
  auto previous = std::make_shared<ConstantStateVersion>(); previous->byte_size = 4096;
  auto current = std::make_shared<ConstantStateVersion>(); current->byte_size = 4096;
  current->parent = previous; current->delta.ranges = {{48, 0, 16}};
  current->delta.payload.resize(16);
  NativeConstantMask mask{4, 0, 0, 0}; // Shader uses register 2, delta changes 3.
  assert(CanReuseConstantProjection(previous.get(), current.get(), mask));
  current->delta.ranges[0].destination_offset = 47; // Crosses register boundary.
  assert(!CanReuseConstantProjection(previous.get(), current.get(), mask));
  current->delta.ranges[0].destination_offset = 48;
  current->delta.complete_snapshot = true;
  assert(!CanReuseConstantProjection(previous.get(), current.get(), mask));
  assert(!CanReuseConstantProjection(nullptr, current.get(), mask));
  // Differential check: every accepted reuse must equal the canonical bytes
  // for every register in the mask, including multi-version gaps and resets.
  std::mt19937 random(42);
  AuthoritativeConstantState state(4096);
  std::vector<uint8_t> initial(4096);
  ConstantPayloadDelta delta;
  CaptureCompleteConstantSnapshot(initial, delta);
  auto hash = [](std::span<const uint8_t>) { return uint64_t(1); };
  auto base = state.Apply(delta, hash).version;
  auto saved = initial;
  for (unsigned iteration = 0; iteration < 20000; ++iteration) {
    uint32_t reg = random() % 256;
    delta.complete_snapshot = false;
    delta.ranges = {{reg * 16, 0, 16}};
    delta.payload.assign(16, uint8_t(random()));
    auto v = state.Apply(delta, hash).version;
    if (CanReuseConstantProjection(base.get(), v.get(), mask)) {
      for (unsigned r = 0; r < 256; ++r)
        if (mask[r / 64] & (uint64_t(1) << (r % 64)))
          assert(std::memcmp(saved.data() + r * 16, state.canonical().data() + r * 16, 16) == 0);
    }
    if (iteration % 5 == 0) { base = v; saved = state.canonical(); }
    if (iteration % 17 == 0) AuthoritativeConstantState::MaterializeView(v);
  }
  std::cout << "constant projection tests passed\n";
}
