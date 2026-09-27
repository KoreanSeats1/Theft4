#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>
#include "stateful_constant_state.h"

namespace rex::graphics::gta4_native {

using NativeConstantMask = std::array<uint64_t, 4>;  // 256 float4 registers.
struct NativeConstantUsage {
  bool known = false;
  std::array<NativeConstantMask, 2> banks{};
  void Merge(const NativeConstantUsage& other) {
    known = known && other.known;
    for (size_t b = 0; b < 2; ++b)
      for (size_t i = 0; i < 4; ++i) banks[b][i] |= other.banks[b][i];
  }
};

// Recognizes only the shipped RawBufferLoad ABI: three uint64 push addresses,
// constant byte offsets, integer-to-pointer bitcasts, and direct scalar/vector loads. Analyze
// the final SPIR-V, including overrides, not guest declarations or debug names.
// Any unknown address, dynamic indexing, pointer escape, or unsupported access
// disables projection for this shader. No shader instructions are rewritten.
inline NativeConstantUsage ReflectNativeConstantUsage(std::span<const uint32_t> code) {
  NativeConstantUsage result;
  if (code.size() < 5 || code[0] != 0x07230203 || !code[3] || code[3] > 1'000'000)
    return result;
  struct Def { std::span<const uint32_t> w; };
  std::vector<Def> defs(code[3]);
  std::vector<std::span<const uint32_t>> instructions;
  std::vector<uint32_t> physical;
  std::vector<uint8_t> physical_types(code[3]);
  for (size_t p = 5; p < code.size();) {
    size_t n = code[p] >> 16;
    if (!n || n > code.size() - p) return {};
    auto w = code.subspan(p, n);
    instructions.push_back(w);
    const auto op = w[0] & 65535;
    size_t id_index = 0;
    switch (op) {
      case 21: case 22: case 23: case 30: case 32: id_index = 1; break;
      case 43: case 59: case 61: case 65: case 66: case 120: case 124: case 128: id_index = 2; break;
    }
    if (id_index) {
      if (n <= id_index || !w[id_index] || w[id_index] >= defs.size() || !defs[w[id_index]].w.empty()) return {};
      defs[w[id_index]].w = w;
    }
    if (op == 32 && n == 4 && w[2] == 5349) physical_types[w[1]] = 1;
    p += n;
  }
  const auto def = [&](uint32_t id) -> std::span<const uint32_t> {
    return id < defs.size() ? defs[id].w : std::span<const uint32_t>{};
  };
  const auto integer = [&](uint32_t id, uint64_t& value) {
    auto w = def(id);
    if (w.size() < 4 || (w[0] & 65535) != 43) return false;
    auto t = def(w[1]);
    if (t.size() != 4 || (t[0] & 65535) != 21 || (t[2] != 32 && t[2] != 64) ||
        w.size() != (t[2] == 64 ? 5u : 4u)) return false;
    value = w[3];
    if (t[2] == 64) value |= uint64_t(w[4]) << 32;
    return true;
  };
  // Address bank 2 is shared data, which is not projected in this round.
  struct Address { int bank = -1; uint64_t offset = 0; };
  std::function<Address(uint32_t, unsigned)> address = [&](uint32_t id, unsigned depth) -> Address {
    if (depth > 16) return {};
    auto w = def(id);
    if (w.size() == 5 && (w[0] & 65535) == 128) {
      uint64_t offset;
      uint32_t base;
      if (integer(w[4], offset)) base = w[3];
      else if (integer(w[3], offset)) base = w[4];
      else return {};
      auto a = address(base, depth + 1);
      if (a.bank < 0 || offset > UINT64_MAX - a.offset) return {};
      a.offset += offset;
      return a;
    }
    if (w.size() < 4 || (w[0] & 65535) != 61) return {};
    auto t = def(w[1]);
    if (t.size() != 4 || (t[0] & 65535) != 21 || t[2] != 64) return {};
    auto access = def(w[3]);
    if (access.size() != 5 || ((access[0] & 65535) != 65 && (access[0] & 65535) != 66)) return {};
    auto var = def(access[3]);
    if (var.size() != 4 || (var[0] & 65535) != 59 || var[3] != 9) return {};
    auto ptr = def(var[1]);
    if (ptr.size() != 4 || (ptr[0] & 65535) != 32 || ptr[2] != 9) return {};
    auto structure = def(ptr[3]);
    if (structure.size() != 5 || (structure[0] & 65535) != 30) return {};
    std::array<bool, 3> offsets{};
    for (unsigned m = 0; m < 3; ++m) {
      auto member = def(structure[m + 2]);
      if (member.size() != 4 || (member[0] & 65535) != 21 || member[2] != 64) return {};
    }
    for (auto d : instructions) {
      if ((d[0] & 65535) == 72 && d.size() == 5 && d[1] == ptr[3] && d[3] == 35) {
        if (d[2] >= 3 || d[4] != d[2] * 8) return {};
        offsets[d[2]] = true;
      }
    }
    uint64_t member;
    if (!offsets[0] || !offsets[1] || !offsets[2] || !integer(access[4], member) || member >= 3) return {};
    return {int(member), 0};
  };
  bool function_scope = false;
  bool has_entry = false;
  for (auto w : instructions) {
    const auto op = w[0] & 65535;
    if (op == 15) has_entry = true;
    if (op == 54) function_scope = true;
    if (op == 56) function_scope = false;
    if (!function_scope) continue;
    if (w.size() >= 3 && w[1] < physical_types.size() && physical_types[w[1]]) {
      // No Phi, function parameter, select, pointer arithmetic, or alias.
      if ((op != 120 && op != 124) || w.size() != 4) return {};
      physical.push_back(w[2]);
    }
  }
  for (uint32_t id : physical) {
    auto conversion = def(id);
    auto a = address(conversion[3], 0);
    if (a.bank < 0) return {};
    auto pointer_type = def(conversion[1]);
    bool in_function = false;
    for (auto w : instructions) {
      const auto op = w[0] & 65535;
      if (op == 54) in_function = true;
      if (op == 56) in_function = false;
      if (!in_function || op == 8 || op == 317) continue; // Debug line metadata.
      for (size_t operand = 1; operand < w.size(); ++operand) {
        if (w[operand] != id) continue;
        if ((op == 120 || op == 124) && operand == 2) continue;
        if (op != 61 || operand != 3 || w.size() < 4 || w[1] != pointer_type[3]) return {};
        if (a.bank == 2) continue;
        auto t = def(w[1]);
        uint64_t components = 1;
        if (t.size() == 4 && (t[0] & 65535) == 23) { components = t[3]; t = def(t[2]); }
        if (t.size() < 3 || ((t[0] & 65535) != 21 && (t[0] & 65535) != 22) ||
            t[2] != 32 || !components || components > 4) return {};
        const uint64_t size = components * 4, bound = a.bank == 0 ? 4096 : 3584;
        if (a.offset >= bound || size > bound - a.offset) return {};
        for (uint64_t reg = a.offset / 16; reg <= (a.offset + size - 1) / 16; ++reg)
          result.banks[a.bank][reg / 64] |= uint64_t(1) << (reg % 64);
      }
    }
  }
  // No physical addresses is valid for an entirely constant-free shader.
  result.known = has_entry;
  return result;
}

// The allocation remains immutable. Only a shader-specific view may reuse it;
// it must never be inserted into the full-bank version cache under a new ID.
inline bool CanReuseConstantProjection(const ConstantStateVersion* previous,
    const ConstantStateVersion* current, const NativeConstantMask& mask) {
  if (!previous || !current || previous->byte_size != current->byte_size) return false;
  for (unsigned depth = 0; depth <= 8; ++depth) {
    if (current == previous) return true;
    if (!current || current->delta.complete_snapshot || !current->parent ||
        !ValidateConstantPayloadDelta(current->delta, current->byte_size)) return false;
    for (const auto& r : current->delta.ranges) {
      for (uint32_t reg = r.destination_offset / 16;
           reg <= (r.destination_offset + r.byte_count - 1) / 16; ++reg)
        if (reg >= 256 || (mask[reg / 64] & (uint64_t(1) << (reg % 64)))) return false;
    }
    current = current->parent.get();
  }
  return false;
}

}  // namespace rex::graphics::gta4_native
