// The standalone inspector needs only these platform primitives, without the
// runtime memory allocator's global CVAR registration. The iOS build links the
// SDK's actual memory implementation instead.
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace rex::memory {
void copy_and_swap_16_unaligned(void* dest, const void* source, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    uint16_t v; std::memcpy(&v, static_cast<const uint8_t*>(source) + i * 2, 2);
    v = __builtin_bswap16(v); std::memcpy(static_cast<uint8_t*>(dest) + i * 2, &v, 2);
  }
}
void copy_and_swap_32_unaligned(void* dest, const void* source, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    uint32_t v; std::memcpy(&v, static_cast<const uint8_t*>(source) + i * 4, 4);
    v = __builtin_bswap32(v); std::memcpy(static_cast<uint8_t*>(dest) + i * 4, &v, 4);
  }
}
void copy_and_swap_16_in_32_unaligned(void* dest, const void* source, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    uint32_t v; std::memcpy(&v, static_cast<const uint8_t*>(source) + i * 4, 4);
    v = (v << 16) | (v >> 16); std::memcpy(static_cast<uint8_t*>(dest) + i * 4, &v, 4);
  }
}
}
