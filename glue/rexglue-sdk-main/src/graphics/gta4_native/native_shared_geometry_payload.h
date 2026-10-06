#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace rex::graphics::gta4_native {
// Publish converted geometry by transferring its allocation, rather than
// copying the whole buffer into another CPU draw packet. Const shared owners
// keep the allocation alive after CPU-cache eviction and GPU submission.
template<class Bytes>
std::shared_ptr<const Bytes> PublishNativeGeometryPayload(std::vector<uint8_t>& payload,
    uint64_t generation,std::array<uint64_t,4> conversion) {
  if(payload.empty()||!generation)return {};
  auto bytes=std::make_shared<Bytes>();
  bytes->generation=generation;bytes->conversion=conversion;
  bytes->value.swap(payload);
  return bytes;
}
} // namespace rex::graphics::gta4_native
