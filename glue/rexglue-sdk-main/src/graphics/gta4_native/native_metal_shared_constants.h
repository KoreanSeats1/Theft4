#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace rex::graphics::gta4_native {
// Exact inputs to the direct Metal shared bank. Descriptor indices are always
// zero in this backend. Environment identity is checked separately by owner,
// never by its content hash or a reusable raw pointer.
struct NativeMetalSharedConstantKey {
  std::array<int32_t,26> lod_bias{};
  std::array<uint32_t,4> color_info{},clip_plane_bits{};
  uint32_t width=0,height=0,logical_width=0,logical_height=0,samples=0;
  uint32_t booleans=0,alpha_reference_bits=0,alpha_to_mask=0;
  uint32_t color_mask=0,clip_enabled=0,lod_limit_bits=0;
  bool operator==(const NativeMetalSharedConstantKey&) const = default;
};

// Worker-owned single-entry memo. A miss keeps the preceding entry intact;
// Remember is called only after the immutable payload was produced. Weak
// context ownership avoids retaining an environment's guest snapshots.
template<class Payload,class Context> class NativeMetalSharedConstantMemo {
 public:
  std::shared_ptr<const Payload> Find(const NativeMetalSharedConstantKey& key,
      const std::shared_ptr<const Context>& context) const {
    if(!payload_||key!=key_||context.get()!=context_pointer_||
       context_.owner_before(context)||context.owner_before(context_))return {};
    return payload_;
  }
  void Remember(const NativeMetalSharedConstantKey& key,
      const std::shared_ptr<const Context>& context,std::shared_ptr<const Payload> payload) {
    if(!payload)return;
    key_=key;context_pointer_=context.get();context_=context;payload_=std::move(payload);
  }
  void Clear(){*this={};}
 private:
  NativeMetalSharedConstantKey key_;
  const Context* context_pointer_=nullptr;
  std::weak_ptr<const Context> context_;
  std::shared_ptr<const Payload> payload_;
};
}
