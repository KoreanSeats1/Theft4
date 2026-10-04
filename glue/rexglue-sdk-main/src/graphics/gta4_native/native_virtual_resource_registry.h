#pragma once

#include <cstdint>
#include <algorithm>
#include <memory>
#include <optional>
#include <unordered_map>

#include <rex/graphics/gta4_native/title_commands.h>

namespace rex::graphics::gta4_native {

struct NativeVirtualResourceRecord {
  uint32_t resource = 0;
  VirtualResourceKind kind = VirtualResourceKind::kTexture;
  uint32_t wrapper = 0;
  uint32_t companion = 0;
  uint32_t guest_backing_width = 0;
  uint32_t guest_backing_height = 0;
  uint32_t logical_width = 0;
  uint32_t logical_height = 0;
  uint32_t physical_width = 0;
  uint32_t physical_height = 0;
  VirtualResourceScaleDomain scale_domain = VirtualResourceScaleDomain::kLogical;
  uint32_t constructor_caller = 0;
  uint32_t packed_depth_source = 0;
  uint64_t lifetime = 0;
  bool guest_write_conflict = false;
  bool operator==(const NativeVirtualResourceRecord&) const = default;

  bool HasSameIdentity(const RegisterVirtualResourceCommand& command) const {
    return resource == command.resource && kind == command.kind && wrapper == command.wrapper &&
           companion == command.companion &&
           guest_backing_width == command.guest_backing_width &&
           guest_backing_height == command.guest_backing_height &&
           logical_width == command.logical_width && logical_height == command.logical_height &&
           physical_width == command.physical_width &&
           physical_height == command.physical_height && scale_domain == command.scale_domain &&
           constructor_caller == command.constructor_caller &&
           packed_depth_source == command.packed_depth_source;
  }
};

enum class VirtualResourceRegistrationResult : uint8_t {
  kCreated,
  kUnchanged,
  kReplaced,
};

class NativeVirtualResourceRegistry {
 public:
  // Retain the exact producer identity rather than assigning a new lifetime
  // when its registration reaches the render worker.
  bool ApplyCapturedRecord(const NativeVirtualResourceRecord& record) {
    const auto previous=records_.find(record.resource);
    if(previous!=records_.end()&&previous->second==record)return false;
    records_.insert_or_assign(record.resource,record);
    next_lifetime_=std::max(next_lifetime_,record.lifetime+1);
    return true;
  }
  VirtualResourceRegistrationResult Register(const RegisterVirtualResourceCommand& command) {
    auto existing = records_.find(command.resource);
    if (existing != records_.end() && existing->second.HasSameIdentity(command)) {
      return VirtualResourceRegistrationResult::kUnchanged;
    }

    NativeVirtualResourceRecord record;
    record.resource = command.resource;
    record.kind = command.kind;
    record.wrapper = command.wrapper;
    record.companion = command.companion;
    record.guest_backing_width = command.guest_backing_width;
    record.guest_backing_height = command.guest_backing_height;
    record.logical_width = command.logical_width;
    record.logical_height = command.logical_height;
    record.physical_width = command.physical_width;
    record.physical_height = command.physical_height;
    record.scale_domain = command.scale_domain;
    record.constructor_caller = command.constructor_caller;
    record.packed_depth_source = command.packed_depth_source;
    record.lifetime = next_lifetime_++;

    const bool replaced = existing != records_.end();
    records_.insert_or_assign(command.resource, record);
    return replaced ? VirtualResourceRegistrationResult::kReplaced
                    : VirtualResourceRegistrationResult::kCreated;
  }

  const NativeVirtualResourceRecord* Find(uint32_t resource) const {
    const auto existing = records_.find(resource);
    return existing == records_.end() ? nullptr : &existing->second;
  }

  bool MarkGuestWrite(uint32_t resource) {
    const auto existing = records_.find(resource);
    if (existing == records_.end()) {
      return false;
    }
    existing->second.guest_write_conflict = true;
    return true;
  }

