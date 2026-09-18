#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rex::graphics::gta4_native {
// This is used only inside one indexed preparation batch. Original commands
// retain their resources; pages are not repurposed until their slot fence.
// Compare guest-null meaning as well as resource identity and all fetch words.
template <typename Command>
bool NativePreparedTextureInputsEqual(const Command& previous, const Command& next) {
  if (!previous.bindings_prepared || previous.failed_texture_mask || !previous.pipeline_state ||
      !next.pipeline_state || previous.used_texture_mask != next.used_texture_mask) return false;
  for (size_t stage = 0; stage < previous.textures.size(); ++stage) {
    if (!(next.used_texture_mask & (uint32_t{1} << stage))) continue;
    if (previous.pipeline_state->textures[stage] != next.pipeline_state->textures[stage] ||
        previous.textures[stage] != next.textures[stage] ||
        std::memcmp(&previous.texture_fetches[stage], &next.texture_fetches[stage],
                    sizeof(previous.texture_fetches[stage])) != 0) return false;
  }
  return true;
}
template <typename Command>
void CopyNativePreparedTextureBindings(const Command& previous, Command& next) {
  next.descriptor_page=previous.descriptor_page; next.descriptor_copy=previous.descriptor_copy;
  next.image_descriptor_epoch=previous.image_descriptor_epoch;
  next.sampler_descriptor_epoch=previous.sampler_descriptor_epoch;
  next.cached_descriptor_epoch=previous.cached_descriptor_epoch;
  next.texture_descriptor_indices=previous.texture_descriptor_indices;
  next.sampler_descriptor_indices=previous.sampler_descriptor_indices;
  next.draw_descriptor_sets=previous.draw_descriptor_sets;
  next.binding_realization=previous.binding_realization;
  next.realized_image_mask=previous.realized_image_mask;
  next.realized_sampler_mask=previous.realized_sampler_mask;
  next.guest_null_texture_mask=previous.guest_null_texture_mask;
  next.failed_texture_mask=0; next.bindings_prepared=true;
  // Never copy diagnostic-owned objects from another draw.
  next.room_light_input_bindings.reset();
}

// Pointers are borrowed for one indexed preparation batch only. Hinted hits
// are always checked against the complete binding input before reuse.
template <typename Command, size_t Capacity = 64>
class NativePreparedBindingMemo {
  static_assert(Capacity && (Capacity & (Capacity - 1)) == 0);
 public:
  const Command* Find(const Command& next) const {
    if (previous_ && NativePreparedTextureInputsEqual(*previous_, next)) return previous_;
    const Command* candidate = entries_[Bucket(next)];
    return candidate && candidate != previous_ &&
                   NativePreparedTextureInputsEqual(*candidate, next) ? candidate : nullptr;
  }

  void Remember(const Command& command) {
    if (!command.bindings_prepared || command.failed_texture_mask || !command.pipeline_state) return;
    previous_ = &command;
    entries_[Bucket(command)] = &command;
  }

 private:
  static size_t Bucket(const Command& command) {
    uint64_t hint = command.used_texture_mask;
    if (command.used_texture_mask) {
      const auto stage = std::countr_zero(command.used_texture_mask);
      if (size_t(stage) < command.textures.size()) {
        hint ^= uint64_t(reinterpret_cast<uintptr_t>(command.textures[stage].get())) >> 4;
      }
    }
    hint ^= hint >> 17;
    hint *= 0x9E3779B185EBCA87ull;
    return size_t(hint >> 32) & (Capacity - 1);
  }

  std::array<const Command*, Capacity> entries_{};
  const Command* previous_ = nullptr;
};
}  // namespace rex::graphics::gta4_native
