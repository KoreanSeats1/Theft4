#pragma once
#include <cstdint>
#include <cstddef>
#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/info.h>

namespace rex::graphics::gta4_native {

// Pure CPU conversion of tightly packed OWNED guest blocks. Caller untiled and
// captured them before dispatch. Each row writes a disjoint range; padding in
// the host payload was zeroed by its owner. No resource, guest or Vulkan access.
struct NativeTextureBlockConversion {
  xenos::TextureFormat format{};
  xenos::Endian endian{};
  uint32_t blocks_wide = 0, rows_per_slice = 0;
  uint32_t guest_bytes = 0, host_bytes = 0;
  uint32_t guest_block_width = 0, guest_block_height = 0;
  TextureExtent host{};

  void ConvertRows(const uint8_t* source, uint8_t* destination,
                   uint32_t first_row, uint32_t end_row) const {
    for (uint32_t row = first_row; row < end_row; ++row) {
      const uint32_t z = row / rows_per_slice, y = row % rows_per_slice;
      const size_t slice = size_t(z) * host.block_height * host.block_pitch_h * host_bytes;
      for (uint32_t x = 0; x < blocks_wide; ++x) {
        const auto* block = source + (size_t(row) * blocks_wide + x) * guest_bytes;
        if (format == xenos::TextureFormat::k_DXT3A) {
          const size_t offset = slice + (size_t(y) * host.block_pitch_h + x) * host_bytes;
          texture_conversion::ConvertTexelDXT3AToDXT3(endian, destination + offset,
                                                       block, host_bytes);
        } else {
          const size_t offset = slice + size_t(y) * guest_block_height * host.block_pitch_h *
              host_bytes + size_t(x) * guest_block_width * host_bytes;
          const size_t pitch = size_t(host.block_pitch_h) * host_bytes;
          if (format == xenos::TextureFormat::k_CTX1)
            texture_conversion::ConvertTexelCTX1ToR8G8(endian, destination + offset, block, pitch);
          else if (format == xenos::TextureFormat::k_DXN)
            texture_conversion::ConvertTexelDXNToR8G8(endian, destination + offset, block, pitch);
          else
            texture_conversion::ConvertTexelDXT5AToR8(endian, destination + offset, block, pitch);
        }
      }
    }
  }
};
}  // namespace rex::graphics::gta4_native