  bool PublishHostWrite(uint32_t resource) {
    const auto existing = records_.find(resource);
    if (existing == records_.end()) {
      return false;
    }
    existing->second.guest_write_conflict = false;
    return true;
  }

  bool Erase(uint32_t resource) { return records_.erase(resource) != 0; }

  void Clear() { records_.clear(); }

 private:
  std::unordered_map<uint32_t, NativeVirtualResourceRecord> records_;
  uint64_t next_lifetime_ = 1;
};

// Registrations are CPU protocol state even when a GPU batch is rejected.
// Snapshot once at the first metadata change inside a pending batch; replay
// that batch from its old definitions while retaining the latest worker state.
class NativeVirtualResourceJournal {
 public:
  const NativeVirtualResourceRegistry& Latest() const {return latest_;}
  const NativeVirtualResourceRegistry& BatchBase() const {return base_?*base_:latest_;}
  bool ApplyCapturedRecord(const NativeVirtualResourceRecord& record,bool pending) {
    const auto* old=latest_.Find(record.resource);
    if(old&&*old==record)return false;
    Preserve(pending);latest_.ApplyCapturedRecord(record);return true;
  }
  bool Erase(uint32_t handle,bool pending) {
    if(!latest_.Find(handle))return false;
    Preserve(pending);return latest_.Erase(handle);
  }
  void FinishBatch() {base_.reset();}
  void Clear() {latest_.Clear();base_.reset();}
 private:
  void Preserve(bool pending) {
    if(pending&&!base_)base_=std::make_shared<const NativeVirtualResourceRegistry>(latest_);
  }
  NativeVirtualResourceRegistry latest_;
  std::shared_ptr<const NativeVirtualResourceRegistry> base_;
};

enum class ResourceUnlockDecision : uint8_t {
  kIgnoreNoDirtyUpdate,
  kDirtyGuestResource,
  kInvalidateVirtualHostOwnership,
};

enum class VirtualTextureCaptureAction : uint8_t {
  kUseHostGeneration,
  kCreateDeterministicHostGeneration,
  kCaptureCompleteGuestBacking,
  kRejectDescriptorMismatch,
  kRejectIncompleteGuestBacking,
};

// Virtual render targets have two legitimate ownership modes. A render-to-
// texture generation is host-owned and must never be reconstructed from its
// tiny placeholder allocation. A concretely backed, CPU-lockable texture may
// become guest-owned after an explicit guest write. Keep that distinction in
// one policy so capture cannot accidentally expand a 1x1 placeholder into a
// logical-resolution guest snapshot.
constexpr VirtualTextureCaptureAction ClassifyVirtualTextureCapture(
    bool shape_matches, bool host_generation_matches, bool guest_write_conflict,
    bool guest_backing_complete) {
  if (!shape_matches) {
    return VirtualTextureCaptureAction::kRejectDescriptorMismatch;
  }
  if (host_generation_matches) {
    return VirtualTextureCaptureAction::kUseHostGeneration;
  }
  if (!guest_write_conflict) {
    return VirtualTextureCaptureAction::kCreateDeterministicHostGeneration;
  }
  return guest_backing_complete
             ? VirtualTextureCaptureAction::kCaptureCompleteGuestBacking
             : VirtualTextureCaptureAction::kRejectIncompleteGuestBacking;
}

inline ResourceUnlockDecision ClassifyResourceUnlock(bool virtual_resource,
                                                     ResourceUnlockAccess access) {
  if (access == ResourceUnlockAccess::kNoDirtyUpdate) {
    return ResourceUnlockDecision::kIgnoreNoDirtyUpdate;
  }
  return virtual_resource ? ResourceUnlockDecision::kInvalidateVirtualHostOwnership
                          : ResourceUnlockDecision::kDirtyGuestResource;
}

}  // namespace rex::graphics::gta4_native
